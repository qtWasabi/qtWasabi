// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// Widget base impl: factory dispatch + default paint/hitTest
// recursion + the shared resolveRect formula.

#include <qtWasabi/Widget.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/HitCtx.h>

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
#include "LayoutStatus.h"
#include "Menu.h"
#include <qtWasabi/MilkdropWidget.h>
#include "MultiColumnList.h"
#include "PlaylistPro.h"
#include "Popup.h"
#include "PopupMenu.h"
#include "ProgressGrid.h"
#include "RadioGroup.h"
#include "Rect.h"
#include "ScrollBar.h"
#include "SectionFrame.h"
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

namespace qtWasabi {

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
    // Wasabi XML coords may be fractional — Big Bento's
    // `<vis x="11" y="43.9">` and `<text y="26.9">` declare sub-
    // pixel offsets that a GDI float path would round.
    // `QString::toInt()` rejects decimal strings outright and
    // returns 0 with ok=false, which silently snaps every
    // fractional-y widget to the container top edge — the Big Bento
    // visualizer then ends up overlapping the timer text.  Parse
    // through `toDouble()` so we honour the declared value and
    // truncate.  Skin-agnostic: applies to every widget via the
    // shared rect resolver.
    auto attrCoord = [&](const QString &k) -> int {
        return int(a.value(k).toDouble());
    };
    int x = attrCoord(QStringLiteral("x"));
    int y = attrCoord(QStringLiteral("y"));
    int w = attrCoord(QStringLiteral("w"));
    int h = attrCoord(QStringLiteral("h"));
    bool rx = attrBool(a, QStringLiteral("relatx"));
    bool ry = attrBool(a, QStringLiteral("relaty"));
    bool rw = attrBool(a, QStringLiteral("relatw"));
    bool rh = attrBool(a, QStringLiteral("relath"));
    // `fitparent="1"` is a Wasabi shortcut for "fill the parent in
    // both axes" — i.e. x=0 y=0 w=0 h=0 relatw=1 relath=1.  Explicit
    // per-axis attrs still override; Bento's tab grids and SUI panels
    // rely on this without spelling out the relat-w/h flags.
    //
    // A NUMERIC fitparent="N" (other than 1) is the inset form: fill
    // the parent inset by N pixels on every edge (negative N = overhang).
    // Bento's bottom album cover `<group fitparent="-2" id="…cover2">`
    // uses it to fill its pane with a 2px overhang; without this it has
    // no x/y/w/h and collapses to 0x0 (the cover never renders).
    const QString fpRaw = a.value(QStringLiteral("fitparent"));
    bool fpIsInt = false;
    const int fpN = fpRaw.toInt(&fpIsInt);
    if (attrBool(a, QStringLiteral("fitparent"))) {     // "1"/"true": fill
        if (!a.contains(QStringLiteral("w"))) rw = true;
        if (!a.contains(QStringLiteral("h"))) rh = true;
    } else if (fpIsInt && !fpRaw.isEmpty()) {           // numeric inset
        if (!a.contains(QStringLiteral("x"))) x = fpN;
        if (!a.contains(QStringLiteral("y"))) y = fpN;
        if (!a.contains(QStringLiteral("w"))) { w = -2 * fpN; rw = true; }
        if (!a.contains(QStringLiteral("h"))) { h = -2 * fpN; rh = true; }
    }
    // Wasabi shorthand: a negative SIZE (w/h) with no relat flag means
    // "parent + N" (parent-relative).  The Wasabi convention treats
    // negative sizes as parent-relative regardless of the flag, and
    // skins routinely omit relatw/relath on negative sizes — or typo
    // them (Big Bento's album cover2 has `w="-5" relaw="1"`, where
    // `relaw` is a typo for `relatw`, so without this the cover
    // resolved to w=-5 → clamped to 0 → no art).  Applies ONLY to w/h
    // (size); a negative x/y is a literal off-screen position, not
    // parent-relative.  General, no per-skin glue.
    if (w < 0 && !rw) rw = true;
    if (h < 0 && !rh) rh = true;
    if (rx) x = parent.width()  + x;
    if (ry) y = parent.height() + y;
    if (rw) w = parent.width()  + w;
    if (rh) h = parent.height() + h;
    // Optional menubar shift — Layout.cpp marks grandchildren of
    // MainFrame with `_shift_y=18` when the skin has a <wasabi.menubar>
    // child.  The shift gets applied after relat* resolution so it
    // works regardless of whether y is literal or anchored.
    if (a.contains(QStringLiteral("_shift_y")))
        y += attrCoord(QStringLiteral("_shift_y"));
    // Wasabi:Frame min-size enforcement (the frame honours its
    // declared min{width,height}): the REMAINDER pane never
    // shrinks below the min, and the FIXED pane is capped to
    // parentExtent-min so both fit.  Planted by Layout.cpp's
    // wasabi_frame addPane.  This is what keeps Bento's playlist pane
    // from collapsing to 0 when the fixed cover (100px) exceeds the
    // 92px strip — the playlist gets its minheight (55) and the cover
    // shrinks to 37.  Attribute-driven; no per-skin id checks.
    if (a.contains(QStringLiteral("_frame_min_w"))) {
        const int m = a.value(QStringLiteral("_frame_min_w")).toInt();
        if (w < m) w = m;
    }
    if (a.contains(QStringLiteral("_frame_min_h"))) {
        const int m = a.value(QStringLiteral("_frame_min_h")).toInt();
        if (h < m) h = m;
    }
    if (a.contains(QStringLiteral("_frame_cap_w"))) {
        const int maxW = parent.width() - a.value(QStringLiteral("_frame_cap_w")).toInt();
        if (w > maxW) {
            w = maxW;
            if (attrBool(a, QStringLiteral("_frame_cap_far"))) x = parent.width() - w;
        }
    }
    if (a.contains(QStringLiteral("_frame_cap_h"))) {
        const int maxH = parent.height() - a.value(QStringLiteral("_frame_cap_h")).toInt();
        if (h > maxH) {
            h = maxH;
            if (attrBool(a, QStringLiteral("_frame_cap_far"))) y = parent.height() - h;
        }
    }

    // A negative resolved w/h is always a bug (e.g. a Wasabi:Frame
    // remainder pane whose fixed sibling is larger than the parent —
    // Bento's playlist.dualwnd: height=100 in a 92px parent makes the
    // top pane resolve to -8).  Clamp to 0 so the pane collapses
    // instead of inverting.  Skin-agnostic safety net.
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    return QRect(x, y, w, h);
}

QRect Widget::resolveRect(const QSize &canvas) const {
    return resolveRectFromAttrs(attrs, canvas);
}

void Widget::cacheResolvedRects(QPoint origin, const QSize &canvas) {
    const QRect r = resolveRect(canvas);
    // The "layout" root is not itself offset by its own x/y.
    QPoint childOrigin = origin;
    if (tag != QStringLiteral("layout"))
        childOrigin = QPoint(origin.x() + r.x(), origin.y() + r.y());
    lastCanvasRect = QRect(childOrigin, r.size());
    // Children resolve against this widget's resolved size (the same
    // nesting hitTest uses), so relatw/relath children fill it.
    QSize childCanvas = canvas;
    if (r.width()  > 0) childCanvas.setWidth (r.width());
    if (r.height() > 0) childCanvas.setHeight(r.height());
    const QPoint adj = childOriginAdjustment();
    childOrigin.rx() += adj.x();
    childOrigin.ry() += adj.y();
    for (const auto &c : children)
        if (c) {
            c->parentWidget = this;     // maintain the parent back-pointer
            c->cacheResolvedRects(childOrigin, childCanvas);
        }
}

void Widget::setXmlParam(const QString &name, const QString &value) {
    attrs.insert(name, value);
}

// Defined in SkinRuntimeBridge.cpp.  Routes through the same
// callback Widget::setXmlParam uses, registered by SkinView.
void fireRepaint();

void Widget::requestRepaint() {
    fireRepaint();
}

// Defined in SkinRuntimeBridge.cpp where the `g_byId` registry lives.
extern Widget *findWidgetById(const QString &id);

Widget *Widget::findById(const QString &id) {
    return findWidgetById(id);
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
    // The factory normally hands out ContainerWidget for container
    // tags, whose isContainer() override returns true.  But the
    // SkinQuickItem root (`m_tree`) is a plain Widget value (not
    // polymorphic), so its isContainer() default returns false even
    // though its tag is "layout".  Tag-check as a backstop so the
    // root widget doesn't claim every hit and shadow the actual
    // interactive children underneath.
    if (isContainer() ||
        tag == QStringLiteral("layout") ||
        tag == QStringLiteral("group") ||
        tag == QStringLiteral("container") ||
        tag == QStringLiteral("groupdef") ||
        tag.startsWith(QStringLiteral("wasabi_")))
        return nullptr;
    if (ctx.requireIdOrInteractive && id.isEmpty() && !isInteractive())
        return nullptr;
    // `ghost="1"` widgets are mouse-transparent in Wasabi: clicks and
    // hover pass straight through to whatever is behind them.  Bento puts
    // an alpha=0 `*.glow` overlay layer on TOP of every transport button
    // (Play.glow, Pause.glow, …); without honouring ghost the hit-test
    // returns the glow layer (it has an id) and the button underneath
    // never sees hover/press — so the play/pause/stop/next/prev buttons
    // showed no hover effect.
    if (attrs.value(QStringLiteral("ghost")) == QStringLiteral("1"))
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
    // space as `point`.  Skipped for solid hit regions (list controls
    // that paint transparent between rows) — every pixel of their bbox
    // is interactive, so an alpha gate would drop clicks in the gaps.
    if (!isSolidHitRegion() &&
        ctx.alphaBuf && !ctx.alphaBuf->isNull() &&
        point.x() >= 0 && point.x() < ctx.alphaBuf->width() &&
        point.y() >= 0 && point.y() < ctx.alphaBuf->height()) {
        const QRgb px = ctx.alphaBuf->pixel(point.x(), point.y());
        if (qAlpha(px) <= 16) return nullptr;
    }
    if (outBbox) *outBbox = bbox;
    lastCanvasRect = bbox;
    if (ctx.collect) ctx.collect->append(this);
    return this;
}

// Diagnostic Widget for tags the factory doesn't recognise — emits
// a single trace line per (tag) when WASABIQT_TRACE_UNKNOWN_TAGS=1
// and recurses into children so unrecognised wrappers don't break
// the tree.  Tags that genuinely have no visible representation
// (an unhandled `<menu>`, lifecycle metadata, etc.) end up here.
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

// Factory: tags with a dedicated subclass go to that subclass;
// everything else falls through to LegacyWidget, which routes back
// into the legacy `paintLegacyTag` switch.  As more tags migrate
// the registry grows and the legacy switch shrinks; eventually the
// fallback disappears entirely.
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
    if (t == QStringLiteral("milkdrop"))
        return std::make_unique<MilkdropWidget>();
    if (t == QStringLiteral("colorthemes_list"))
        return std::make_unique<ColorThemesListWidget>();
    // Bevelled chrome frame.  XML alias `sectionframe`; canonical
    // Wasabi convention also accepts the namespaced form
    // `wasabi:sectionframe` for skins that prefer the prefix.
    if (t == QStringLiteral("sectionframe") ||
        t == QStringLiteral("wasabi.sectionframe"))
        return std::make_unique<SectionFrameWidget>();
    // Multi-column list (Win32 LVS_REPORT analogue).
    if (t == QStringLiteral("multicolumnlist") ||
        t == QStringLiteral("wasabi.listview"))
        return std::make_unique<MultiColumnListWidget>();
    // Inputs / placeholders.
    if (t == QStringLiteral("edit") ||
        t == QStringLiteral("wasabi.edit.box"))
        return std::make_unique<EditWidget>();
    if (t == QStringLiteral("windowholder") ||
        t == QStringLiteral("wmh")          ||
        // <component hold="guid:..."> is Wasabi's other spelling for
        // an HWND-host slot.  Bento + Big Bento use it everywhere
        // (vis panel, tab pages, playlist host) where Winamp Modern
        // uses <windowholder>.  Same role: paint the black-rect
        // background, recurse children, surface the slot for
        // engine-level GUID handling (auto-album-cover for video,
        // MilkDrop overlay for AVS).  Without this alias, Bento's
        // entire bottom-half tab area falls through to
        // UnknownWidget and renders nothing.
        t == QStringLiteral("component"))
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
        // <guiobject> is Wasabi's "untyped widget reference" — used
        // inside templates to host arbitrary content.  Treat as a
        // generic container: no own paint, just recurse into
        // children.  Bento uses this for several intermediate
        // wrappers between groupdef and the actual leaf widgets.
        t == QStringLiteral("guiobject") ||
        t.startsWith(QStringLiteral("wasabi_")))
        return std::make_unique<ContainerWidget>();
    // Stubs — registered so the factory recognises the tag, paint
    // inherits the default (visibility check + child recurse).
    // Per-widget state + host integration that drives actual paint
    // behaviour for these lands separately.
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
    if (t == QStringLiteral("layoutstatus"))
        return std::make_unique<LayoutStatusWidget>();
    if (t == QStringLiteral("playlistpro"))
        return std::make_unique<PlaylistProWidget>();
    if (t == QStringLiteral("playlistdirectory"))
        return std::make_unique<PlaylistDirectoryWidget>();
    // Anything else: a diagnostic placeholder that logs the tag once
    // per process when WASABIQT_TRACE_UNKNOWN_TAGS=1 and recurses
    // into children so unrecognised wrappers don't break the tree.
    return std::make_unique<UnknownWidget>();
}

}  // namespace qtWasabi
