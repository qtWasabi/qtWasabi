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
// translates into `qtWasabi::Widget`s.
//
// We intentionally don't model individual element semantics here —
// every tag becomes the same `Element` struct with `tag`, `attrs`,
// `children`, `text`.  The widget builder layer interprets them
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

namespace qtWasabi::SkinXml {

struct Element {
    QString                  tag;          // lowercased
    QHash<QString, QString>  attrs;        // attribute key (lowercased) → value
    QList<Element>           children;
    QString                  text;         // inner text for leaf elements
    QString                  sourceFile;   // path relative to skin root
    int                      sourceLine = 0;
};

// One <script file="…" param="…"/> reference encountered during parse.
struct ScriptRef {
    QString file;     // path relative to skin root
    QString param;    // per-script param string ("" when omitted)
    // Id of the closest enclosing <groupdef>/<group>/<container>/<layout>
    // ancestor (if any).  Used to resolve `getScriptGroup()` at runtime
    // — without it, scripts can't address their sibling widgets.
    QString ownerGroupId;
};

struct Document {
    QString      skinDir;                  // absolute path to the skin root
    Element      root;                     // top-level <wasabixml> element
    QString      skinName;                 // <skininfo><name>
    QString      authorName;               // <skininfo><author>
    QString      skinVersion;              // <skininfo><version>
    QStringList  scriptFiles;              // file paths only (legacy)
    QList<ScriptRef> scripts;              // file + param pairs
    int          includesResolved = 0;
    int          elementCount     = 0;
    QStringList  warnings;                 // non-fatal parse issues
};

// Parse `skinXmlPath` (typically `<skin>/skin.xml`) and return a fully
// expanded document.  Returns true on success, false if the root file
// can't be opened or is malformed; in either case `errMsg`, if
// non-null, receives a description of the first hard error.
bool parse(const QString &skinXmlPath, Document &out, QString *errMsg = nullptr);

// Resolve a container *reference* — as written in a skin's action
// dispatch (`<button action="TOGGLE" param="guid:pl">`) — to the
// `id=` of the matching `<container>` in `doc`.  Wasabi addresses
// containers three ways, all of which this resolves:
//
//   1. By the container's literal `id=` attribute
//      (e.g. param="Pledit"  →  <container id="Pledit">).
//   2. By the container's `component=` GUID
//      (e.g. param="guid:{6B0EDF80-…}" →
//       <container component="guid:{6B0EDF80-…}">).
//   3. By one of Winamp's well-known component *name* aliases
//      (`pl`, `ml`, `vid`/`video`, `vis`/`avs`, `eq`), which map to
//      the canonical component GUID and then match a container's
//      `component=` (e.g. param="guid:pl" → the Playlist Editor's
//      GUID → <container id="Pledit"> in every Modern-family skin).
//
// The `ref` may carry a leading "guid:" / "GUID:" prefix; it is
// stripped before matching.  Matching is case-insensitive and GUID
// brace/dash punctuation is ignored, mirroring Wasabi's `eqi` GUID
// comparison.
//
// Returns the resolved container id on success, or an empty string if
// no container matches (callers then know the reference is dangling).
QString resolveContainerId(const Document &doc, const QString &ref);

}  // namespace qtWasabi::SkinXml
