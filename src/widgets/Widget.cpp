// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// Widget base impl: factory dispatch + default paint/hitTest
// recursion + the shared resolveRect formula.

#include <WasabiQt/Widget.h>
#include <WasabiQt/PaintCtx.h>
#include <WasabiQt/HitCtx.h>

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

// Diagnostic Widget for tags we don't have a class for yet.  Paints
// nothing visible, recurses children so unrecognised wrappers don't
// break the tree, and logs the tag once per process so the gap is
// observable from outside.
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

// Phase-1 factory: every tag maps to UnknownWidget.  Phases 2/3
// add real per-tag subclasses; the factory grows one registration
// at a time without disturbing the consumers.  Today's TreePainter
// continues to do all the actual painting via paintRecursive —
// this factory's instances aren't yet wired into the paint pipeline.
std::unique_ptr<Widget> Widget::create(const QString & /*normalisedTag*/) {
    return std::make_unique<UnknownWidget>();
}

}  // namespace WasabiQt
