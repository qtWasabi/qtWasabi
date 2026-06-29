#pragma once
//
// MediaLibraryPanel — engine-level visual substitute for the
// Wasabi Media Library plugin (canonical GUID
// `{6B0EDF80-C9A5-11D3-9F26-00C04F39FFC6}`) when no real ml.dll
// host is plumbed in.
//
// Sidebar tree is built from a registry of `MlPlugin` entries.
// Each entry mirrors the registration handshake real Winamp uses
// (gen_ml's MLNavCtrl_InsertItem) — invariant id, display label,
// icon resource, and a child-provider lambda that returns either a
// static category list (ml_local) or a dynamic enumeration
// (ml_disc → mounted optical drives; ml_playlists → on-disk
// playlist files; etc.).  The registry order is the registration
// order — same as how the real plugin host stacks the tree.
//

#include <qtWasabi/Widget.h>

#include <QHash>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSet>
#include <QString>

#include <functional>

namespace qtWasabi {

// Single tree node — describes either a plugin's root entry or one
// of its (static or dynamic) children.  The `childProvider` is
// invoked lazily when the node is expanded; null means the node
// is a leaf.  `defaultExpanded` is honoured on first paint only;
// subsequent toggles go through `MediaLibraryPanel::m_expanded`.
struct MlNode {
    QString                                invariantId;     // stable id, e.g. "Local Media"
    QString                                displayLabel;    // e.g. "Local Library"
    QString                                iconRelPath;     // relative to ML icons base, may be empty
    bool                                   defaultExpanded = false;
    std::function<QList<MlNode>()>         childProvider;   // null => leaf
};

class MediaLibraryPanel : public Widget {
public:
    MediaLibraryPanel();
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    bool isInteractive() const override { return true; }
    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;

private:
    // Plugin registry — populated in the constructor with one entry
    // per active ml_* plugin equivalent (ml_local, ml_playlists,
    // ml_online, ml_devices, ml_disc, ml_bookmarks, ml_history, …).
    QList<MlNode>            m_plugins;

    // Per-node expand state keyed by invariant path (parent ids
    // joined with '/').  Persists between paints.
    QSet<QString>            m_expanded;

    // Currently-selected visible-row index.  Recomputed every paint
    // from the flattened tree.
    int                      m_sidebarSel = 1;   // first leaf under Local Library
    QRect                    m_lastPanelRect;    // canvas-space

    // Geometry cached at paint time so click routing can map
    // (y - sidebarY) / rowH back to a visible row.
    int                      m_lastSidebarY = 0;
    int                      m_lastRowH     = 13;
    int                      m_lastVisibleCount = 0;

    // Flatten the registry into a paint/click-ready list of visible
    // rows, honouring expanded-state of folder nodes.  Called on
    // every paint and click — cheap because the tree depth is small
    // and the providers cache.
    struct VisibleRow {
        // Snapshot, not a pointer — dynamic plugin providers
        // (children_disc, children_playlists, …) return a fresh
        // QList<MlNode> on each call, so a pointer back into one of
        // those temporaries would dangle the moment the local list
        // goes out of scope inside `flattenVisible`.
        MlNode  node;
        int     depth;
        bool    isFolder;
        bool    isExpanded;
        QString parentPath;
    };
    QList<VisibleRow>        flattenVisible() const;
};

}  // namespace qtWasabi

