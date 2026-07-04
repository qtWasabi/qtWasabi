#pragma once
//
// TreeListWidget — hierarchical browse list with indent + twist
// + per-row icon + selection.
//
// The row layout the MediaLibraryPanel substitute proved out is
// lifted into a reusable widget so:
//
//   * The TVM_* / LVM_* dispatch (in wasabi-compat) can route
//     gen_ml's tree messages to a real receiver.
//   * MlHostWidget composes a sidebar tree without re-implementing
//     twist/indent/icon paint.
//   * Any host modern skin that ships a `<TreeList>` element in
//     its own XML gets a styled tree out of the box.
//
// Data model: TreeListNode carries one row's display state plus a
// lazy child-provider lambda.  Folder rows (provider != null) get
// a twist triangle; leaf rows don't.  The host populates the root
// node list via `setRoots()`.  Selection + expand state live in
// the widget; flatten happens per paint (cheap — tree depth small).
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

struct TreeListNode {
    QString invariantId;                 // stable id for expand-state keys
    QString displayLabel;                // visible row text
    QString iconResource;                // bitmap-registry id OR path
    bool    defaultExpanded = false;
    std::function<QList<TreeListNode>()> childProvider;  // null ⇒ leaf
};

class TreeListWidget : public Widget {
public:
    TreeListWidget() = default;

    // Replace the root node list.  Triggers a repaint.  Selection
    // is reset to row 0 if the previous selection index is out of
    // range against the new tree.
    void setRoots(QList<TreeListNode> roots);
    const QList<TreeListNode> &roots() const { return m_roots; }

    // Selection.  Index is into the flattened visible-row list at
    // paint time.
    int  selection() const { return m_selection; }
    void setSelection(int row);

    // Stable id of the selected row's node (invariantId, falling back
    // to displayLabel), resolved against the last painted flatten.
    // Empty when nothing has painted yet or selection is out of range.
    QString selectedNodeId() const {
        if (m_selection < 0 || m_selection >= m_lastVisible.size())
            return {};
        const TreeListNode &n = m_lastVisible[m_selection].node;
        return n.invariantId.isEmpty() ? n.displayLabel : n.invariantId;
    }

    // Bitmap-registry the widget should consult for icon ids.  When
    // a node's iconResource starts with `/` or a known file
    // extension, the widget loads it directly via QImage; otherwise
    // it calls into the registry.
    void setBitmapResolver(std::function<QImage(const QString &)> fn) {
        m_iconResolver = std::move(fn);
    }

    bool isInteractive() const override { return true; }

    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override;
    void onLeftButtonDown(QPoint pos, PaintCtx &ctx) override;

private:
    QList<TreeListNode>  m_roots;
    QSet<QString>        m_expanded;     // by invariantId
    int                  m_selection = 0;

    std::function<QImage(const QString &)> m_iconResolver;

    // Per-paint cache so onLeftButtonDown can map a hit y back to a
    // row index without re-flattening.
    struct VisibleRow {
        TreeListNode node;               // by-value (provider lambdas
                                          // may return temporaries)
        int          depth      = 0;
        bool         isFolder   = false;
        bool         isExpanded = false;
    };
    QList<VisibleRow>    m_lastVisible;
    QRect                m_lastRect;
    int                  m_lastRowH  = 14;
    int                  m_lastFirstRowY = 0;

    QList<VisibleRow>    flattenVisible() const;
};

}  // namespace qtWasabi
