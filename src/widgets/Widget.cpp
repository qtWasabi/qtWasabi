// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// Widget base impl: factory dispatch + default paint/hitTest
// recursion + the shared resolveRect formula.

#include <WasabiQt/Widget.h>
#include <WasabiQt/PaintCtx.h>
#include <WasabiQt/HitCtx.h>
#include <WasabiQt/TreePainter.h>

#include "ComponentBucket.h"
#include "Container.h"
#include "Edit.h"
#include "GroupXFade.h"
#include "Layer.h"
#include "Rect.h"
#include "WindowHolder.h"

#include <QPainter>
#include <QSet>

#include <cstdio>
#include <cstdlib>

namespace WasabiQt {

namespace {
// Diagnostic: log once per unknown tag so we know which Wasabi
// element types still need a Widget subclass.  Emits at most one
// line per (tag) across the process lifetime.
QSet<QString> &unknownTagsSeen() {
    static QSet<QString> s;
    return s;
}

bool attrBool(const QHash<QString, QString> &a, const QString &k) {
    const QString v = a.value(k);
    return v == QStringLiteral("1") ||
           v.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0;
}
}  // namespace

QRect Widget::resolveRectFromAttrs(const QHash<QString, QString> &a,
                                     QSize parent) {
    int x = a.value(QStringLiteral("x")).toInt();
    int y = a.value(QStringLiteral("y")).toInt();
    int w = a.value(QStringLiteral("w")).toInt();
    int h = a.value(QStringLiteral("h")).toInt();
    bool rx = attrBool(a, QStringLiteral("relatx"));
    bool ry = attrBool(a, QStringLiteral("relaty"));
    bool rw = attrBool(a, QStringLiteral("relatw"));
    bool rh = attrBool(a, QStringLiteral("relath"));
    // `fitparent="1"` is a Wasabi shortcut for "fill the parent in
    // both axes" — i.e. x=0 y=0 w=0 h=0 relatw=1 relath=1.  Explicit
    // per-axis attrs still override; Bento's tab grids and SUI panels
    // rely on this without spelling out the relat-w/h flags.
    if (attrBool(a, QStringLiteral("fitparent"))) {
        if (!a.contains(QStringLiteral("w"))) rw = true;
        if (!a.contains(QStringLiteral("h"))) rh = true;
    }
    if (rx) x = parent.width()  + x;
    if (ry) y = parent.height() + y;
    if (rw) w = parent.width()  + w;
    if (rh) h = parent.height() + h;
    return QRect(x, y, w, h);
}

QRect Widget::resolveRect(const QSize &canvas) const {
    return resolveRectFromAttrs(attrs, canvas);
}

void Widget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    // Default: container-style recursion.  Concrete widgets override
    // to paint their own visuals.  Visibility filter applies here so
    // every Widget subclass that calls Widget::paint as a fallback
    // gets it for free.
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    for (const auto &c : children)
        if (c) c->paint(p, ctx, canvas);
}

Widget *Widget::hitTest(QPoint point, QPoint origin,
                         const QSize &canvas,
                         HitCtx &ctx, QRect *outBbox) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return nullptr;
    const QRect r = resolveRect(canvas);
    QPoint childOrigin = origin;
    if (tag != QStringLiteral("layout"))
        childOrigin = QPoint(origin.x() + r.x(), origin.y() + r.y());
    QSize childCanvas = canvas;
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    // Children-first depth search (topmost wins).
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (!*it) continue;
        if (auto *hit = (*it)->hitTest(point, childOrigin, childCanvas,
                                       ctx, outBbox))
            return hit;
    }

    if (ctx.actionOnly && !attrs.contains(QStringLiteral("action")))
        return nullptr;
    if (isContainer()) return nullptr;

    // Self bbox: prefer resolved w/h, fall back to bitmap-image
    // dimensions for widgets without sizes when the embedder
    // provided an imageSize resolver.
    int width  = r.width();
    int height = r.height();
    if ((width <= 0 || height <= 0) && ctx.imageSize) {
        const QString img = attrs.value(QStringLiteral("image"));
        if (!img.isEmpty()) {
            const QSize imgSize = ctx.imageSize(img);
            if (width  <= 0) width  = imgSize.width();
            if (height <= 0) height = imgSize.height();
        }
    }
    if (width <= 0 || height <= 0) return nullptr;

    const QRect bbox(childOrigin.x(), childOrigin.y(), width, height);
    if (!bbox.contains(point)) return nullptr;
    if (outBbox) *outBbox = bbox;
    return this;
}

// Default Widget for tags we haven't migrated to a dedicated class
// yet.  Forwards to `TreePainter::paintLegacyTag`, which contains
// the monolithic `if (t == "...")` switch that used to live in
// `paintRecursive`.  Each phase-2/3 migration peels one tag off the
// legacy switch into its own Widget subclass and registers it in
// `Widget::create`; until every tag is migrated, the unmigrated
// remainder keeps rendering through this fallback.
class LegacyWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override {
        WasabiQt::TreePainter::paintLegacyTag(p, *this, ctx, canvas);
    }
};

// Diagnostic Widget for tags the factory's registry doesn't recognise
// at all.  Currently unused (LegacyWidget catches every tag) but kept
// for when the factory grows an allowlist that excludes unknown XML.
class UnknownWidget : public Widget {
public:
    void paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) override {
        if (std::getenv("WASABIQT_TRACE_UNKNOWN_TAGS")) {
            auto &seen = unknownTagsSeen();
            if (!seen.contains(tag)) {
                seen.insert(tag);
                std::fprintf(stderr,
                    "[widget] unknown tag: %s (id=%s) — paint stub, "
                    "children still recurse\n",
                    tag.toLocal8Bit().constData(),
                    id.toLocal8Bit().constData());
            }
        }
        Widget::paint(p, ctx, canvas);
    }
};

// Phase-2 factory: tags with a dedicated subclass go to that
// subclass; everything else falls through to LegacyWidget, which
// routes back into the legacy `paintLegacyTag` switch.  As more
// tags migrate (phases 2/3) the registry grows and the legacy
// switch shrinks; eventually the fallback disappears entirely.
//
// Container-family tags (group / container / layout / groupdef and
// the XUI-mangled wasabi_* groupdef refs) share ContainerWidget;
// componentbucket / groupxfade are their own subclasses on top.
std::unique_ptr<Widget> Widget::create(const QString &normalisedTag) {
    const QString &t = normalisedTag;
    if (t == QStringLiteral("layer"))
        return std::make_unique<LayerWidget>();
    if (t == QStringLiteral("rect"))
        return std::make_unique<RectWidget>();
    if (t == QStringLiteral("edit") ||
        t == QStringLiteral("wasabi.edit.box"))
        return std::make_unique<EditWidget>();
    if (t == QStringLiteral("windowholder") ||
        t == QStringLiteral("wmh"))
        return std::make_unique<WindowHolderWidget>();
    if (t == QStringLiteral("componentbucket"))
        return std::make_unique<ComponentBucketWidget>();
    if (t == QStringLiteral("groupxfade"))
        return std::make_unique<GroupXFadeWidget>();
    if (t == QStringLiteral("group")     ||
        t == QStringLiteral("container") ||
        t == QStringLiteral("layout")    ||
        t == QStringLiteral("groupdef")  ||
        t.startsWith(QStringLiteral("wasabi_")))
        return std::make_unique<ContainerWidget>();
    return std::make_unique<LegacyWidget>();
}

}  // namespace WasabiQt
