// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Button.h"

#include <qtWasabi/BitmapRegistry.h>
#include <qtWasabi/CfgAttribStore.h>
#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/LayerPainter.h>
#include <qtWasabi/PaintCtx.h>

#include <QPainter>
#include <QStringList>

namespace qtWasabi {

QString ButtonWidget::currentImageAttr() const {
    // Canonical Wasabi precedence (buttwnd.cpp:353-356):
    //   down > hover > active > normal
    //
    // SkinXml lowercases every attr name at parse time, so the keys
    // are `downimage` / `hoverimage` / `activeimage` / `image`.  Real
    // Wasabi gates `active` behind ToggleButton/NStates; we apply it
    // to any button whose attrs declare activeImage AND that has a
    // sync key (cfgattrib or action) — see isToggleShaped() for the
    // rationale.
    if (m_pressed && attrs.contains(QStringLiteral("downimage")))
        return QStringLiteral("downimage");
    if (m_hover && attrs.contains(QStringLiteral("hoverimage")))
        return QStringLiteral("hoverimage");
    if (m_activated && attrs.contains(QStringLiteral("activeimage")))
        return QStringLiteral("activeimage");
    return QStringLiteral("image");
}

QString ButtonWidget::resolvedCfgKey() const {
    const QString cfg = attrs.value(QStringLiteral("cfgattrib"));
    if (!cfg.isEmpty()) return cfg;
    const QString action = attrs.value(QStringLiteral("action"));
    if (!action.isEmpty() &&
        attrs.contains(QStringLiteral("activeimage"))) {
        // Synthetic key for plain <button>s that lack a real cfgattrib
        // but have an action — so the EQ on/off button and its LED
        // (both action="EQ_TOGGLE", both activeImage=) sync.  Keeps the
        // sync purely engine-level: no per-action hard-coding.
        return QStringLiteral("__action:") + action.toUpper();
    }
    return QString();
}

bool ButtonWidget::isToggleShaped() const {
    // A widget toggles when it has a sync key — either an explicit
    // `cfgattrib` (author-declared binding to a config attribute) or
    // the action-based pseudo-key (which itself requires activeImage,
    // see resolvedCfgKey).  Don't require activeImage here: skins
    // routinely pair a cfgattrib-bound togglebutton WITHOUT
    // activeImage (the button's visual stays the same, the value
    // change drives a sibling LED widget that DOES have activeImage).
    // WinampModernPP's `button.vis.random` (cfgattrib=`Random`, no
    // activeImage) is the canonical example — clicking it lights up
    // `led.vis.random` (same cfgattrib, has activeImage).
    return !resolvedCfgKey().isEmpty();
}

void ButtonWidget::wireCfgAttrib(int initialValue) {
    m_cfgKey = resolvedCfgKey();
    if (m_cfgKey.isEmpty()) return;
    auto &store = CfgAttribStore::instance();
    const bool storeHad = store.has(m_cfgKey);
    if (storeHad) initialValue = store.get(m_cfgKey);
    // Subclasses interpret the int however they need (bool for
    // ButtonWidget, state index ↔ cfgvals for NStatesButton).  The
    // base class just keeps m_activated in sync.
    m_activated = (initialValue != 0);
    m_cfgSubHandle = store.subscribe(m_cfgKey,
        [this](int v) {
            const bool nv = (v != 0);
            if (m_activated != nv) {
                m_activated = nv;
                requestRepaint();
            }
        });
    if (!storeHad)
        store.set(m_cfgKey, m_activated ? 1 : 0);
}

ButtonWidget::~ButtonWidget() {
    if (m_cfgSubHandle)
        CfgAttribStore::instance().unsubscribe(m_cfgSubHandle);
    if (m_tabSubHandle)
        CfgAttribStore::instance().unsubscribe(m_tabSubHandle);
}

void ButtonWidget::onAttrsInitialized() {
    // autotoggle / cfgval govern the toggle behaviour (ToggleButton; a
    // plain Button leaves them at the defaults).  autotoggle=0 → the
    // engine doesn't self-flip on click; cfgval = the ON value written
    // to the bound config attribute.
    if (attrs.contains(QStringLiteral("autotoggle")))
        m_autoToggle =
            attrs.value(QStringLiteral("autotoggle")).toInt() != 0;
    if (attrs.contains(QStringLiteral("cfgval"))) {
        bool ok = false;
        const int v = attrs.value(QStringLiteral("cfgval")).toInt(&ok);
        if (ok) m_cfgVal = v;
    }
    // Seed activated from inline attr (rarely set) then maybe adopt
    // the shared value from the cfgattrib store.
    const QString init = attrs.value(QStringLiteral("activated"));
    const bool fromAttr = (init == QStringLiteral("1") ||
                            init.compare(QStringLiteral("true"),
                                          Qt::CaseInsensitive) == 0);
    if (isToggleShaped()) wireCfgAttrib(fromAttr ? 1 : 0);
    else                  m_activated = fromAttr;

    // Tab pattern — `Layout::wireTabs` tags `switch.X` tab buttons
    // (or the inner mousetrap button of a Bento:TabButton XUI
    // instance) with `_tab_key` + `_tab_value` so cycleOnRelease
    // writes the value to the store, and a value-equals subscription
    // mirrors `m_activated` for the active-tab LED.
    {
        const QString tabKey = attrs.value(QStringLiteral("_tab_key"));
        if (!tabKey.isEmpty()) {
            bool tabOk = false;
            const int tabVal = attrs.value(
                QStringLiteral("_tab_value")).toInt(&tabOk);
            if (tabOk) {
                m_tabKey   = tabKey;
                m_tabValue = tabVal;
                if (m_tabSubHandle) {
                    CfgAttribStore::instance().unsubscribe(m_tabSubHandle);
                    m_tabSubHandle = 0;
                }
                auto &store = CfgAttribStore::instance();
                m_tabSubHandle = store.subscribe(m_tabKey,
                    [this](int v) {
                        const bool wantActive = (v == m_tabValue);
                        if (m_activated != wantActive) {
                            m_activated = wantActive;
                            requestRepaint();
                        }
                    });
                m_activated = (store.get(m_tabKey) == m_tabValue);
            }
        }
    }

    // Stepper pattern — `Layout::wireSteppers` tags the relevant
    // buttons by injecting `_stepper_key` / `_stepper_low` /
    // `_stepper_high` / `_stepper_step` attrs at skin-load time.
    // If our id ends in `Decrease` / `Increase` (case-insensitive)
    // AND those attrs were planted, latch the metadata so
    // cycleOnRelease can inc/dec the cfgattrib value.  The +/-
    // delta is derived from the id suffix.  Idempotent — re-running
    // (after a stepper-wire pass) just re-latches the same data.
    const QString key = attrs.value(QStringLiteral("_stepper_key"));
    if (!key.isEmpty() && !id.isEmpty()) {
        bool ok = false;
        m_stepperLow = attrs.value(
            QStringLiteral("_stepper_low")).toInt(&ok);
        if (!ok) m_stepperLow = 0;
        m_stepperHigh = attrs.value(
            QStringLiteral("_stepper_high")).toInt(&ok);
        if (!ok) m_stepperHigh = 0;
        m_stepperStep = attrs.value(
            QStringLiteral("_stepper_step")).toInt(&ok);
        if (!ok || m_stepperStep <= 0) m_stepperStep = 1;
        m_stepperKey  = key;
        if (id.endsWith(QLatin1String("Decrease"),
                          Qt::CaseInsensitive)) {
            m_stepperDelta = -1;
        } else if (id.endsWith(QLatin1String("Increase"),
                                 Qt::CaseInsensitive)) {
            m_stepperDelta = +1;
        } else {
            m_stepperDelta = 0;
        }
    }
}

void ButtonWidget::setXmlParam(const QString &name,
                                const QString &value) {
    if (name.compare(QStringLiteral("activated"),
                     Qt::CaseInsensitive) == 0) {
        const bool newVal = (value == QStringLiteral("1") ||
                             value.compare(QStringLiteral("true"),
                                            Qt::CaseInsensitive) == 0);
        if (m_activated != newVal) {
            m_activated = newVal;
            requestRepaint();
            if (!m_cfgKey.isEmpty())
                CfgAttribStore::instance().set(
                    m_cfgKey, newVal ? m_cfgVal : 0);
        }
    }
    Widget::setXmlParam(name, value);
}

void ButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                          const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const QString slot = currentImageAttr();
    // Buttons paint at the bitmap's NATURAL size — never tile, never
    // stretch.  Real Wasabi's buttwnd.cpp draws a single sprite from
    // the bound image at the widget's top-left.  Tiling (which the
    // chrome paintLayer path does for `w > bitmap.w`) creates a
    // partial second copy of the icon when the button's declared
    // rect is wider than the bitmap (sysmenu @ w=20 / 15px icon →
    // "1.5 icons" artifact).
    if (slot == QStringLiteral("image")) {
        LayerPainter::paintLayerAtNaturalSize(p, *ctx.bmp, attrs, canvas, ctx.windowActive);
    } else {
        QHash<QString, QString> a = attrs;
        a.insert(QStringLiteral("image"), a.value(slot));
        LayerPainter::paintLayerAtNaturalSize(p, *ctx.bmp, a, canvas, ctx.windowActive);
    }

    // Text-label buttons (dialog / config skins) carry a `text=` caption
    // with no bitmap.  Draw it centred in the button rect using
    // `textcolor=` (literal r,g,b or a colour-registry key; defaults to
    // black).  Bitmap-only player buttons set neither, so this no-ops.
    const QString text = attrs.value(QStringLiteral("text"));
    if (!text.isEmpty()) {
        QColor col(0, 0, 0);
        const QString tc = attrs.value(QStringLiteral("textcolor"));
        if (tc.contains(QChar(','))) {
            const auto parts = tc.split(QChar(','));
            if (parts.size() == 3)
                col = QColor(parts[0].toInt(), parts[1].toInt(),
                             parts[2].toInt());
        } else if (!tc.isEmpty() && ctx.colors) {
            col = ctx.colors->resolve(tc, ctx.gammasets, col);
        }
        p->save();
        p->setPen(col);
        p->drawText(resolveRect(canvas), Qt::AlignCenter, text);
        p->restore();
    }
}

void ButtonWidget::cycleOnRelease() {
    // Tab buttons take priority: writing m_tabValue to m_tabKey
    // flips both the visibility of the wdh.X groupdefs (which
    // subscribe to the same key with value-equals semantics) and
    // the m_activated LED on every sibling switch.* button.
    if (!m_tabKey.isEmpty() && m_tabValue >= 0) {
        CfgAttribStore::instance().set(m_tabKey, m_tabValue);
        return;
    }
    // Stepper buttons take priority: a Decrease/Increase button
    // with `_stepper_key` planted by Layout::wireSteppers reads
    // the current cfgattrib value and writes
    // clamp(current ± step, low, high).  Engine-level dispatch
    // covers crossfade controls (and anything else following the
    // canonical Wasabi naming convention) without per-skin XML.
    if (m_stepperDelta != 0 && !m_stepperKey.isEmpty()) {
        auto &store = CfgAttribStore::instance();
        const int cur = store.get(m_stepperKey);
        int next = cur + m_stepperDelta * m_stepperStep;
        if (next < m_stepperLow)  next = m_stepperLow;
        if (next > m_stepperHigh) next = m_stepperHigh;
        if (next != cur) store.set(m_stepperKey, next);
        return;
    }
    // Toggle-shaped buttons flip activated on release.  Plain buttons
    // (no activeImage + no cfgattrib/action) skip — they just fire
    // their action via the existing Maki path and don't carry state.
    // autotoggle=0 buttons also skip the self-flip: a script owns the
    // state and would otherwise be double-toggled.
    if (isToggleShaped() && m_autoToggle) {
        setXmlParam(QStringLiteral("activated"),
            m_activated ? QStringLiteral("0") : QStringLiteral("1"));
    }
}

void ButtonWidget::onLeftButtonDown(QPoint, PaintCtx &) {
    if (!m_pressed) { m_pressed = true; requestRepaint(); }
}
void ButtonWidget::onLeftButtonUp(QPoint, PaintCtx &) {
    if (m_pressed) { m_pressed = false; requestRepaint(); }
    cycleOnRelease();
}
void ButtonWidget::onMouseMove(QPoint, PaintCtx &) {
    if (!m_hover) { m_hover = true; requestRepaint(); }
}
void ButtonWidget::onMouseLeave(PaintCtx &) {
    if (m_hover || m_pressed) {
        m_hover = false;
        m_pressed = false;
        requestRepaint();
    }
}

// ── NStatesButton ────────────────────────────────────────────────

int NStatesButtonWidget::stateToValue(int state) const {
    if (state < 0 || state >= m_cfgVals.size()) return state;
    return m_cfgVals[state].toInt();
}

int NStatesButtonWidget::valueToState(int value) const {
    for (int i = 0; i < m_cfgVals.size(); ++i)
        if (m_cfgVals[i].toInt() == value) return i;
    return 0;
}

void NStatesButtonWidget::onAttrsInitialized() {
    // Parse cfgvals first so the subscribe-callback closure can
    // translate values into state indices.  Empty cfgvals means
    // state IS the value.
    const QString raw = attrs.value(QStringLiteral("cfgvals"));
    if (!raw.isEmpty()) m_cfgVals = raw.split(QLatin1Char(';'),
                                              Qt::SkipEmptyParts);
    m_cfgKey = resolvedCfgKey();
    if (m_cfgKey.isEmpty()) return;
    auto &store = CfgAttribStore::instance();
    const bool storeHad = store.has(m_cfgKey);
    if (storeHad) m_state = valueToState(store.get(m_cfgKey));
    // Subscribe with NStates-specific value→state translation,
    // overriding the base class's "bool activated" interpretation.
    m_cfgSubHandle = store.subscribe(m_cfgKey,
        [this](int v) {
            const int ns = valueToState(v);
            if (m_state != ns) {
                m_state = ns;
                requestRepaint();
            }
        });
    if (!storeHad) store.set(m_cfgKey, stateToValue(m_state));
}

void NStatesButtonWidget::cycleOnRelease() {
    const int n = attrs.value(QStringLiteral("nstates")).toInt();
    if (n <= 0) return;
    m_state = (m_state + 1) % n;
    requestRepaint();
    if (!m_cfgKey.isEmpty())
        CfgAttribStore::instance().set(m_cfgKey, stateToValue(m_state));
}

void NStatesButtonWidget::paint(QPainter *p, PaintCtx &ctx,
                                 const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    // Canonical Wasabi NStatesTgButton::setupBitmaps
    // (tgbutton.cpp:262-269):
    //
    //   if (nstates <= 1 || onevstate /* autoelements="0" */)
    //     setBitmaps(image, down, hover, active);    // bare attrs
    //   else
    //     setBitmaps(image+state, down+state, hover+state /*, active */);
    //
    // The two cases are visually distinct:
    //   • Default (autoelements=1) — every state has its own bitmap;
    //     `image` is the BASE name and the engine appends the state
    //     index ("player.button.repeat" → "player.button.repeat0/1/2").
    //     The `active` slot is unused (state encodes the visual).
    //   • autoelements=0 — same bitmaps regardless of state, but the
    //     LED-style skin author wants state != 0 to look "active".
    //     We delegate to the toggle precedence (down > hover > active
    //     > normal) with activated derived from `m_state != 0`
    //     (matches getActivatedButton override at tgbutton.cpp:247).
    const bool autoElements =
        attrs.value(QStringLiteral("autoelements"),
                    QStringLiteral("1")) != QStringLiteral("0");
    QHash<QString, QString> a = attrs;
    QString img;
    if (autoElements) {
        // Pick down/hover/normal slot, then append state suffix.
        QString slot = QStringLiteral("image");
        if (m_pressed && a.contains(QStringLiteral("downimage")))
            slot = QStringLiteral("downimage");
        else if (m_hover && a.contains(QStringLiteral("hoverimage")))
            slot = QStringLiteral("hoverimage");
        const QString base = a.value(slot);
        if (!base.isEmpty()) {
            const QString suffixed = base + QString::number(m_state);
            // Prefer the suffixed id; fall back to the bare id when
            // the skin only ships one form for this slot.
            img = ctx.bmp->find(suffixed) ? suffixed : base;
        }
    } else {
        const bool activated = (m_state != 0);
        QString slot = QStringLiteral("image");
        if (m_pressed && a.contains(QStringLiteral("downimage")))
            slot = QStringLiteral("downimage");
        else if (m_hover && a.contains(QStringLiteral("hoverimage")))
            slot = QStringLiteral("hoverimage");
        else if (activated && a.contains(QStringLiteral("activeimage")))
            slot = QStringLiteral("activeimage");
        img = a.value(slot);
    }
    a.insert(QStringLiteral("image"), img);
    LayerPainter::paintLayer(p, *ctx.bmp, a, canvas, ctx.windowActive);
}

}  // namespace qtWasabi
