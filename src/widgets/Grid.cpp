// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Grid.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/CfgAttribStore.h>
#include <qtWasabi/PaintCtx.h>

#include <QImage>
#include <QPainter>

namespace qtWasabi {

GridWidget::~GridWidget() {
    if (m_tabStateSubHandle)
        CfgAttribStore::instance().unsubscribe(m_tabStateSubHandle);
}

void GridWidget::onAttrsInitialized() {
    const QString key =
        attrs.value(QStringLiteral("_tab_state_key"));
    if (key.isEmpty()) return;
    bool ok = false;
    const int v =
        attrs.value(QStringLiteral("_tab_state_value")).toInt(&ok);
    if (!ok) return;
    const QString showWhen =
        attrs.value(QStringLiteral("_tab_show_when"));
    if (showWhen.isEmpty()) return;
    m_tabStateKey   = key;
    m_tabStateValue = v;
    m_tabShowWhen   = showWhen;
    if (m_tabStateSubHandle) {
        CfgAttribStore::instance().unsubscribe(m_tabStateSubHandle);
        m_tabStateSubHandle = 0;
    }
    auto apply = [this](int storeValue) {
        const bool isActive = (storeValue == m_tabStateValue);
        const bool want = (m_tabShowWhen == QStringLiteral("active"))
                              ? isActive : !isActive;
        const QString wantStr = want ? QStringLiteral("1")
                                      : QStringLiteral("0");
        if (attrs.value(QStringLiteral("visible")) != wantStr) {
            attrs.insert(QStringLiteral("visible"), wantStr);
            requestRepaint();
        }
    };
    auto &store = CfgAttribStore::instance();
    m_tabStateSubHandle = store.subscribe(m_tabStateKey, apply);
    apply(store.get(m_tabStateKey));
}

void GridWidget::paint(QPainter *p, PaintCtx &ctx, const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;
    auto img = [&](const char *a) {
        const QString n = attrs.value(QString::fromLatin1(a));
        return n.isEmpty() ? QImage() : ctx.bmp->imageFor(n);
    };

    // Full 9-slice when corner bitmaps are declared (the player display
    // background `infocomp.background.*`, window frames, etc.).  Corners
    // are fixed; the four edges tile along their run; the centre tiles
    // both ways to FILL the whole rect.  The horizontal-only left/middle/
    // right path below would paint just a one-row-tall strip, leaving the
    // rest of the display showing the gray window background behind it —
    // which is exactly the "gray instead of black display" bug.
    const QImage tl = img("topleft"), tmE = img("top"),    trI = img("topright");
    const QImage ml = img("left"),    mm  = img("middle"), mr  = img("right");
    const QImage bl = img("bottomleft"), bmE = img("bottom"), brI = img("bottomright");
    const bool nineSlice =
        !tl.isNull() || !trI.isNull() || !bl.isNull() || !brI.isNull();
    if (nineSlice) {
        auto tileH = [&](const QImage &im, int x0, int yy, int x1, int hh) {
            if (im.isNull() || hh <= 0 || x1 <= x0) return;
            const int iw = qMax(1, im.width());
            for (int x = x0; x < x1; x += iw) {
                const int c = qMin(iw, x1 - x);
                p->drawImage(QRect(x, yy, c, hh), im,
                             QRect(0, 0, c, qMin(hh, im.height())));
            }
        };
        auto tileV = [&](const QImage &im, int xx, int y0, int y1, int ww) {
            if (im.isNull() || ww <= 0 || y1 <= y0) return;
            const int ih = qMax(1, im.height());
            for (int y = y0; y < y1; y += ih) {
                const int c = qMin(ih, y1 - y);
                p->drawImage(QRect(xx, y, ww, c), im,
                             QRect(0, 0, qMin(ww, im.width()), c));
            }
        };
        auto tileBoth = [&](const QImage &im, const QRect &box) {
            if (im.isNull() || box.width() <= 0 || box.height() <= 0) return;
            const int iw = qMax(1, im.width()), ih = qMax(1, im.height());
            for (int y = box.top(); y < box.bottom(); y += ih)
                for (int x = box.left(); x < box.right(); x += iw)
                    p->drawImage(QRect(x, y, qMin(iw, box.right() - x),
                                       qMin(ih, box.bottom() - y)),
                                 im, QRect(0, 0, qMin(iw, box.right() - x),
                                           qMin(ih, box.bottom() - y)));
        };
        const int lw = !tl.isNull() ? tl.width()  : !ml.isNull() ? ml.width()  : !bl.isNull() ? bl.width()  : 0;
        const int rw = !trI.isNull()? trI.width() : !mr.isNull() ? mr.width()  : !brI.isNull()? brI.width() : 0;
        const int th = !tl.isNull() ? tl.height() : !tmE.isNull()? tmE.height(): !trI.isNull()? trI.height(): 0;
        const int bh = !bl.isNull() ? bl.height() : !bmE.isNull()? bmE.height(): !brI.isNull()? brI.height(): 0;
        const int x0 = r.x(), y0 = r.y(), x1 = r.right() + 1, y1 = r.bottom() + 1;
        tileBoth(mm, QRect(x0 + lw, y0 + th,
                           r.width() - lw - rw, r.height() - th - bh));
        tileH(tmE, x0 + lw, y0,      x1 - rw, th);
        tileH(bmE, x0 + lw, y1 - bh, x1 - rw, bh);
        tileV(ml,  x0,      y0 + th, y1 - bh, lw);
        tileV(mr,  x1 - rw, y0 + th, y1 - bh, rw);
        if (!tl.isNull())  p->drawImage(QRect(x0,      y0,      lw, th), tl);
        if (!trI.isNull()) p->drawImage(QRect(x1 - rw, y0,      rw, th), trI);
        if (!bl.isNull())  p->drawImage(QRect(x0,      y1 - bh, lw, bh), bl);
        if (!brI.isNull()) p->drawImage(QRect(x1 - rw, y1 - bh, rw, bh), brI);
        return;
    }

    const QString lN = attrs.value(QStringLiteral("left"));
    const QString mN = attrs.value(QStringLiteral("middle"));
    const QString rN = attrs.value(QStringLiteral("right"));
    QImage leftPm   = ctx.bmp->imageFor(lN);
    QImage middlePm = ctx.bmp->imageFor(mN);
    QImage rightPm  = ctx.bmp->imageFor(rN);
    if (qEnvironmentVariableIntValue("WASABIQT_TRACE_GRID") == 1) {
        fprintf(stderr, "[grid id=%s] L=%s(%s) M=%s(%s) R=%s(%s)\n",
                id.toLocal8Bit().constData(),
                lN.toLocal8Bit().constData(),
                leftPm.isNull() ? "NULL" : "ok",
                mN.toLocal8Bit().constData(),
                middlePm.isNull() ? "NULL" : "ok",
                rN.toLocal8Bit().constData(),
                rightPm.isNull() ? "NULL" : "ok");
    }
    const int leftW  = leftPm.isNull()  ? 0 : leftPm.width();
    const int rightW = rightPm.isNull() ? 0 : rightPm.width();
    const int rh = qMin(r.height(),
                        !middlePm.isNull() ? middlePm.height()
                        : !leftPm.isNull() ? leftPm.height()
                        : r.height());
    if (!leftPm.isNull())
        p->drawImage(QRect(r.x(), r.y(), qMin(leftW, r.width()), rh),
                     leftPm,
                     QRect(0, 0, qMin(leftW, r.width()), rh));
    if (!rightPm.isNull() && r.width() > leftW)
        p->drawImage(QRect(r.x() + r.width() - rightW, r.y(),
                            qMin(rightW, r.width()), rh),
                     rightPm,
                     QRect(0, 0, qMin(rightW, r.width()), rh));
    if (!middlePm.isNull() && r.width() > leftW + rightW) {
        int mx = r.x() + leftW;
        const int mEnd = r.x() + r.width() - rightW;
        const int mw = middlePm.width();
        while (mx < mEnd) {
            const int chunk = qMin(mw, mEnd - mx);
            p->drawImage(QRect(mx, r.y(), chunk, rh),
                         middlePm,
                         QRect(0, 0, chunk, rh));
            mx += chunk;
        }
    }
}

}  // namespace qtWasabi
