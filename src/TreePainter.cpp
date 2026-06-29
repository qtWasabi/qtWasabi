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

class QPainter;

namespace qtWasabi::TreePainter {

using Layout::ResolvedWidget;

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, const DisplayResolver &resolver) {
    PaintCtx ctx{&reg, &fontReg, resolver, nullptr};
    const_cast<Widget &>(root).paint(p, ctx, canvas);
}

void paintTree(QPainter *p, const ResolvedWidget &root,
               BitmapRegistry &reg, FontRegistry &fontReg,
               const QSize &canvas, Host *host) {
    PaintCtx ctx{&reg, &fontReg, makeDefaultDisplayResolver(host), host};
    const_cast<Widget &>(root).paint(p, ctx, canvas);
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
    const_cast<Widget &>(root).paint(p, ctx, canvas);
}

}  // namespace qtWasabi::TreePainter
