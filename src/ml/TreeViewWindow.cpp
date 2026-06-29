// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "TreeViewWindow.h"

#include <atomic>
#include <cstring>
#include <memory>

namespace qtWasabi {
namespace ml {

namespace {

// Process-global per-instance tag generator.  High 32 bits of
// HTREEITEM hold the instance; low 32 bits the item id.  This
// way two distinct TreeViewWindows can never produce the same
// HTREEITEM value.
std::atomic<quint32> g_nextInstanceTag{1};

}  // anonymous

TreeViewWindow::TreeViewWindow()
    : m_instanceTag(g_nextInstanceTag.fetch_add(1)) {
    // The embedded TreeListWidget paints into its own rect; we
    // start with an empty roots list — gen_ml's plugin will
    // populate via TVM_INSERTITEMW.
    m_tree.setRoots({});
}

HTREEITEM TreeViewWindow::encode(quint32 id) const {
    const uintptr_t raw =
        (static_cast<uintptr_t>(m_instanceTag) << 32) |
         static_cast<uintptr_t>(id);
    return reinterpret_cast<HTREEITEM>(raw);
}

quint32 TreeViewWindow::decode(HTREEITEM h) const {
    if (!h) return 0;
    const uintptr_t raw = reinterpret_cast<uintptr_t>(h);
    const quint32 tag   = static_cast<quint32>(raw >> 32);
    if (tag != m_instanceTag) return 0;
    return static_cast<quint32>(raw & 0xFFFFFFFFu);
}

void TreeViewWindow::rebuildRoots() {
    // Build a top-level node list from m_items, then recurse into
    // children via the index below.
    QList<qtWasabi::TreeListNode> roots;

    // Index children by parent for an O(N) build.
    QHash<quint32, QList<quint32>> childIdsByParent;
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) {
        const quint32 parent = decode(it.value().hParent);
        childIdsByParent[parent].append(it.value().id);
    }

    // Sort each parent's children by id so insertion order is
    // preserved (gen_ml expects this — same order MLNavCtrl_*
    // queries return).
    for (auto &v : childIdsByParent) std::sort(v.begin(), v.end());

    // Recursive builder.
    std::function<QList<qtWasabi::TreeListNode>(quint32)> buildChildren;
    buildChildren = [&](quint32 parentId) {
        QList<qtWasabi::TreeListNode> out;
        auto it = childIdsByParent.constFind(parentId);
        if (it == childIdsByParent.constEnd()) return out;
        for (quint32 childId : it.value()) {
            const TvItem &item = m_items.value(childId);
            qtWasabi::TreeListNode node;
            node.invariantId  = item.invariantId;
            node.displayLabel = item.label;
            // Copy the item id so the lazy childProvider lambda
            // below can capture it by value and re-query m_items.
            // (The HIMAGELIST -> icon mapping is not yet rendered.)
            const quint32 idCopy = item.id;
            // Folder when children exist OR the insert struct
            // explicitly declared cChildren > 0.
            const bool hasKids =
                childIdsByParent.contains(idCopy) ||
                item.hasChildren;
            if (hasKids) {
                node.childProvider = [this, idCopy]() {
                    // Snapshot under lock-free access — children
                    // are only mutated from the GUI thread which
                    // is the only paint caller.
                    QHash<quint32, QList<quint32>> idx;
                    for (auto it = m_items.constBegin();
                          it != m_items.constEnd(); ++it) {
                        idx[decode(it.value().hParent)]
                            .append(it.value().id);
                    }
                    QList<qtWasabi::TreeListNode> kids;
                    auto cit = idx.constFind(idCopy);
                    if (cit == idx.constEnd()) return kids;
                    auto ids = cit.value();
                    std::sort(ids.begin(), ids.end());
                    for (quint32 childId : ids) {
                        const TvItem &c = m_items.value(childId);
                        qtWasabi::TreeListNode kn;
                        kn.invariantId  = c.invariantId;
                        kn.displayLabel = c.label;
                        kids.append(kn);
                    }
                    return kids;
                };
            }
            out.append(node);
        }
        return out;
    };

    roots = buildChildren(0);  // 0 = TVI_ROOT
    m_tree.setRoots(roots);
}

LRESULT TreeViewWindow::wndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case TVM_INSERTITEMW: {
            auto *ins = reinterpret_cast<TVINSERTSTRUCTW *>(lp);
            if (!ins) return 0;
            const quint32 id = m_nextId++;
            TvItem item;
            item.id       = id;
            item.hParent  = ins->hParent == TVI_ROOT ? nullptr
                                                     : ins->hParent;
            if (ins->item.mask & TVIF_TEXT && ins->item.pszText) {
                item.label = QString::fromWCharArray(ins->item.pszText);
            }
            if (ins->item.mask & TVIF_PARAM) {
                item.userLParam = ins->item.lParam;
            }
            if (ins->item.mask & TVIF_IMAGE)
                item.iImage = ins->item.iImage;
            if (ins->item.mask & TVIF_SELECTEDIMAGE)
                item.iSelectedImage = ins->item.iSelectedImage;
            if (ins->item.mask & TVIF_CHILDREN)
                item.hasChildren = ins->item.cChildren > 0;
            // invariantId — gen_ml doesn't pass one explicitly;
            // synthesise from the id for stable expand-state.
            item.invariantId = QStringLiteral("tv%1#%2")
                .arg(m_instanceTag).arg(id);
            m_items.insert(id, item);
            rebuildRoots();
            return reinterpret_cast<LRESULT>(encode(id));
        }
        case TVM_DELETEITEM: {
            auto h = reinterpret_cast<HTREEITEM>(lp);
            if (h == TVI_ROOT) {
                m_items.clear();
                rebuildRoots();
                return TRUE;
            }
            const quint32 id = decode(h);
            if (!id) return FALSE;
            // Cascade — remove every item whose ancestor chain
            // contains the deleted id.
            QSet<quint32> toRemove;
            toRemove.insert(id);
            bool grew = true;
            while (grew) {
                grew = false;
                for (auto it = m_items.constBegin();
                      it != m_items.constEnd(); ++it) {
                    if (toRemove.contains(it.value().id)) continue;
                    const quint32 pid = decode(it.value().hParent);
                    if (toRemove.contains(pid)) {
                        toRemove.insert(it.value().id);
                        grew = true;
                    }
                }
            }
            for (quint32 r : toRemove) m_items.remove(r);
            rebuildRoots();
            return TRUE;
        }
        case TVM_GETCOUNT:
            return static_cast<LRESULT>(m_items.size());
        case TVM_SELECTITEM: {
            (void)wp;  // TVGN_CARET selected
            auto h = reinterpret_cast<HTREEITEM>(lp);
            const quint32 id = decode(h);
            if (!id) return FALSE;
            // Translate item id into the visible row index.
            // Simple linear scan over the flattened tree.
            int row = 0;
            for (auto it = m_items.constBegin();
                  it != m_items.constEnd(); ++it, ++row) {
                if (it.value().id == id) {
                    m_tree.setSelection(row);
                    return TRUE;
                }
            }
            return FALSE;
        }
        case TVM_GETNEXTITEM: {
            // TVGN_CARET = currently-selected item.  We return
            // the HTREEITEM for the widget's current selection.
            const quint32 sel = m_tree.selection();
            // Map back: the linear scan order above gave us
            // visit position → item id.  Snapshot the same
            // order to map row→id.  Fine for small trees.
            int row = 0;
            for (auto it = m_items.constBegin();
                  it != m_items.constEnd(); ++it, ++row) {
                if (row == static_cast<int>(sel))
                    return reinterpret_cast<LRESULT>(encode(it.value().id));
            }
            return 0;
        }
        default:
            return 0;
    }
}

void TreeViewWindow::paint(QPainter *p) {
    // The gen_ml host composite WindowObject calls here at this
    // WindowObject's allocated rect, intending us to delegate to
    // the embedded TreeListWidget.
    if (!p) return;
    // TreeListWidget::paint needs a PaintCtx, which is not
    // available at this layer (the host composite owns it), so the
    // actual delegation happens host-side and this body is empty.
}

HWND createTreeView(HWND parent) {
    using namespace qtWasabi::wasabi_compat;
    auto tv = std::make_unique<TreeViewWindow>();
    tv->parent = parent;
    return registerHandle<WindowObject>(std::move(tv));
}

}  // namespace ml
}  // namespace qtWasabi
