// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinXml.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

namespace WasabiQt::SkinXml {

namespace {

// Wasabi uses colon-prefixed tag names (`<Wasabi:StandardFrame:NoStatus/>`,
// `<menu:button_hover/>`, etc.) without an xmlns declaration.  Strict
// XML parsers (Qt's included) treat `:` as a namespace separator and
// reject these.  Convert colons to underscores in tag-name positions
// before parsing.  Attribute values stay untouched (they sit inside
// `"..."` and our regex doesn't match those).
QByteArray normaliseColons(const QByteArray &raw) {
    const QString s = QString::fromUtf8(raw);
    static const QRegularExpression rx(
        QStringLiteral("(<\\/?[A-Za-z_][A-Za-z0-9_.-]*)(:[A-Za-z_][A-Za-z0-9_.-]*)+"));
    QString out;
    out.reserve(s.size());
    int cursor = 0;
    auto it = rx.globalMatch(s);
    while (it.hasNext()) {
        const auto m = it.next();
        out.append(s.mid(cursor, m.capturedStart() - cursor));
        QString span = s.mid(m.capturedStart(), m.capturedLength());
        span.replace(QChar(':'), QChar('_'));
        out.append(span);
        cursor = m.capturedStart() + m.capturedLength();
    }
    out.append(s.mid(cursor));
    return out.toUtf8();
}

// Wrap the file's content in a synthetic <wasabi-root> so files with
// several top-level siblings (e.g. xml/player.xml: two <include>s
// followed by a <container>) parse cleanly under QXmlStreamReader,
// which requires exactly one root element.  The XML declaration —
// `<?xml ... ?>` — must remain at the very start of the file, so we
// keep it in place and inject our wrapper immediately after.
QByteArray wrapInSyntheticRoot(const QByteArray &content) {
    QByteArray out;
    out.reserve(content.size() + 64);

    // Skip leading whitespace.
    int idx = 0;
    while (idx < content.size() && (content[idx] == ' '  ||
                                    content[idx] == '\t' ||
                                    content[idx] == '\n' ||
                                    content[idx] == '\r')) ++idx;

    // Honour an optional XML declaration.
    if (content.mid(idx, 5) == "<?xml") {
        const int closeIdx = content.indexOf("?>", idx);
        if (closeIdx >= 0) {
            out.append(content.mid(0, closeIdx + 2));
            out.append("\n<wasabi-root>\n");
            out.append(content.mid(closeIdx + 2));
        } else {
            out.append("<wasabi-root>\n");
            out.append(content);
        }
    } else {
        out.append("<wasabi-root>\n");
        out.append(content);
    }
    out.append("\n</wasabi-root>\n");
    return out;
}

// Recursively walk a QXmlStreamReader, building the Element tree.
// `<include file="..."/>` is resolved by opening the referenced file
// and splicing its top-level <wasabixml>/<winampabstractionlayer>
// children into the current element's child list.
struct Parser {
    Document    *doc;
    QStringList  fileStack;     // for include-cycle detection
    QString      relativeFromSkin(const QString &absPath) const {
        return QDir(doc->skinDir).relativeFilePath(absPath);
    }

    bool readFile(const QString &absPath, Element &parent, QString *errMsg) {
        if (fileStack.contains(absPath)) {
            if (errMsg) *errMsg = QStringLiteral(
                "include cycle detected at %1").arg(absPath);
            return false;
        }
        QFile f(absPath);
        if (!f.open(QIODevice::ReadOnly)) {
            if (errMsg) *errMsg = QStringLiteral(
                "cannot open %1: %2").arg(absPath, f.errorString());
            return false;
        }
        const QByteArray normalised =
            wrapInSyntheticRoot(normaliseColons(f.readAll()));
        f.close();
        fileStack.append(absPath);
        QXmlStreamReader xml(normalised);

        // Wasabi skin XML files are fragments: each file's content is
        // a list of top-level elements that get spliced directly into
        // the parent at the include point.  Some files wrap the list
        // in <WasabiXML>/<WinampAbstractionLayer> (which we transparently
        // unwrap), others have several siblings at the file root with
        // no wrapper at all (e.g. xml/player.xml: two <include>s, then
        // a <container>).  We walk every top-level event and let
        // readTopLevelElement decide what to do.
        const QString relPath = relativeFromSkin(absPath);
        bool sawAny = false;
        while (!xml.atEnd() && !xml.hasError()) {
            xml.readNext();
            if (xml.isStartElement()) {
                sawAny = true;
                if (!readTopLevelElement(xml, parent, relPath, errMsg)) {
                    fileStack.removeLast();
                    return false;
                }
            }
        }
        if (!sawAny) {
            doc->warnings << QStringLiteral("empty XML in %1").arg(relPath);
        }
        if (xml.hasError()) {
            doc->warnings << QStringLiteral("%1:%2 %3")
                .arg(relPath).arg(xml.lineNumber()).arg(xml.errorString());
        }
        fileStack.removeLast();
        ++doc->includesResolved;
        return true;
    }

    // xml is positioned at a StartElement.  Decide what kind of
    // element it is — wrapper, include, or real — and dispatch.
    bool readTopLevelElement(QXmlStreamReader &xml, Element &parent,
                             const QString &relPath, QString *errMsg) {
        const QString tag = xml.name().toString().toLower();
        if (tag == QStringLiteral("wasabixml") ||
            tag == QStringLiteral("winampabstractionlayer") ||
            tag == QStringLiteral("wasabi-root")) {
            return readChildrenInto(xml, parent, relPath, errMsg);
        }
        if (tag == QStringLiteral("include")) {
            QString file;
            for (const auto &a : xml.attributes()) {
                if (a.name().toString().toLower() == QStringLiteral("file"))
                    file = a.value().toString();
            }
            xml.skipCurrentElement();
            if (file.isEmpty()) return true;
            QFileInfo cur(fileStack.last());
            QString sub = QDir(cur.absolutePath()).absoluteFilePath(file);
            if (!readFile(sub, parent, errMsg)) {
                doc->warnings << QStringLiteral("include failed: %1")
                                      .arg(file);
            }
            return true;
        }
        // Regular element.
        Element e;
        e.tag        = tag;
        e.sourceFile = relPath;
        e.sourceLine = (int)xml.lineNumber();
        for (const auto &a : xml.attributes()) {
            e.attrs.insert(a.name().toString().toLower(),
                           a.value().toString());
        }
        ++doc->elementCount;
        if (!readChildrenInto(xml, e, relPath, errMsg)) return false;

        if (tag == QStringLiteral("name")     && !e.text.isEmpty())
            doc->skinName    = e.text.trimmed();
        else if (tag == QStringLiteral("author") && !e.text.isEmpty())
            doc->authorName  = e.text.trimmed();
        else if (tag == QStringLiteral("version") && !e.text.isEmpty())
            doc->skinVersion = e.text.trimmed();
        else if (tag == QStringLiteral("script")) {
            const QString f = e.attrs.value(QStringLiteral("file"));
            if (!f.isEmpty()) {
                doc->scriptFiles << f;
                ScriptRef ref;
                ref.file  = f;
                ref.param = e.attrs.value(QStringLiteral("param"));
                doc->scripts << ref;
            }
        }
        parent.children.append(std::move(e));
        return true;
    }

    bool readChildrenInto(QXmlStreamReader &xml, Element &parent,
                          const QString &relPath, QString *errMsg) {
        while (!xml.atEnd() && !xml.hasError()) {
            xml.readNext();
            if (xml.isEndElement()) return true;
            if (xml.isCharacters() && !xml.isWhitespace()) {
                parent.text += xml.text().toString();
                continue;
            }
            if (!xml.isStartElement()) continue;

            const QString tag = xml.name().toString().toLower();

            // <include file="…"/> — resolve and splice children.
            if (tag == QStringLiteral("include")) {
                QString file;
                for (const auto &a : xml.attributes()) {
                    if (a.name().toString().toLower() == QStringLiteral("file"))
                        file = a.value().toString();
                }
                xml.skipCurrentElement();
                if (file.isEmpty()) continue;
                // Resolve relative to the file currently being parsed.
                QFileInfo cur(fileStack.last());
                QString sub = QDir(cur.absolutePath())
                                  .absoluteFilePath(file);
                if (!readFile(sub, parent, errMsg)) {
                    // Don't fail the whole parse — record and continue.
                    doc->warnings << QStringLiteral("include failed: %1")
                                          .arg(file);
                }
                continue;
            }

            // Normal element.
            Element e;
            e.tag        = tag;
            e.sourceFile = relPath;
            e.sourceLine = (int)xml.lineNumber();
            for (const auto &a : xml.attributes()) {
                e.attrs.insert(a.name().toString().toLower(),
                               a.value().toString());
            }
            ++doc->elementCount;

            // Recurse.
            if (!readChildrenInto(xml, e, relPath, errMsg)) return false;

            // Capture metadata + script files for top-level access.
            if (tag == QStringLiteral("name")     && !e.text.isEmpty())
                doc->skinName    = e.text.trimmed();
            else if (tag == QStringLiteral("author") && !e.text.isEmpty())
                doc->authorName  = e.text.trimmed();
            else if (tag == QStringLiteral("version") && !e.text.isEmpty())
                doc->skinVersion = e.text.trimmed();
            else if (tag == QStringLiteral("script")) {
                const QString f = e.attrs.value(QStringLiteral("file"));
                if (!f.isEmpty()) {
                    doc->scriptFiles << f;
                    ScriptRef ref;
                    ref.file  = f;
                    ref.param = e.attrs.value(QStringLiteral("param"));
                    doc->scripts << ref;
                }
            }

            parent.children.append(std::move(e));
        }
        return true;
    }
};

}  // namespace

bool parse(const QString &skinXmlPath, Document &out, QString *errMsg) {
    QFileInfo fi(skinXmlPath);
    if (!fi.exists() || !fi.isReadable()) {
        if (errMsg) *errMsg = QStringLiteral("not readable: %1").arg(skinXmlPath);
        return false;
    }
    out = {};
    out.skinDir   = fi.absolutePath();
    out.root.tag  = QStringLiteral("wasabixml");
    out.root.sourceFile = fi.fileName();

    Parser p{&out, {}};
    return p.readFile(fi.absoluteFilePath(), out.root, errMsg);
}

}  // namespace WasabiQt::SkinXml
