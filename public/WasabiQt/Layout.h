#pragma once
//
// Layout expansion — turn a parsed SkinXml::Document plus a chosen
// (container, layout) pair into a tree of ResolvedWidget instances
// ready for the painter.
//
// What expansion does:
//
//   • Walks the named <container>'s named <layout> (id="normal" by
//     default), descending into nested groups.
//
//   • Inlines every `<groupdef>` referenced by `<group id="…">`.  A
//     `<group>` whose `id` matches a known groupdef id is replaced
//     by the groupdef's children, wrapped under the group node so
//     positions stay group-local.
//
//   • Applies `<sendparams group="instanceid" target="widget_id"
//     attr="value"/>` overrides on widgets inside a specific group
//     instance.  Sendparams are scoped by `instanceid`, so the same
//     groupdef can be instantiated multiple times with different
//     overrides (titlebar.streak.left vs titlebar.streak.right).
//
// What it does NOT do:
//
//   • Paint anything (that's the LayerPainter / future widget
//     painters' job).
//
//   • Resolve relatx / relatw / negative w-h.  Those are pixel-time
//     concerns evaluated against the parent's actual size at paint.
//
//   • Run Maki scripts.  Script-driven attribute changes are layered
//     on top of the static expansion at runtime.
//

#include <QtCore/qglobal.h>
#include <QHash>
#include <QList>
#include <QString>

namespace WasabiQt {
namespace SkinXml { struct Document; }

namespace Layout {

struct ResolvedWidget {
    QString tag;                          // lowercased element name
    QString id;                           // attrs["id"], promoted for convenience
    QString instanceId;                   // attrs["instanceid"], same
    QHash<QString, QString> attrs;        // post-sendparams attributes
    QList<ResolvedWidget>   children;     // for <group>/<groupdef>/<container>
    QString sourceFile;
    int     sourceLine = 0;
};

// Find (containerId, layoutId) in `doc` and produce its expanded
// tree.  Returns false if the container or layout isn't present;
// `errMsg`, when non-null, receives a description of the first
// problem.  Defaults to layoutId="normal" — what every Modern-family
// container ships.
bool expandLayout(const SkinXml::Document &doc,
                  const QString &containerId,
                  const QString &layoutId,
                  ResolvedWidget &out,
                  QString *errMsg = nullptr);

// List every <container id="…"> found in the document, in source
// order.  Useful for embedders that want to enumerate available
// windows without parsing the tree themselves.
QStringList containerIds(const SkinXml::Document &doc);

// Same for <layout> children of a given container.
QStringList layoutIds(const SkinXml::Document &doc,
                      const QString &containerId);

}  // namespace Layout
}  // namespace WasabiQt
