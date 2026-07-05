// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/SkinXml.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

namespace qtWasabi::SkinXml {

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

// Wasabi registers many widgets under more than one XML tag — a
// namespaced form (`Wasabi:CheckBox`, which colon-normalisation turns
// into `wasabi_checkbox`) and/or a legacy form.  They all instantiate
// the same widget class.  Collapse the known alternates to the one
// canonical tag the widget factory understands, so a skin written with
// any spelling renders the real widget instead of degrading to a
// generic container.  (The `<component>`/`<windowholder>` pair is left
// intact — both are handled directly downstream, including the AVS
// overlay's tag check.)
QString canonicalTag(const QString &t) {
    static const QHash<QString, QString> kTagAliases = {
        {QStringLiteral("wasabi_checkbox"),       QStringLiteral("checkbox")},
        {QStringLiteral("wasabi_dropdownlist"),   QStringLiteral("dropdownlist")},
        {QStringLiteral("wasabi_editbox"),        QStringLiteral("edit")},
        {QStringLiteral("wasabi_historyeditbox"), QStringLiteral("edit")},
        {QStringLiteral("wasabi_tabsheet"),       QStringLiteral("tabsheet")},
        {QStringLiteral("wasabi_radiogroup"),     QStringLiteral("radiogroup")},
        {QStringLiteral("wasabi_titlebox"),       QStringLiteral("text")},
        // SeekBar and EqBand are dedicated slider tags that auto-bind to
        // a transport action (no action= needed in the XML).  Route them
        // to the generic Slider widget; injectImplicitAttrs stamps the
        // action (and EqBand's vertical orientation) below.
        {QStringLiteral("seekbar"),               QStringLiteral("slider")},
        {QStringLiteral("eqband"),                QStringLiteral("slider")},
    };
    const auto it = kTagAliases.constFind(t);
    return it == kTagAliases.constEnd() ? t : it.value();
}

// A few dedicated tags imply attributes the generic widget needs.  Stamp
// them (without overriding anything the skin set explicitly) keyed on the
// ORIGINAL tag, before it is canonicalised away.
void injectImplicitAttrs(const QString &origTag,
                         QHash<QString, QString> &attrs) {
    if (origTag == QStringLiteral("seekbar")) {
        if (!attrs.contains(QStringLiteral("action")))
            attrs.insert(QStringLiteral("action"), QStringLiteral("seek"));
    } else if (origTag == QStringLiteral("eqband")) {
        if (!attrs.contains(QStringLiteral("action")))
            attrs.insert(QStringLiteral("action"), QStringLiteral("eq_band"));
        if (!attrs.contains(QStringLiteral("orientation")))
            attrs.insert(QStringLiteral("orientation"),
                         QStringLiteral("vertical"));
    }
}

// Several XML attributes are pure spelling synonyms in Wasabi (two
// names mapped to the same widget action).  Rewrite the alternate
// spelling to the canonical key at parse time so no downstream read
// site has to know both.  Only unambiguous, collision-free synonyms
// live here; context-specific aliases (e.g. EqBand `band`/`param`) are
// resolved at their read site.
QString canonicalAttrKey(const QString &k) {
    static const QHash<QString, QString> kAttrAliases = {
        {QStringLiteral("regionop"),       QStringLiteral("sysregion")},
        {QStringLiteral("forceupcase"),    QStringLiteral("forceuppercase")},
        {QStringLiteral("forcelocase"),    QStringLiteral("forcelowercase")},
        {QStringLiteral("textdimmedcolor"), QStringLiteral("texthovercolor")},
    };
    const auto it = kAttrAliases.constFind(k);
    return it == kAttrAliases.constEnd() ? k : it.value();
}

// Skins are authored on Windows, so `file=` attributes routinely use
// backslash separators (`scripts\wa2songtimer.maki`).  QFile treats a
// backslash as an ordinary filename character on Linux/macOS, so the
// reference silently fails to resolve.  Normalise once at parse time —
// every downstream consumer (script loader, BitmapRegistry,
// FontRegistry, include resolution) then sees portable paths.
QString normalisePathSeparators(QString path) {
    path.replace(QChar('\\'), QChar('/'));
    return path;
}

void normaliseFileAttr(QHash<QString, QString> &attrs) {
    const auto it = attrs.find(QStringLiteral("file"));
    if (it != attrs.end() && it->contains(QChar('\\')))
        *it = normalisePathSeparators(*it);
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
    // Stack of enclosing scope ids — pushed when entering a <groupdef>,
    // <group>, <container>, or <layout> with an id.  Used to set
    // ScriptRef::ownerGroupId on every <script> encountered.
    QStringList  scopeStack;
    QString      relativeFromSkin(const QString &absPath) const {
        return QDir(doc->skinDir).relativeFilePath(absPath);
    }

    // Is this tag a "scope" — i.e. something a script inside it would
    // consider as its enclosing group?
    static bool isScopeTag(const QString &tag) {
        return tag == QStringLiteral("groupdef") ||
               tag == QStringLiteral("group") ||
               tag == QStringLiteral("container") ||
               tag == QStringLiteral("layout");
    }

    // RAII-ish: push the element's id (when any + scope-tag) on entry,
    // pop on destruction.  Returns the depth pushed (0 or 1).
    struct ScopeGuard {
        QStringList *stack;
        int          pushed;
        ScopeGuard(QStringList *s, const QString &tag,
                   const QHash<QString, QString> &attrs)
            : stack(s), pushed(0) {
            if (isScopeTag(tag)) {
                const QString id = attrs.value(QStringLiteral("id"));
                if (!id.isEmpty()) {
                    stack->append(id);
                    pushed = 1;
                }
            }
        }
        ~ScopeGuard() { while (pushed-- > 0) stack->removeLast(); }
    };

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
                    file = normalisePathSeparators(a.value().toString());
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
        e.tag        = canonicalTag(tag);
        e.sourceFile = relPath;
        e.sourceLine = (int)xml.lineNumber();
        for (const auto &a : xml.attributes()) {
            e.attrs.insert(canonicalAttrKey(a.name().toString().toLower()),
                           a.value().toString());
        }
        normaliseFileAttr(e.attrs);
        injectImplicitAttrs(tag, e.attrs);
        ++doc->elementCount;
        ScopeGuard scope(&scopeStack, tag, e.attrs);
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
                // ScopeGuard for `<script>` doesn't push (script isn't
                // a scope tag), so scopeStack still reflects the
                // ENCLOSING scope at this point.
                if (!scopeStack.isEmpty())
                    ref.ownerGroupId = scopeStack.last();
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
                        file = normalisePathSeparators(a.value().toString());
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
            e.tag        = canonicalTag(tag);
            e.sourceFile = relPath;
            e.sourceLine = (int)xml.lineNumber();
            for (const auto &a : xml.attributes()) {
                e.attrs.insert(canonicalAttrKey(a.name().toString().toLower()),
                               a.value().toString());
            }
            normaliseFileAttr(e.attrs);
            injectImplicitAttrs(tag, e.attrs);
            ++doc->elementCount;

            ScopeGuard scope(&scopeStack, tag, e.attrs);

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
                    if (!scopeStack.isEmpty())
                        ref.ownerGroupId = scopeStack.last();
                    doc->scripts << ref;
                }
            }

            parent.children.append(std::move(e));
        }
        return true;
    }
};

}  // namespace

namespace {

// Normalise a GUID for comparison the way Wasabi's `eqi` does:
// lowercase, drop a leading "guid:" tag, and strip the brace/dash
// punctuation so `guid:{6B0EDF80-C9A5-…}`, `{6b0edf80c9a5…}` and bare
// `6b0edf80c9a5…` all compare equal.
QString normaliseGuid(const QString &raw) {
    QString s = raw.trimmed().toLower();
    if (s.startsWith(QLatin1String("guid:"))) s.remove(0, 5);
    s.remove(QChar('{')).remove(QChar('}')).remove(QChar('-'));
    s = s.trimmed();
    return s;
}

// Winamp's well-known component *name* aliases.  Skins write
// `param="guid:pl"` / `guid:ml` etc.; the running Winamp keeps a
// guid-cache that maps each short name to the component's real GUID,
// and the matching `<container>` declares that GUID via `component=`.
// These are the canonical Winamp 5.x component GUIDs (verified against
// every Modern-family skin's container `component=` attributes).
const QHash<QString, QString> &componentNameAliases() {
    static const QHash<QString, QString> kAliases = {
        { QStringLiteral("pl"),    QStringLiteral("45f3f7c1a6f34ee6a15e125e92fc3f8d") },  // Playlist Editor
        { QStringLiteral("ml"),    QStringLiteral("6b0edf80c9a511d39f2600c04f39ffc6") },  // Media Library
        { QStringLiteral("vid"),   QStringLiteral("f0816d7bfffc434380f2e8199aa15cc3") },  // Video
        { QStringLiteral("video"), QStringLiteral("f0816d7bfffc434380f2e8199aa15cc3") },  // Video (alt name)
        { QStringLiteral("vis"),   QStringLiteral("0000000a000c0010ff7b01014263450c") },  // Visualization / AVS
        { QStringLiteral("avs"),   QStringLiteral("0000000a000c0010ff7b01014263450c") },  // AVS (alt name)
    };
    return kAliases;
}

// Walk the parsed tree collecting every <container>, in source order,
// returning (id, normalised-component-guid) pairs.
void collectContainers(const Element &el,
                       QList<QPair<QString, QString>> &out) {
    if (el.tag == QStringLiteral("container")) {
        const QString id  = el.attrs.value(QStringLiteral("id"));
        // A container names its component GUID via component= OR hold=
        // (aliases, per the Wasabi parser).
        QString cmp = el.attrs.value(QStringLiteral("component"));
        if (cmp.isEmpty()) cmp = el.attrs.value(QStringLiteral("hold"));
        out.append({ id, cmp.isEmpty() ? QString() : normaliseGuid(cmp) });
    }
    for (const auto &c : el.children) collectContainers(c, out);
}

}  // namespace

QString resolveContainerId(const Document &doc, const QString &ref) {
    if (ref.isEmpty()) return QString();

    QList<QPair<QString, QString>> containers;
    collectContainers(doc.root, containers);

    // 1. Literal id= match (case-insensitive).  This is the common
    //    path for skins that toggle a container by its own id and must
    //    keep working unchanged.
    for (const auto &c : containers)
        if (c.first.compare(ref, Qt::CaseInsensitive) == 0)
            return c.first;

    // The remaining matches all key off a GUID.  Strip the optional
    // "guid:" prefix and decide whether `ref` is a short component
    // name alias or a literal GUID.
    QString bare = ref.trimmed();
    if (bare.startsWith(QLatin1String("guid:"), Qt::CaseInsensitive))
        bare = bare.mid(5).trimmed();

    QString wantGuid;
    const auto aliasIt = componentNameAliases().constFind(bare.toLower());
    if (aliasIt != componentNameAliases().constEnd())
        wantGuid = aliasIt.value();          // 3. short-name alias
    else
        wantGuid = normaliseGuid(ref);       // 2. literal GUID

    if (!wantGuid.isEmpty())
        for (const auto &c : containers)
            if (!c.second.isEmpty() && c.second == wantGuid)
                return c.first;

    return QString();
}

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

}  // namespace qtWasabi::SkinXml
