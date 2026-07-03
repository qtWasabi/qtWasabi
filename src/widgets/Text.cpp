// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Text.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/CfgAttribStore.h>
#include <qtWasabi/FontRegistry.h>
#include <qtWasabi/PaintCtx.h>
#include <qtWasabi/TextPainter.h>

#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <cstdio>

namespace qtWasabi {

TextWidget::~TextWidget() {
    if (m_stepperSubHandle)
        CfgAttribStore::instance().unsubscribe(m_stepperSubHandle);
    if (m_tabStateSubHandle)
        CfgAttribStore::instance().unsubscribe(m_tabStateSubHandle);
}

void TextWidget::onAttrsInitialized() {
    // Tab state subscription — `Layout::wireTabs` plants
    // `_tab_state_*` attrs on text widgets nested in `switch.X`
    // groupdef expansions that bear `*.normal` / `*.active` ids.
    // Mirrors GridWidget::onAttrsInitialized.
    {
        const QString tk =
            attrs.value(QStringLiteral("_tab_state_key"));
        if (!tk.isEmpty()) {
            bool tok = false;
            const int tv = attrs.value(
                QStringLiteral("_tab_state_value")).toInt(&tok);
            const QString showWhen =
                attrs.value(QStringLiteral("_tab_show_when"));
            if (tok && !showWhen.isEmpty()) {
                m_tabStateKey   = tk;
                m_tabStateValue = tv;
                m_tabShowWhen   = showWhen;
                if (m_tabStateSubHandle) {
                    CfgAttribStore::instance().unsubscribe(
                        m_tabStateSubHandle);
                    m_tabStateSubHandle = 0;
                }
                auto apply = [this](int storeValue) {
                    const bool isActive =
                        (storeValue == m_tabStateValue);
                    const bool want =
                        (m_tabShowWhen == QStringLiteral("active"))
                            ? isActive : !isActive;
                    const QString wantStr =
                        want ? QStringLiteral("1")
                              : QStringLiteral("0");
                    if (attrs.value(QStringLiteral("visible")) !=
                        wantStr) {
                        attrs.insert(QStringLiteral("visible"),
                                      wantStr);
                        requestRepaint();
                    }
                };
                auto &store = CfgAttribStore::instance();
                m_tabStateSubHandle =
                    store.subscribe(m_tabStateKey, apply);
                apply(store.get(m_tabStateKey));
            }
        }
    }

    // Auto-bind `*Display` text widgets to the sibling cfgattrib
    // slider's value.  Layout::wireSteppers tags us with
    // `_stepper_key` when the canonical pattern matched; we
    // subscribe to that key here so CfgAttribStore::set triggers a
    // repaint with the new value.  The `display` attr that
    // TextPainter resolves doesn't need to be touched — paint() now
    // checks for the stepper key first and renders the stored int
    // directly when present.
    if (!id.endsWith(QLatin1String("Display"),
                      Qt::CaseInsensitive)) return;
    m_stepperKey = attrs.value(QStringLiteral("_stepper_key"));
    if (m_stepperKey.isEmpty()) return;
    if (m_stepperSubHandle) {
        CfgAttribStore::instance().unsubscribe(m_stepperSubHandle);
        m_stepperSubHandle = 0;
    }
    m_stepperSubHandle = CfgAttribStore::instance().subscribe(
        m_stepperKey,
        [this](int) { requestRepaint(); });
}

QRect TextWidget::keepTickerOffTime(QRect r, const QSize &canvas) {
    if (!parentWidget) return r;
    for (const auto &c : parentWidget->children) {
        if (!c || c.get() == static_cast<Widget *>(this)) continue;
        if (c->attrs.value(QStringLiteral("display"))
                .compare(QStringLiteral("time"), Qt::CaseInsensitive) != 0)
            continue;
        const QRect tr = c->resolveRect(canvas);
        if (tr.isValid() && tr.right() >= r.left() && tr.left() <= r.right() &&
            tr.bottom() >= r.top() && tr.top() <= r.bottom())
            r.setLeft(qMax(r.left(), tr.right() + 2));
    }
    return r;
}

void TextWidget::paint(QPainter *p, PaintCtx &ctx,
                        const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    // Wasabi `alpha="0"` means "fully transparent" — semantically
    // the same as "don't paint."  Bento/Big Bento's `<Text
    // id="InfoDisplay" alpha="0" ghost="1">` is a static song-title
    // mirror that sits at the same x/y as the live `<SongTicker>`,
    // intentionally invisible so only the scroller is seen.  Without
    // this short-circuit it painted at full opacity on top of the
    // ticker, producing the "two overlapping titles, one still and
    // one moving" artifact in the player chrome.  Skin-agnostic —
    // every Wasabi text widget with alpha=0 means hide me.
    if (attrs.value(QStringLiteral("alpha")) == QStringLiteral("0"))
        return;
    // Window-focus dimming (Wasabi inactiveAlpha): a text widget that
    // declares `inactiveAlpha` renders at that alpha when its window is
    // unfocused — the titlebar title's 128 → half-dim, matching real
    // Winamp.  Applied as a painter-opacity scope so every return path
    // below restores it.  General (keys only off the standard attr).
    struct OpacityGuard {
        QPainter *p; qreal saved;
        ~OpacityGuard() { if (saved >= 0.0) p->setOpacity(saved); }
    } opacityGuard{p, -1.0};
    if (!ctx.windowActive) {
        bool ok = false;
        const int ia = attrs.value(QStringLiteral("inactivealpha")).toInt(&ok);
        if (ok && ia >= 0 && ia < 255) {
            opacityGuard.saved = p->opacity();
            p->setOpacity(opacityGuard.saved * (ia / 255.0));
        }
    }
    // The displayed string comes through the resolver as "songtitle"
    // / "songinfo" or whatever the embedder wires up.
    QHash<QString, QString> a = attrs;
    if (tag == QStringLiteral("songticker") &&
        a.value(QStringLiteral("display")).isEmpty()) {
        a.insert(QStringLiteral("display"),
                 QStringLiteral("songtitle"));
    }
    // Stepper-bound display widgets (id ends in "Display" and we
    // subscribed in onAttrsInitialized) render the current
    // CfgAttribStore value of their stepper key.  We synthesise a
    // unique `display=` key + register a one-shot resolver entry
    // via PaintCtx.  Simpler: just inject the literal integer as
    // a fake `text=` attr that TextPainter falls back to.
    if (!m_stepperKey.isEmpty()) {
        const int v = CfgAttribStore::instance().get(m_stepperKey);
        a.insert(QStringLiteral("display"), QString());
        a.insert(QStringLiteral("text"), QString::number(v));
    }
    // Ticker scrolling: <songticker> is implicitly a ticker, and
    // <text ticker="1"> opts into scrolling explicitly.  The text
    // scrolls leftward when the resolved string is wider than the
    // widget rect; wraps with a small gap; loops continuously.
    //
    // Two paths: bitmap fonts (Wasabi `<bitmapfont>` ids) measure via
    // FontDef.charWidth; TrueType fonts (`font="Tahoma"`,
    // `font="Arial"`, the empty default → "sans-serif") measure via
    // QFontMetrics with the same fontsize → Qt setPixelSize mapping
    // TextPainter uses.  Skin-agnostic — Bento's `Songticker` (no
    // bitmap font, color=color.display, fontsize=13, bold=2),
    // WACUP-stock's bitmap-font ticker, Big Bento's titlebar
    // ticker — all use the same code path with the appropriate
    // measurement.  Without this, the TrueType path fell straight
    // through to `TextPainter::paintText` which `QPainter::drawText`s
    // the full string without clipping; long titles overflowed
    // visibly into adjacent widgets (timer, KBPS/KHZ labels).
    const bool isTicker =
        (tag == QStringLiteral("songticker")) ||
        (a.value(QStringLiteral("ticker")) == QStringLiteral("1"));
    if (isTicker && ctx.font && ctx.bmp) {
        const QString fontId = a.value(QStringLiteral("font"));
        const auto *fd = ctx.font->find(fontId);
        // Resolve display string the same way TextPainter does so we
        // can measure it against the widget rect.
        QString tickText;
        const QString display = a.value(QStringLiteral("display"));
        if (ctx.resolver && !display.isEmpty())
            tickText = ctx.resolver(display);
        if (tickText.isEmpty() && ctx.resolver) {
            const QString textId = a.value(QStringLiteral("id"));
            if (!textId.isEmpty()) tickText = ctx.resolver(textId);
        }
        if (tickText.isEmpty())
            tickText = a.value(QStringLiteral("default"));
        if (tickText.isEmpty())
            tickText = a.value(QStringLiteral("text"));
        QRect r = keepTickerOffTime(resolveRect(canvas), canvas);

        // Measure tickText width and choose a wrap gap.  Bitmap
        // fonts use FontDef.charWidth + hSpacing (Wasabi's per-glyph
        // metrics); TrueType fonts use QFontMetrics with the same
        // Wasabi-fontsize-to-Qt-pixel mapping TextPainter uses
        // (6/7 ratio, override via WASABIQT_FONT_RATIO).
        int tickW = 0;
        int gap = 0;
        if (fd && fd->charWidth > 0) {
            tickW = tickText.size() * fd->charWidth +
                     qMax(0, tickText.size() - 1) * fd->hSpacing;
            gap = fd->charWidth * 4;
        } else if (!tickText.isEmpty()) {
            QFont qf(fontId.isEmpty() ? QStringLiteral("sans-serif")
                                         : fontId);
            const int fontsize =
                a.value(QStringLiteral("fontsize")).toInt() > 0
                    ? a.value(QStringLiteral("fontsize")).toInt()
                    : 12;
            int rn = 6, rd = 7;
            if (const char *rrt = ::getenv("WASABIQT_FONT_RATIO")) {
                int an = 0, ad = 0;
                if (sscanf(rrt, "%d,%d", &an, &ad) == 2 &&
                    an > 0 && ad > 0) {
                    rn = an; rd = ad;
                }
            }
            qf.setPixelSize(qMax(1, (fontsize * rn + rd/2) / rd));
            if (a.value(QStringLiteral("bold")).toInt() > 0)
                qf.setBold(true);
            if (a.value(QStringLiteral("italic")) == QStringLiteral("1"))
                qf.setItalic(true);
            const QFontMetrics fm(qf);
            tickW = fm.horizontalAdvance(tickText);
            gap = fm.horizontalAdvance(QStringLiteral("MMMM"));
        }

        if (tickW > 0 && r.width() > 0 && tickW > r.width()) {
            const int totalW = tickW + gap;
            const qint64 ms = QDateTime::currentMSecsSinceEpoch();
            auto attrInt = [&](const QString &k) {
                return a.value(k).toInt();
            };
            int speed = attrInt(QStringLiteral("tickspeed"));
            if (speed <= 0) speed = attrInt(QStringLiteral("speed"));
            if (speed <= 0)
                speed = attrInt(QStringLiteral("pixelsperframe"))
                        * 30;
            // Canonical Wasabi songticker pace.  Real Winamp's
            // `cfg_uioptions_textspeed` default works out to roughly
            // 20 px/s at the default `cfg_uioptions_textincrement`
            // (see gen_ff/wa2songticker.cpp's
            // `SONGTICKER_SCROLL_ONE_PIXEL_MS` formula).  Most skin
            // scripts (Bento's `songticker.maki`, WinampModernPP's
            // stock ticker) leave this implicit by never calling
            // `setSpeed`.  Override at runtime with WASABIQT_TICK_SPEED.
            if (speed <= 0) {
                speed = 20;
                if (const char *envSpeed = ::getenv("WASABIQT_TICK_SPEED")) {
                    const int n = QString::fromLatin1(envSpeed).toInt();
                    if (n > 0) speed = n;
                }
            }
            const int offset =
                int((ms * speed / 1000) % qint64(totalW));
            p->save();
            p->setClipRect(r);
            // Force left-align for the scroll passes so both text
            // copies start at the same x and translate predictably.
            QHash<QString, QString> aScroll = a;
            aScroll.insert(QStringLiteral("align"),
                            QStringLiteral("left"));
            // Widen the widget's `w` attr inside the scroll passes
            // so `TextPainter::paintText`'s internal drawText rect
            // is large enough to hold the WHOLE tick string without
            // Qt's per-rect clip truncating it.  Without this, Qt's
            // `drawText(QRect, flags, text)` clips to drawRect.width
            // (the widget's own declared width); the scrolling
            // translate just moves a one-rect-wide window over the
            // text and you never see characters outside that window
            // — the visible "only half the title ever rolls
            // through" symptom.  Our outer setClipRect(r) above
            // still bounds what actually paints, so the widening
            // affects only the truncation math, not the visible
            // pixels.
            aScroll.insert(QStringLiteral("w"),
                            QString::number(tickW + r.width() + 4));
            // The relat-w bias must come off too — otherwise the
            // resolver re-adds canvas.width to our literal w.
            aScroll.remove(QStringLiteral("relatw"));
            p->translate(-offset, 0);
            TextPainter::paintText(p, *ctx.font, *ctx.bmp, aScroll,
                                    canvas, ctx.resolver,
                                    ctx.colors, ctx.gammasets,
                                    /*clipToWidget=*/false);
            p->translate(totalW, 0);
            TextPainter::paintText(p, *ctx.font, *ctx.bmp, aScroll,
                                    canvas, ctx.resolver,
                                    ctx.colors, ctx.gammasets,
                                    /*clipToWidget=*/false);
            p->restore();
            return;
        }
    }
    // Non-ticker path (or ticker text that fits) — paint with the
    // widget rect clip enforced so even slightly-too-wide titles
    // don't bleed into siblings.  Without this clip, a
    // single-character overflow was silently smearing into the
    // adjacent timer/KBPS widgets in Bento's display group.  Tickers
    // additionally stay off a sibling time readout.
    QRect rClip = resolveRect(canvas);
    if (isTicker) rClip = keepTickerOffTime(rClip, canvas);
    p->save();
    p->setClipRect(rClip, Qt::IntersectClip);
    TextPainter::paintText(p, *ctx.font, *ctx.bmp, a, canvas,
                           ctx.resolver, ctx.colors, ctx.gammasets);
    p->restore();
}

}  // namespace qtWasabi
