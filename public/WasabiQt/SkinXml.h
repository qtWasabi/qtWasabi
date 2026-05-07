#pragma once
//
// SkinXml — Wasabi skin.xml parser.  Qt-native (QXmlStreamReader),
// no upstream dependency.
//
// Wasabi skins are a tree of XML files rooted at `skin.xml` with an
// `<include file="..."/>` mechanism for splitting the tree across
// many physical files.  This parser walks the tree top-down,
// resolving every `<include>` recursively, and produces a flat
// `Element` representation that downstream code (the widget builder)
// translates into `WasabiQt::Widget`s.
//
// We intentionally don't model individual element semantics here —
// every tag becomes the same `Element` struct with `tag`, `attrs`,
// `children`, `text`.  The widget builder layer (M4) interprets them
// based on `tag`.
//
// Tag names are normalised to lowercase: skins are inconsistent about
// casing (e.g. `<Layer>` vs `<layer>`, `<Button>` vs `<button>`), and
// upstream Wasabi treated them case-insensitively.
//

#include <QtCore/qglobal.h>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

namespace WasabiQt::SkinXml {

struct Element {
    QString                  tag;          // lowercased
    QHash<QString, QString>  attrs;        // attribute key (lowercased) → value
    QList<Element>           children;
    QString                  text;         // inner text for leaf elements
    QString                  sourceFile;   // path relative to skin root
    int                      sourceLine = 0;
};

struct Document {
    QString      skinDir;                  // absolute path to the skin root
    Element      root;                     // top-level <wasabixml> element
    QString      skinName;                 // <skininfo><name>
    QString      authorName;               // <skininfo><author>
    QString      skinVersion;              // <skininfo><version>
    QStringList  scriptFiles;              // collected <script file="…"/> paths
    int          includesResolved = 0;
    int          elementCount     = 0;
    QStringList  warnings;                 // non-fatal parse issues
};

// Parse `skinXmlPath` (typically `<skin>/skin.xml`) and return a fully
// expanded document.  Returns true on success, false if the root file
// can't be opened or is malformed; in either case `errMsg`, if
// non-null, receives a description of the first hard error.
bool parse(const QString &skinXmlPath, Document &out, QString *errMsg = nullptr);

}  // namespace WasabiQt::SkinXml
