// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// TreePainter::paintTree — the public entrypoint that the embedder
// calls each frame.  After the Phase-2/3 class-per-widget refactor,
// this file is reduced to PaintCtx construction and a single virtual
// `root.paint(...)` dispatch.  Every per-tag paint path lives in its
// own Widget subclass under `src/widgets/`.

#include <qtWasabi/TreePainter.h>
#include <qtWasabi/Layout.h>
#include <qtWasabi/Widget.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/FontRegistry.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/Host.h>

#include <QtGlobal>

class QPainter;

namespace qtWasabi::TreePainter {

using Layout::ResolvedWidget;

namespace {
// Paint the tree, then the queued window holders on top of it: child
// windows cover the skin's own pixels.
void paintRootThenHolders(QPainter *p, PaintCtx &ctx,
                          const ResolvedWidget &root, const QSize &canvas) {
    std::vector<std::function<void()>> holders;
    // WASABIQT_HOLDERS_IN_PLACE=1 paints holders in XML order instead
    // (debugging aid: shows what the skin itself lays over a holder).
    if (!qEnvironmentVariableIsSet("WASABIQT_HOLDERS_IN_PLACE"))
        ctx.deferredHolders = &holders;
    const_cast<Widget &>(root).paint(p, ctx, canvas);
    ctx.deferredHolders = nullptr;
    for (const auto &paintHolder : holders) paintHolder();
}
}  // namespace

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, const DisplayResolver &resolver) {
    PaintCtx ctx{&reg, &fontReg, resolver, nullptr};
    paintRootThenHolders(p, ctx, root, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host};
    paintRootThenHolders(p, ctx, root, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host,
               GammasetRegistry *gammasets,
               ColorRegistry *colors,
               int colorthemesSelectedRow,
               int colorthemesTopRowIn,
               QRect *colorthemesListBboxOut,
               int  *colorthemesTopRowOut,
               int  visMode, bool windowActive) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host,
                 gammasets, colors, colorthemesSelectedRow,
                 colorthemesTopRowIn,
                 colorthemesListBboxOut, colorthemesTopRowOut,
                 visMode};
    ctx.windowActive = windowActive;
    paintRootThenHolders(p, ctx, root, canvas);
}

}  // namespace qtWasabi::TreePainter
