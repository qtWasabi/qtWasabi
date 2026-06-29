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
#include <QPoint>
#include <QRegion>
#include <QSize>
#include <QString>

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/Widget.h>

class QPainter;

namespace qtWasabi {
namespace SkinXml { struct Document; }

namespace Layout {

// ResolvedWidget = Widget — the class-per-widget design makes the
// layout tree polymorphic.  The `Widget` base holds the same
// data fields ResolvedWidget historically did (tag/id/attrs/source-
// info); subclasses (LayerWidget, ButtonWidget, …) add per-tag paint /
// hitTest / event-handler overrides plus per-instance runtime state.
//
// Children went from QList<ResolvedWidget> (value-typed) to
// std::vector<std::unique_ptr<Widget>> (polymorphic).  Existing
// `for (const auto &c : node.children)` walks still iterate, but
// element access is `c->tag` / `c->attrs` (c is a unique_ptr).
using ResolvedWidget = ::qtWasabi::Widget;

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

// Hit-test a resolved layout tree at a window-coordinate point, and
// return the deepest widget whose absolute pixel bounds contain it.
// `pointInLayout` is in the same coordinate space as the layout's
// own origin (i.e. window-local for top-level layouts).
//
// Walks children depth-first in reverse paint order so the topmost
// widget wins.  Only considers widgets with a non-empty `action`
// attribute by default — pass `actionOnly=false` to hit any widget.
// Relative coords (relatx / relatw / negative w/h) are not yet
// resolved; widgets using them are skipped.
//
// Many Wasabi widgets (buttons, layers) take their pixel dimensions
// from a named bitmap rather than carrying explicit `w`/`h` attrs.
// Pass a `imageSize` resolver that maps a bitmap-id (the widget's
// `image=` attribute) to the bitmap's pixel size; the hit-test
// falls back to that when the widget has no explicit `w`/`h`.  Pass
// `nullptr` if you only want to hit explicitly-sized widgets.
using ImageSizeResolver = QSize (*)(const QString &bitmapId, void *userdata);

const ResolvedWidget *hitTest(const ResolvedWidget &root,
                              QPoint pointInLayout,
                              bool actionOnly = true,
                              ImageSizeResolver imageSize = nullptr,
                              void *imageSizeUserdata = nullptr,
                              QRect *outBbox = nullptr);

// Build the window mask defined by `sysregion=` layers in the
// resolved tree.  Wasabi convention: layers with `sysregion="1"`
// (or "-2") contribute their opaque pixels to the window region;
// pixels outside that union are not part of the window at all
// (Qt's `setMask()` cuts them off, both visually and for input).
//
// Walks `root` looking for sysregion layers, paints each to an
// offscreen ARGB buffer at `canvas` size, and converts the union
// of alpha-positive pixels into a QRegion.  Returns an empty
// region if the tree has no sysregion layers — embedders can
// detect that and skip setMask, leaving the widget rectangular.
QRegion computeWindowRegion(const ResolvedWidget &root,
                            BitmapRegistry &registry,
                            QSize canvas);

// Walk the resolved tree and pair every `sysregion="-N"` cutout layer
// to its sibling chrome layer by the bitmap-id naming convention
// `<chrome>.region` → `<chrome>` (e.g. `drawer.main.left.region` is
// the cutout for `drawer.main.left`).  Returns a map keyed by the
// chrome's bitmap id whose value is the list of cutouts to overlay on
// it (image id + offset within the chrome bitmap).
//
// The offset is `cutout.layer.xy - chrome.layer.xy` in the parent
// group's local coord space — works for both same-position siblings
// (drawer's left+left.bottom.region at (0,0)) and corner-mask siblings
// painted at a fixed offset inside the chrome (player.main.left's
// 180×126 chrome with a 6×6 mask at (0,120)).
//
// SkinQuickItem hands the result to BitmapRegistry::setChromeCutouts so
// `chromeImageFor()` returns chrome bitmaps with cutouts baked into
// their alpha — drawing the masked chrome on top of another group's
// chrome at the same canvas pixels then leaves the underlying chrome
// visible at the cut area (no notch at the player/drawer overlap).
QHash<QString, QList<ChromeCutout>>
    collectChromeCutouts(const ResolvedWidget &root,
                         BitmapRegistry &registry);

// Replay the subtractive half of the sysregion walk directly on a
// QPainter aimed at the just-painted chrome buffer.  For every layer
// with `sysregion="-N"` (cutout polarity), paints its bitmap with
// CompositionMode_DestinationOut at the layer's resolved rect so the
// destination's alpha is zeroed where the cutout bitmap is opaque.
//
// Used by SkinQuickItem after paintInto: the QQuickWindow's setMask
// only sets the Wayland input region, not the visible surface shape,
// so the rounded corners and drawer-edge cuts have to live in the
// texture's alpha channel itself.  setMask is redundant once the
// alpha is painted correctly.
void paintRegionCutouts(QPainter &p, const ResolvedWidget &root,
                        BitmapRegistry &registry, QSize canvas);

// Walk a resolved tree and synchronise each `<GroupXFade>`'s
// children with its current `groupid` attribute.  GroupXFade is the
// Wasabi page-swapper: scripts call `setXmlParam("groupid", "X")` to
// switch to a different groupdef inside it, and the engine is
// expected to re-instantiate that groupdef's children in the widget.
// Without this pass the GroupXFade widget paints an empty box no
// matter how many times Maki swaps its groupid.
//
// Idempotent — uses a marker attribute (`_resolved_groupid`) to skip
// re-materialisation when the groupid hasn't changed since last call.
// Should run before TreePainter::paintTree on each frame.
void resolveGroupXFadePages(ResolvedWidget &root,
                            const SkinXml::Document &doc);

// Apply static equivalents of well-known Maki scripts to a resolved
// tree.  Mirrors the geometry / visibility mutations a script's
// load-time handlers would otherwise do (titlebar.m's resizeObjects,
// etc.).  This is separate from expandLayout — call it explicitly
// when you want the legacy static path; skip it when SkinRuntime
// will dispatch the real .maki scripts and mutate widgets through
// setXmlParam.
void runKnownScripts(ResolvedWidget &root, int layoutWidth);

// Wire up the engine-level "stepper" pattern that Wasabi skins
// use for incremented/decremented config values (canonical example:
// the crossfade time slider + Decrease/Increase buttons + numeric
// display in WinampModernPP/Winamp Modern/Bento).  Walks the tree
// once and for each container scope where this structure exists:
//
//   • a widget with `cfgattrib=` AND `high=` (defines int range)
//   • sibling button(s) whose `id` ends in `Decrease` / `Increase`
//   • sibling text widget(s) whose `id` ends in `Display`
//
// it caches the cfgattrib key + low/high/step on the matching
// button so its `onLeftButtonUp` inc/decs via `CfgAttribStore`,
// and binds the text widget to the same key so it auto-rewrites
// when the value changes.  Skin-agnostic; no per-skin XML edits
// needed.
void wireSteppers(ResolvedWidget &root);


// `applyPlaylistEnlarge` is intentionally absent — the Maki VM drives
// Bento's playlist enlarge itself (pledit.m playlist_enlarge_attrib.onDataChanged +
// g_playlist.onResize + playlistpro.frameGroup.onResize), settled to a
// geometry fixpoint by SkinRuntime::dispatchInitialResize.  No per-skin
// emulation needed; works for any .wal skin that drives its own onResize.

// Re-split a Wasabi:Frame node against a new divider position (the live
// pullbar position).  Backs the Maki Frame.setPosition() binding; the
// frame node must carry the `_frame_*` metadata planted at expansion.
void applyFrameDividerPos(ResolvedWidget &frameNode, int pos);

// `wireMenuBackgrounds` — propagate menu.text.X widget's x/w to its
// sibling menu.layer.X.normal/hover/down background layers so the
// titlebar's per-menu-item highlight band paints continuously.
// Engine-level workaround for a CALLM2 DLF-dispatch bug in our
// Maki VM port that prevents Bento's mainmenu.maki from setting
// the background layer widths directly.  Call after Maki dispatch.
void wireMenuBackgrounds(ResolvedWidget &root);

// `wireMenuAlign` — static menualign.maki equivalent: lay the named
// menu groups (File/Playlist/Sort/Help, …) side-by-side under their
// owner group, each at the running x offset, advancing by the width of
// its `autowidthsource=` label bitmap.  Mirrors menualign.m's
// onScriptLoaded loop (`tmp.setXmlParam("x", offset); offset +=
// tmp.getAutoWidth()`).  Must run at load time (it needs the
// BitmapRegistry for the label widths) on ANY window that carries a
// menubar — the main player AND the Playlist Editor / Media Library
// subwindows — so their menu items don't collapse onto x=0.
void wireMenuAlign(ResolvedWidget &root, const SkinXml::Document &doc,
                   BitmapRegistry &reg);

// `resolveBitmapAutoWidths` — set `w` on any widget that declares an
// `autowidthsource=` pointing at a child with an `image=` (a bitmap
// label), to that bitmap's width.  The Wasabi layout engine resolves a
// group's autoWidth from its label this way; the menubar's File/Play/…
// groups rely on it so their hover/click area is the label width (the VM's
// menualign.maki only sets each group's x, not its width).  Must run AFTER
// the BitmapRegistry is populated (unlike the text-source autowidth pass in
// expandLayout, which runs before the registry exists).  Scoped per widget
// — the Playlist/Media-Library menu groups all reuse the child id
// "label.txt", so the lookup is restricted to each group's own subtree.
// General: any skin's autowidthsource-bitmap widget, no per-skin ids.
void resolveBitmapAutoWidths(ResolvedWidget &root, BitmapRegistry &reg);

// `dumpResolved` — diagnostic walker.  Prints every widget that
// matched a paint pass (`lastCanvasRect.isValid()` or `attrs[w]/h`
// declare a size) along with its id, tag, resolved rect, and
// visibility.  Useful for diagnosing layout problems where on-
// screen positions don't match the XML's expectation — by reading
// the actual rendered geometry tree we can pinpoint which
// groupdef / Wasabi:Frame / `relatw=` cascade produced the wrong
// number.  Gated by `WASABIQT_TRACE_LAYOUTROOT=1` in qtamp's
// startup path.  `out` defaults to stderr if null.
void dumpResolved(const ResolvedWidget &root, QSize canvas,
                  FILE *out = nullptr);

}  // namespace Layout
}  // namespace qtWasabi
