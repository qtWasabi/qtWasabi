// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// Widget base impl: factory dispatch + default paint/hitTest
// recursion + the shared resolveRect formula.

#include <WasabiQt/Widget.h>
#include <WasabiQt/PaintCtx.h>
#include <WasabiQt/HitCtx.h>

#include "AlbumArt.h"
#include "AnimatedLayer.h"
#include "Browser.h"
#include "Button.h"
#include "CheckBox.h"
#include "ColorThemesList.h"
#include "ComponentBucket.h"
#include "Container.h"
#include "DropDownList.h"
#include "Edit.h"
#include "EqVis.h"
#include "Grid.h"
#include "GroupXFade.h"
#include "GuiList.h"
#include "HideObject.h"
#include "Images.h"
#include "Layer.h"
#include "Menu.h"
#include "Popup.h"
#include "PopupMenu.h"
#include "ProgressGrid.h"
#include "RadioGroup.h"
#include "Rect.h"
#include "ScrollBar.h"
#include "Slider.h"
#include "Splitter.h"
#include "Status.h"
#include "TabSheet.h"
#include "Text.h"
#include "TreeList.h"
#include "Vis.h"
#include "WindowHolder.h"
#include "XmlRenderer.h"

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

void Widget::setXmlParam(const QString &name, const QString &value) {
    attrs.insert(name, value);
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
    // Container/ScrollBar/TreeList-style interior offsets — defaults
    // to (0,0) for non-scrolled widgets.  ComponentBucket overrides.
    const QPoint adj = childOriginAdjustment();
    childOrigin.rx() += adj.x();
    childOrigin.ry() += adj.y();
    QSize childCanvas = canvas;
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());

    // Children-first depth search (topmost wins).  In list-collect
    // mode we keep walking after a hit so every match lands in
    // ctx.collect; in single-result mode we short-circuit on the
    // first hit.
    Widget *topmost = nullptr;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (!*it) continue;
        if (auto *hit = (*it)->hitTest(point, childOrigin, childCanvas,
                                       ctx, outBbox)) {
            if (!topmost) topmost = hit;
            if (!ctx.collect) return hit;
        }
    }
    if (topmost) return topmost;

    if (ctx.actionOnly && !attrs.contains(QStringLiteral("action")))
        return nullptr;
    if (isContainer()) return nullptr;
    if (ctx.requireIdOrInteractive && id.isEmpty() && !isInteractive())
        return nullptr;

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

    // Optional alpha sample: reject hits on visually-transparent
    // pixels.  Coordinates in `alphaBuf` are in the same canvas
    // space as `point`.
    if (ctx.alphaBuf && !ctx.alphaBuf->isNull() &&
        point.x() >= 0 && point.x() < ctx.alphaBuf->width() &&
        point.y() >= 0 && point.y() < ctx.alphaBuf->height()) {
        const QRgb px = ctx.alphaBuf->pixel(point.x(), point.y());
        if (qAlpha(px) <= 16) return nullptr;
    }
    if (outBbox) *outBbox = bbox;
    if (ctx.collect) ctx.collect->append(this);
    return this;
}

// Diagnostic Widget for tags the factory doesn't recognise — emits
// a single trace line per (tag) when WASABIQT_TRACE_UNKNOWN_TAGS=1
// and recurses into children so unrecognised wrappers don't break
// the tree.  Tags that genuinely have no visible representation
// (`<menu>` until Phase 5, lifecycle metadata, etc.) end up here.
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
    // Bitmap / blit widgets.
    if (t == QStringLiteral("layer"))
        return std::make_unique<LayerWidget>();
    if (t == QStringLiteral("animatedlayer"))
        return std::make_unique<AnimatedLayerWidget>();
    if (t == QStringLiteral("albumart"))
        return std::make_unique<AlbumArtWidget>();
    if (t == QStringLiteral("rect"))
        return std::make_unique<RectWidget>();
    // Text / ticker.
    if (t == QStringLiteral("text"))
        return std::make_unique<TextWidget>();
    if (t == QStringLiteral("songticker"))
        return std::make_unique<SongTickerWidget>();
    // Button family.
    if (t == QStringLiteral("button"))
        return std::make_unique<ButtonWidget>();
    if (t == QStringLiteral("togglebutton"))
        return std::make_unique<ToggleButtonWidget>();
    if (t == QStringLiteral("nstatesbutton"))
        return std::make_unique<NStatesButtonWidget>();
    // Slider / interactive.
    if (t == QStringLiteral("slider"))
        return std::make_unique<SliderWidget>();
    // Bitmap-strip / driven by host signals.
    if (t == QStringLiteral("grid"))
        return std::make_unique<GridWidget>();
    if (t == QStringLiteral("progressgrid"))
        return std::make_unique<ProgressGridWidget>();
    if (t == QStringLiteral("images"))
        return std::make_unique<ImagesWidget>();
    if (t == QStringLiteral("status"))
        return std::make_unique<StatusWidget>();
    if (t == QStringLiteral("vis"))
        return std::make_unique<VisWidget>();
    if (t == QStringLiteral("colorthemes_list"))
        return std::make_unique<ColorThemesListWidget>();
    // Inputs / placeholders.
    if (t == QStringLiteral("edit") ||
        t == QStringLiteral("wasabi.edit.box"))
        return std::make_unique<EditWidget>();
    if (t == QStringLiteral("windowholder") ||
        t == QStringLiteral("wmh"))
        return std::make_unique<WindowHolderWidget>();
    // Containers.
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
    // Phase 5 stubs — registered so the factory recognises the tag,
    // paint inherits the default (visibility check + child recurse).
    // Phase 6 lands per-widget state + host integration that drives
    // actual paint behaviour for these.
    if (t == QStringLiteral("menu"))
        return std::make_unique<MenuWidget>();
    if (t == QStringLiteral("eqvis"))
        return std::make_unique<EqVisWidget>();
    if (t == QStringLiteral("guilist") || t == QStringLiteral("list"))
        return std::make_unique<GuiListWidget>();
    if (t == QStringLiteral("treelist"))
        return std::make_unique<TreeListWidget>();
    if (t == QStringLiteral("scrollbar"))
        return std::make_unique<ScrollBarWidget>();
    if (t == QStringLiteral("popup"))
        return std::make_unique<PopupWidget>();
    if (t == QStringLiteral("popupmenu"))
        return std::make_unique<PopupMenuWidget>();
    if (t == QStringLiteral("splitter"))
        return std::make_unique<SplitterWidget>();
    if (t == QStringLiteral("tabsheet"))
        return std::make_unique<TabSheetWidget>();
    if (t == QStringLiteral("xmlrenderer"))
        return std::make_unique<XmlRendererWidget>();
    if (t == QStringLiteral("checkbox"))
        return std::make_unique<CheckBoxWidget>();
    if (t == QStringLiteral("radiogroup"))
        return std::make_unique<RadioGroupWidget>();
    if (t == QStringLiteral("dropdownlist"))
        return std::make_unique<DropDownListWidget>();
    if (t == QStringLiteral("browser"))
        return std::make_unique<BrowserWidget>();
    if (t == QStringLiteral("hideobject"))
        return std::make_unique<HideObjectWidget>();
    // Anything else: a diagnostic placeholder that logs the tag once
    // per process when WASABIQT_TRACE_UNKNOWN_TAGS=1 and recurses
    // into children so unrecognised wrappers don't break the tree.
    return std::make_unique<UnknownWidget>();
}

}  // namespace WasabiQt
