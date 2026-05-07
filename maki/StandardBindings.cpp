#include "StandardBindings.h"
#include "Vcpu.h"
#include <wasabiq/IWasabiHost.h>
#include <wasabiq/SkinRuntime.h>
#include <wasabiq/SkinEngineRoot.h>
#include <wasabiq/WasabiLayout.h>

#include <QString>
#include <QtMath>
#include <QDateTime>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <functional>

namespace wasabiq::maki {

namespace {

// Helper: pull the SystemObject* out of `self`.
SystemObject *asSystem(IObject *o) {
    return o ? dynamic_cast<SystemObject*>(o) : nullptr;
}

RuntimeWidget *asWidget(IObject *o) {
    return o ? dynamic_cast<RuntimeWidget*>(o) : nullptr;
}

#define REG_SYS(name, params, body) \
    b.registerMethod(kSystemObjectGuid(), QStringLiteral(name), (params), \
        [](IObject *self, const QVector<Slot> &args) -> Slot { \
            SystemObject *s = asSystem(self); (void)args; \
            if (!s || !s->host) return {}; \
            wasabiq::IWasabiHost *h = s->host; (void)h; \
            body \
        });

#define REG_GUI(guid, name, params, body) \
    b.registerMethod(guid, QStringLiteral(name), (params), \
        [](IObject *self, const QVector<Slot> &args) -> Slot { \
            RuntimeWidget *w = asWidget(self); (void)args; \
            if (!w || !w->widget || !w->runtime) return {}; \
            body \
        });

void registerSystemObject(Bindings &b) {
    // Lifecycle / events — these are *event* names that scripts
    // register handlers for, not callable methods.  We don't expose
    // them as callables but DO let the host fire them by name via
    // VM::dispatchEventByName.

    REG_SYS("getStatus",       0, return Slot::makeInt(h->playbackState());)
    REG_SYS("getPosition",     0, return Slot::makeInt(h->position());)
    REG_SYS("getPlayItemLength", 0, return Slot::makeInt(h->duration() / 1000);)
    REG_SYS("seekTo",          1,
        h->setPosition(args.value(0).asInt());
        return Slot::makeInt(0);)
    REG_SYS("play",            0, h->play();  return Slot::makeInt(0);)
    REG_SYS("pause",           0, h->pause(); return Slot::makeInt(0);)
    REG_SYS("stop",            0, h->stop();  return Slot::makeInt(0);)
    REG_SYS("next",            0, h->next();  return Slot::makeInt(0);)
    REG_SYS("previous",        0, h->prev();  return Slot::makeInt(0);)
    REG_SYS("getVolume",       0, return Slot::makeInt(h->volume());)
    REG_SYS("setVolume",       1,
        h->setVolume(args.value(0).asInt());
        return Slot::makeInt(0);)
    REG_SYS("getEq",           0, return Slot::makeInt(h->eqEnabled() ? 1 : 0);)
    REG_SYS("setEq",           1,
        bool want = args.value(0).asBool();
        if (h->eqEnabled() != want) h->toggleEq();
        return Slot::makeInt(0);)
    REG_SYS("getEqBand",       1,
        return Slot::makeInt(h->eqBand(args.value(0).asInt()));)
    REG_SYS("setEqBand",       2,
        h->setEqBand(args.value(0).asInt(), args.value(1).asInt());
        return Slot::makeInt(0);)
    REG_SYS("getEqPreAmp",     0, return Slot::makeInt(h->eqPreamp());)
    REG_SYS("setEqPreAmp",     1,
        h->setEqPreamp(args.value(0).asInt());
        return Slot::makeInt(0);)
    REG_SYS("getCurrentTrackTitle", 0, return Slot::makeString(h->songTitle());)
    REG_SYS("getPlayItemDisplayTitle", 0, return Slot::makeString(h->songTitle());)
    REG_SYS("getCurrentTrackArtist",   0, return Slot::makeString(h->songArtist());)
    REG_SYS("getCurrentTrackAlbum",    0, return Slot::makeString(h->songAlbum());)
    REG_SYS("getPlayItemString",       0, return Slot::makeString(h->currentFile());)
    REG_SYS("getSongInfoText",         0, return Slot::makeString(h->songTitle());)
    REG_SYS("getCurrentTrackInfoText", 0, return Slot::makeString(h->songTitle());)
    REG_SYS("getRating",         0, return Slot::makeInt(0);)
    REG_SYS("setRating",         1, return Slot::makeInt(0);)
    REG_SYS("getCurrentTrackRating", 0, return Slot::makeInt(0);)
    REG_SYS("setCurrentTrackRating", 1, return Slot::makeInt(0);)
    REG_SYS("getPlayItemMetaDataString", 1, return Slot::makeString({});)
    REG_SYS("getPlaylistIndex",  0, return Slot::makeInt(0);)
    REG_SYS("getPlaylistLength", 0, return Slot::makeInt(0);)
    REG_SYS("isKeyDown",         1, return Slot::makeInt(0);)

    // Math / utility — light implementations so scripts don't crash
    // on common helpers.
    REG_SYS("integer", 1, return Slot::makeInt(qint64(args.value(0).asDouble()));)
    REG_SYS("frac",    1, double v = args.value(0).asDouble();
                          return Slot::makeDouble(v - qint64(v));)
    REG_SYS("integerToString", 1, return Slot::makeString(QString::number(args.value(0).asInt()));)
    REG_SYS("stringToInteger", 1, return Slot::makeInt(args.value(0).s.toLongLong());)
    REG_SYS("getViewportWidth",  0, return Slot::makeInt(0);)
    REG_SYS("getViewportHeight", 0, return Slot::makeInt(0);)
    REG_SYS("getMousePosX",      0, return Slot::makeInt(0);)
    REG_SYS("getMousePosY",      0, return Slot::makeInt(0);)
    REG_SYS("isAppActive",       0, return Slot::makeInt(1);)
    REG_SYS("isObjectValid",     1, return Slot::makeInt(args.value(0).o ? 1 : 0);)
    REG_SYS("debugString",       2, return Slot::makeInt(0);)
    REG_SYS("getSkinName",       0, return Slot::makeString(h->skinName());)
    // std.mi declares this as `Double` and versionCheck() does
    // `if (v < VCPU_VERSION || v > 65535)` — so we must return a
    // Double in [VCPU_VERSION, 65535].  VCPU_VERSION is 2 in std.mi.
    REG_SYS("getRuntimeVersion", 0, return Slot::makeDouble(5.0);)
    REG_SYS("getScriptGroup",    0,
        // Per-script enclosing-group lookup: each script lives inside
        // a specific groupdef.  Real Wasabi resolves getScriptGroup()
        // to the *instance* of that groupdef placed in the layout —
        // its declared x/w on the instance is what scripts measure
        // via sg.getLeft()/sg.getWidth().  Without per-script
        // resolution, every script saw a synthetic root pseudo-group
        // (x=0, w=layout.w), and titlebar.m's
        // `lx -= sg.getLeft()` did nothing — placing the title 10 px
        // too far left in the player layout (where the wasabi.titlebar
        // instance is at x=10).
        if (s->vm) {
            int sid = s->vm->currentScriptId();
            if (sid >= 0 && sid < s->scriptGroups.size()) {
                auto *rw = s->scriptGroups[sid];
                if (rw) return Slot::makeObject(rw);
            }
        }
        return Slot::makeObject(s->globalGroup);)
    REG_SYS("isTransparencyAvailable", 0, return Slot::makeInt(1);)
    REG_SYS("isDesktopAlphaAvailable", 0, return Slot::makeInt(1);)
    REG_SYS("getNumContainers",  0, return Slot::makeInt(1);)
    REG_SYS("enumContainer",     1, return Slot::makeObject(s);)
    REG_SYS("getContainer",      1, return Slot::makeObject(s);)
    REG_SYS("newDynamicContainer", 1, return Slot::makeObject(s);)
    REG_SYS("onEqFreqChanged",   1, return Slot::makeInt(0);)
    REG_SYS("onEqBandChanged",   2, return Slot::makeInt(0);)
    REG_SYS("onEqPreampChanged", 1, return Slot::makeInt(0);)
    REG_SYS("onSetEq",           1, return Slot::makeInt(0);)
    REG_SYS("onSetEqAuto",       1, return Slot::makeInt(0);)
    REG_SYS("hasVideoSupport",   0, return Slot::makeInt(0);)
    REG_SYS("isVideo",           0, return Slot::makeInt(0);)
    REG_SYS("isAudioStream",     0, return Slot::makeInt(0);)
    REG_SYS("isAudio",           0, return Slot::makeInt(1);)
    REG_SYS("isMinimized",       0, return Slot::makeInt(0);)
    REG_SYS("activateApplication",  0, return Slot::makeInt(0);)
    REG_SYS("minimizeApplication",  0, return Slot::makeInt(0);)
    REG_SYS("restoreApplication",   0, return Slot::makeInt(0);)
    REG_SYS("setAtom",           2, return Slot::makeInt(0);)
    REG_SYS("getAtom",           1, return Slot::makeString({});)
    // Private/public config storage — backed by an in-process
    // map.  Scripts use these to remember initialisation state
    // across invocations; if these are no-ops, scripts often loop
    // or skip their setup entirely.
    REG_SYS("setPrivateString",  3,
        QString k = args.value(0).s + "/" + args.value(1).s;
        h->setConfig(QStringLiteral("private"), k, args.value(2).s);
        return Slot::makeInt(0);)
    REG_SYS("getPrivateString",  3,
        QString k = args.value(0).s + "/" + args.value(1).s;
        QString def = args.value(2).s;
        QVariant v = h->getConfig(QStringLiteral("private"), k);
        return Slot::makeString(v.isValid() ? v.toString() : def);)
    REG_SYS("setPrivateInt",     3,
        QString k = args.value(0).s + "/" + args.value(1).s;
        h->setConfig(QStringLiteral("private"), k, args.value(2).asInt());
        return Slot::makeInt(0);)
    REG_SYS("getPrivateInt",     3,
        QString k = args.value(0).s + "/" + args.value(1).s;
        qint64 def = args.value(2).asInt();
        QVariant v = h->getConfig(QStringLiteral("private"), k);
        return Slot::makeInt(v.isValid() ? v.toLongLong() : def);)
    REG_SYS("setPublicString",   2,
        h->setConfig(QStringLiteral("public"), args.value(0).s, args.value(1).s);
        return Slot::makeInt(0);)
    REG_SYS("getPublicString",   1,
        QVariant v = h->getConfig(QStringLiteral("public"), args.value(0).s);
        return Slot::makeString(v.isValid() ? v.toString() : QString());)
    REG_SYS("setPublicInt",      2,
        h->setConfig(QStringLiteral("public"), args.value(0).s, args.value(1).asInt());
        return Slot::makeInt(0);)
    REG_SYS("getPublicInt",      1,
        QVariant v = h->getConfig(QStringLiteral("public"), args.value(0).s);
        return Slot::makeInt(v.isValid() ? v.toLongLong() : 0);)
    REG_SYS("getViewportLeft",   0, return Slot::makeInt(0);)
    REG_SYS("getViewportTop",    0, return Slot::makeInt(0);)
    REG_SYS("getCurAppLeft",     0, return Slot::makeInt(0);)
    REG_SYS("getCurAppTop",      0, return Slot::makeInt(0);)
    REG_SYS("getCurAppWidth",    0, return Slot::makeInt(0);)
    REG_SYS("getCurAppHeight",   0, return Slot::makeInt(0);)
    REG_SYS("getMonitorWidth",   0, return Slot::makeInt(0);)
    REG_SYS("getMonitorHeight",  0, return Slot::makeInt(0);)
    REG_SYS("getMonitorLeft",    0, return Slot::makeInt(0);)
    REG_SYS("getMonitorTop",     0, return Slot::makeInt(0);)
    REG_SYS("getDate",           0,
        return Slot::makeInt(QDateTime::currentSecsSinceEpoch());)
    REG_SYS("getTimeOfDay",      0,
        // Wasabi returns ms-since-midnight here.
        QDateTime now = QDateTime::currentDateTime();
        QDateTime midnight(now.date(), QTime(0, 0));
        return Slot::makeInt(midnight.msecsTo(now));)
    REG_SYS("getCh",             1, return Slot::makeInt(0);)
    REG_SYS("isAppActive",       0, return Slot::makeInt(1);)
    REG_SYS("formatDate",        1, return Slot::makeString({});)
    REG_SYS("formatLongDate",    1, return Slot::makeString({});)
    REG_SYS("getString",         2, return Slot::makeString(args.value(1).s);)
    REG_SYS("translate",         1, return Slot::makeString(args.value(0).s);)
    REG_SYS("getLanguageId",     0, return Slot::makeInt(0);)
    REG_SYS("getParam",          0,
        // Returns the `<script param="…">` attribute string of the
        // *currently executing* script — needed by every Wasabi
        // script that uses getToken(getParam(),…) to bind widgets.
        if (s->vm) return Slot::makeString(s->vm->scriptParam(s->vm->currentScriptId()));
        return Slot::makeString({});)
    REG_SYS("isLoadingSkin",     0, return Slot::makeInt(0);)
    REG_SYS("Chr",               1, return Slot::makeString(QString(QChar(static_cast<char16_t>(args.value(0).asInt()))));)
    REG_SYS("StrLen",            1, return Slot::makeInt(args.value(0).s.size());)
    REG_SYS("StrLeft",           2, return Slot::makeString(args.value(0).s.left(args.value(1).asInt()));)
    REG_SYS("StrRight",          2, return Slot::makeString(args.value(0).s.right(args.value(1).asInt()));)
    REG_SYS("StrMid",            3, return Slot::makeString(args.value(0).s.mid(args.value(1).asInt(), args.value(2).asInt()));)
    REG_SYS("StrSearch",         2, return Slot::makeInt(args.value(0).s.indexOf(args.value(1).s));)
    REG_SYS("StrUpper",          1, return Slot::makeString(args.value(0).s.toUpper());)
    REG_SYS("StrLower",          1, return Slot::makeString(args.value(0).s.toLower());)
    REG_SYS("UrlEncode",         1, return Slot::makeString(args.value(0).s);)
    REG_SYS("UrlDecode",         1, return Slot::makeString(args.value(0).s);)
    REG_SYS("RemovePath",        1, return Slot::makeString(args.value(0).s.section('/', -1));)
    REG_SYS("GetPath",           1, return Slot::makeString(args.value(0).s.section('/', 0, -2));)
    REG_SYS("GetExtension",      1, return Slot::makeString(args.value(0).s.section('.', -1));)
    REG_SYS("getToken",          3,
        // getToken(string, separator, n) — return the n-th token.
        // Wasabi semantics: empty fields count, so we use a plain
        // split (Qt::KeepEmptyParts) and clamp out-of-range to "".
        QString src = args.value(0).s;
        QString sep = args.value(1).s;
        int n = args.value(2).asInt();
        if (sep.isEmpty()) return Slot::makeString(n == 0 ? src : QString());
        QStringList parts = src.split(sep, Qt::KeepEmptyParts);
        if (n < 0 || n >= parts.size()) return Slot::makeString({});
        return Slot::makeString(parts.at(n));)
    REG_SYS("sin",               1, return Slot::makeDouble(qSin(args.value(0).asDouble()));)
    REG_SYS("cos",               1, return Slot::makeDouble(qCos(args.value(0).asDouble()));)
    REG_SYS("tan",               1, return Slot::makeDouble(qTan(args.value(0).asDouble()));)
    REG_SYS("sqrt",              1, return Slot::makeDouble(qSqrt(args.value(0).asDouble()));)
    REG_SYS("pow",               2, return Slot::makeDouble(qPow(args.value(0).asDouble(), args.value(1).asDouble()));)
    REG_SYS("random",            1, return Slot::makeInt(0);)
    REG_SYS("floatToString",     2, return Slot::makeString(QString::number(args.value(0).asDouble()));)
    REG_SYS("stringToFloat",     1, return Slot::makeDouble(args.value(0).s.toDouble());)
    REG_SYS("messageBox",        4, return Slot::makeInt(0);)

    // No-op stubs for things scripts call but we don't yet support.
    REG_SYS("triggerAction",     3, return Slot::makeInt(0);)
    REG_SYS("lockUI",            0, return Slot::makeInt(0);)
    REG_SYS("unlockUI",          0, return Slot::makeInt(0);)
}

void registerGuiObjectMethods(Bindings &b, const QUuid &guid) {
    REG_GUI(guid, "setVisible", 1,
        bool v = args.value(0).asBool();
        WidgetOverride &ov = w->runtime->mutate(w->widget->id);
        ov.visible = v ? WidgetOverride::OnState : WidgetOverride::OffState;
        return Slot::makeInt(0);)
    REG_GUI(guid, "getVisible", 0,
        const WidgetOverride *ov = w->runtime->peek(w->widget->id);
        return Slot::makeInt(SkinRuntime::effectiveVisible(*w->widget, ov) ? 1 : 0);)
    REG_GUI(guid, "show", 0,
        w->runtime->mutate(w->widget->id).visible = WidgetOverride::OnState;
        return Slot::makeInt(0);)
    REG_GUI(guid, "hide", 0,
        w->runtime->mutate(w->widget->id).visible = WidgetOverride::OffState;
        return Slot::makeInt(0);)
    REG_GUI(guid, "setAlpha", 1,
        w->runtime->mutate(w->widget->id).alpha = args.value(0).asInt();
        return Slot::makeInt(0);)
    REG_GUI(guid, "getAlpha", 0,
        const WidgetOverride *ov = w->runtime->peek(w->widget->id);
        return Slot::makeInt(SkinRuntime::effectiveAlpha(*w->widget, ov));)
    REG_GUI(guid, "setXmlParam", 2,
        QString name = args.value(0).s.toLower();
        QString val  = args.value(1).s;
        if (qEnvironmentVariableIsSet("WASABIQ_TRACE_SETXP")) {
            fprintf(stderr, "[setXmlParam] id='%s' %s='%s'\n",
                    qPrintable(w->widget->id), qPrintable(name), qPrintable(val));
        }
        WidgetOverride &ov = w->runtime->mutate(w->widget->id);
        if (name == "image")        ov.imageOverride = val;
        else if (name == "text")    ov.textOverride  = val;
        ov.xmlParams.insert(name, val);
        return Slot::makeInt(0);)
    REG_GUI(guid, "getXmlParam", 1,
        QString name = args.value(0).s.toLower();
        const WidgetOverride *ov = w->runtime->peek(w->widget->id);
        if (ov && ov->xmlParams.contains(name))
            return Slot::makeString(ov->xmlParams.value(name));
        if (name == "image") return Slot::makeString(w->widget->image);
        if (name == "id")    return Slot::makeString(w->widget->id);
        return Slot::makeString({});)
    REG_GUI(guid, "getId", 0, return Slot::makeString(w->widget->id);)
    REG_GUI(guid, "getName", 0, return Slot::makeString(w->widget->id);)
    REG_GUI(guid, "isVisible", 0,
        const WidgetOverride *ov = w->runtime->peek(w->widget->id);
        return Slot::makeInt(SkinRuntime::effectiveVisible(*w->widget, ov) ? 1 : 0);)
    REG_GUI(guid, "bringToFront", 0, return Slot::makeInt(0);)
    REG_GUI(guid, "bringToBack",  0, return Slot::makeInt(0);)
    REG_GUI(guid, "getWidth",     0,
        // <groupdef autowidthsource="X"/> binds the group's effective
        // width to a child widget's content width — used by the
        // config-drawer tabs to size each tab to its label.
        // Without this, scripts that lay tabs out via
        // `tEQon.getWidth()` see 0 and stack everything at x=0.
        if (!w->widget->autoWidthSource.isEmpty()) {
            // Locate the named child (any depth).
            std::function<const Wasabi::Widget*(const Wasabi::Widget&, const QString&)> findChild;
            findChild = [&](const Wasabi::Widget &g, const QString &id) -> const Wasabi::Widget* {
                for (const auto &c : g.children) {
                    if (c.id == id) return &c;
                    if (auto *r = findChild(c, id)) return r;
                }
                return nullptr;
            };
            const Wasabi::Widget *src = findChild(*w->widget, w->widget->autoWidthSource);
            if (src && src->type == Wasabi::WidgetType::Text) {
                QString s = src->text;
                if (s.isEmpty()) s = src->defaultText;
                if (!s.isEmpty()) {
                    QFont f;
                    if (!src->font.isEmpty()) f.setFamily(src->font);
                    if (src->fontSize > 0)
                        f.setPixelSize(qMax(1, (src->fontSize * 5 + 3) / 7));
                    if (src->bold) f.setBold(true);
                    QFontMetrics fm(f);
                    int textW = fm.horizontalAdvance(s);
                    // Source widget's own pos says how much padding
                    // wraps the text.  For w="-N" relatw=1 the widget
                    // expands to fill (parent_w + (-N)), reserving N
                    // pixels of total padding around the text.
                    int padding = src->pos.relatw ? -src->pos.w : 0;
                    return Slot::makeInt(textW + padding);
                }
            }
        }
        return Slot::makeInt(w->widget->pos.w);)
    REG_GUI(guid, "getHeight",    0, return Slot::makeInt(w->widget->pos.h);)
    REG_GUI(guid, "getLeft",      0, return Slot::makeInt(w->widget->pos.x);)
    REG_GUI(guid, "getTop",       0, return Slot::makeInt(w->widget->pos.y);)
    REG_GUI(guid, "getTopParent", 0, return Slot::makeObject(w);)
    REG_GUI(guid, "getParentLayout", 0,
        // Return the synthetic layout-pseudo wrapper (carries the
        // main layout's full w/h) rather than the calling widget
        // itself.  titlebar.m's resizeObjects needs
        // `l.getWidth() == 354`, not sg's narrower bounds.
        if (w->root && w->root->layoutWrapper)
            return Slot::makeObject(w->root->layoutWrapper);
        return Slot::makeObject(w);)
    REG_GUI(guid, "getParentGroup",  0, return Slot::makeObject(w);)
    REG_GUI(guid, "getParent",       0, return Slot::makeObject(w);)
    // Resolved geometry queries — return the widget's XML position.
    // The renderer applies relative-anchor math at paint time; for
    // scripts these are just the declared values.
    REG_GUI(guid, "getGuiX",         0, return Slot::makeInt(w->widget->pos.x);)
    REG_GUI(guid, "getGuiY",         0, return Slot::makeInt(w->widget->pos.y);)
    REG_GUI(guid, "getGuiW",         0, return Slot::makeInt(w->widget->pos.w);)
    REG_GUI(guid, "getGuiH",         0, return Slot::makeInt(w->widget->pos.h);)
    REG_GUI(guid, "getGuiRelatX",    0, return Slot::makeInt(w->widget->pos.relatx ? 1 : 0);)
    REG_GUI(guid, "getGuiRelatY",    0, return Slot::makeInt(w->widget->pos.relaty ? 1 : 0);)
    REG_GUI(guid, "getGuiRelatW",    0, return Slot::makeInt(w->widget->pos.relatw ? 1 : 0);)
    REG_GUI(guid, "getGuiRelatH",    0, return Slot::makeInt(w->widget->pos.relath ? 1 : 0);)
    REG_GUI(guid, "getAutoWidth",    0,
        // Mirror Wasabi's Text::getPreferences(SUGGESTED_W) — found
        // in Src/Wasabi/api/skin/widgets/text.cpp line 421-427:
        //   w += canvas.getTextWidth(...) + 4;       // per segment
        //   return min_w + lpadding + rpadding;
        // The "+ 4" per text segment plus the widget's lpadding /
        // rpadding determine the auto-width.  Without these, the
        // script's centring math (lx = (layout_w - text_w) / 2)
        // gets a smaller text_w than reality, putting the right
        // streak too close to the title text.
        const Wasabi::Widget *ww = w->widget;
        if (ww->type == Wasabi::WidgetType::Text) {
            QString s = ww->text;
            if (s.isEmpty()) s = ww->defaultText;
            const WidgetOverride *ov = w->runtime->peek(ww->id);
            if (ov && !ov->textOverride.isEmpty()) s = ov->textOverride;
            if (!s.isEmpty()) {
                QFont f;
                if (!ww->font.isEmpty()) f.setFamily(ww->font);
                if (ww->fontSize > 0) {
                    f.setPixelSize(qMax(1, (ww->fontSize * 5 + 3) / 7));
                }
                if (ww->bold)            f.setBold(true);
                QFontMetrics fm(f);
                int textW = fm.horizontalAdvance(s);
                // Wasabi's Text::getPreferences(SUGGESTED_W) adds 4
                // per segment plus lpadding+rpadding (text.cpp:421-427).
                // Plus an empirical offset for the Win32 GDI vs Qt
                // QFontMetrics width difference: GDI's getTextWidth
                // for Arial Bold lfHeight=-N is ~7 px wider than
                // Qt's QFontMetrics::horizontalAdvance at the
                // matching setPixelSize.  Without this, the script
                // reads a smaller text_w than reference Wasabi
                // would, and the right streak ends up overlapping
                // the title text instead of the reference's ~5 px
                // gap.  Total per-segment: 11 px.
                return Slot::makeInt(textW + 11);
            }
        }
        return Slot::makeInt(ww->pos.w);)
    REG_GUI(guid, "getAutoHeight",   0, return Slot::makeInt(w->widget->pos.h);)
    REG_GUI(guid, "getTextWidth",    0, return Slot::makeInt(w->widget->pos.w);)
    REG_GUI(guid, "isActive",        0, return Slot::makeInt(1);)
    // Coordinate conversion helpers — titlebar.m chains a
    // client→screen→client round-trip to convert between a layout
    // and a sibling group's local coords.  We render everything in
    // a single coord space, so these are identity functions.
    REG_GUI(guid, "clientToScreenX", 1, return Slot::makeInt(args.value(0).asInt());)
    REG_GUI(guid, "clientToScreenY", 1, return Slot::makeInt(args.value(0).asInt());)
    REG_GUI(guid, "screenToClientX", 1, return Slot::makeInt(args.value(0).asInt());)
    REG_GUI(guid, "screenToClientY", 1, return Slot::makeInt(args.value(0).asInt());)
    REG_GUI(guid, "isMouseOverRect", 0, return Slot::makeInt(0);)
    REG_GUI(guid, "setFocus",        0, return Slot::makeInt(0);)
    REG_GUI(guid, "bringAbove",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "bringBelow",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "setEnabled",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "isEnabled",       0, return Slot::makeInt(1);)
    REG_GUI(guid, "setTargetX",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "setTargetY",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "setTargetW",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "setTargetH",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "setTargetSpeed",  1, return Slot::makeInt(0);)
    REG_GUI(guid, "setTargetA",      1, return Slot::makeInt(0);)
    REG_GUI(guid, "gotoTarget",      0, return Slot::makeInt(0);)
    REG_GUI(guid, "cancelTarget",    0, return Slot::makeInt(0);)
    REG_GUI(guid, "onTargetReached", 0, return Slot::makeInt(0);)
    // Per-widget event hooks (scripts may call them explicitly).
    REG_GUI(guid, "onsetvisible",    1, return Slot::makeInt(0);)
    REG_GUI(guid, "onSetVisible",    1, return Slot::makeInt(0);)
    REG_GUI(guid, "onResize",        4, return Slot::makeInt(0);)
    REG_GUI(guid, "onLeftButtonDown",2, return Slot::makeInt(0);)
    REG_GUI(guid, "onLeftButtonUp",  2, return Slot::makeInt(0);)
    REG_GUI(guid, "onLeftClick",     0, return Slot::makeInt(0);)
    REG_GUI(guid, "onTextChanged",   1, return Slot::makeInt(0);)
    REG_GUI(guid, "onMouseMove",     2, return Slot::makeInt(0);)
    REG_GUI(guid, "onEnterArea",     0, return Slot::makeInt(0);)
    REG_GUI(guid, "onLeaveArea",     0, return Slot::makeInt(0);)
    REG_GUI(guid, "onAction",        4, return Slot::makeInt(0);)
}

void registerGroup(Bindings &b) {
    // findObject: scan for a widget by id.  Always goes through the
    // SkinEngineRoot's wrapper pool so identity-equality holds —
    // every script that finds the same widget gets the SAME
    // RuntimeWidget* pointer.  Without this, dispatchEventByName
    // can't match script-bound variables to host-fired events.
    auto find = [](IObject *self, const QVector<Slot> &args) -> Slot {
        RuntimeWidget *root = asWidget(self);
        if (!root || !root->root) return Slot::makeObject(nullptr);
        QString id = args.value(0).s;
        auto *result = root->root->findWidgetWrapper(id);
        if (qEnvironmentVariableIsSet("WASABIQ_TRACE_FIND")) {
            fprintf(stderr, "[findObject] '%s' -> %p (widget=%p widget.id='%s')\n",
                    qPrintable(id),
                    static_cast<void*>(result),
                    result ? static_cast<void*>(result->widget) : nullptr,
                    result && result->widget ? qPrintable(result->widget->id) : "(null)");
        }
        return Slot::makeObject(result);
    };
    b.registerMethod(kGroupGuid(), QStringLiteral("findObject"), 1, find);
    b.registerMethod(kGroupGuid(), QStringLiteral("getObject"),  1, find);
    // GuiObject + Layout also have findObject.
    b.registerMethod(kGuiObjectGuid(), QStringLiteral("findObject"), 1, find);
    b.registerMethod(kLayoutGuid(),    QStringLiteral("findObject"), 1, find);
    b.registerMethod(kLayoutGuid(),    QStringLiteral("getObject"),  1, find);
}

void registerSlider(Bindings &b) {
    b.registerMethod(kSliderGuid(), QStringLiteral("getPosition"), 0,
        [](IObject *self, const QVector<Slot> &) -> Slot {
            RuntimeWidget *w = asWidget(self);
            if (!w || !w->widget || !w->runtime) return {};
            const WidgetOverride *ov = w->runtime->peek(w->widget->id);
            if (ov && ov->sliderPosition >= 0)
                return Slot::makeInt(ov->sliderPosition);
            return Slot::makeInt(0);
        });
    b.registerMethod(kSliderGuid(), QStringLiteral("setPosition"), 1,
        [](IObject *self, const QVector<Slot> &args) -> Slot {
            RuntimeWidget *w = asWidget(self);
            if (!w || !w->widget || !w->runtime) return {};
            w->runtime->mutate(w->widget->id).sliderPosition = qBound(0, int(args.value(0).asInt()), 255);
            return Slot::makeInt(0);
        });
    // onSetPosition / onSetFinalPosition are events scripts subscribe
    // to.  Scripts can also call them explicitly to trigger handlers;
    // we accept the call as a no-op return (the actual handler runs
    // via dispatchEventByName).
    b.registerMethod(kSliderGuid(), QStringLiteral("onSetPosition"), 1,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(kSliderGuid(), QStringLiteral("onSetFinalPosition"), 1,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
}

void registerToggleButton(Bindings &b) {
    auto getAct = [](IObject *self, const QVector<Slot> &) -> Slot {
        RuntimeWidget *w = asWidget(self);
        if (!w || !w->widget || !w->runtime) return {};
        const WidgetOverride *ov = w->runtime->peek(w->widget->id);
        return Slot::makeInt(ov && ov->activated == WidgetOverride::OnState ? 1 : 0);
    };
    auto setAct = [](IObject *self, const QVector<Slot> &args) -> Slot {
        RuntimeWidget *w = asWidget(self);
        if (!w || !w->widget || !w->runtime) return {};
        w->runtime->mutate(w->widget->id).activated =
            args.value(0).asBool() ? WidgetOverride::OnState : WidgetOverride::OffState;
        return Slot::makeInt(0);
    };
    auto onTog = [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); };

    // ToggleButton — the dedicated subclass.
    b.registerMethod(kToggleButtonGuid(), QStringLiteral("getActivated"), 0, getAct);
    b.registerMethod(kToggleButtonGuid(), QStringLiteral("setActivated"), 1, setAct);
    b.registerMethod(kToggleButtonGuid(), QStringLiteral("onToggle"),     1, onTog);
    // Button — Bento uses these too (e.g. for "active state" buttons
    // that don't sub-class ToggleButton in script).
    b.registerMethod(kButtonGuid(),       QStringLiteral("getActivated"), 0, getAct);
    b.registerMethod(kButtonGuid(),       QStringLiteral("setActivated"), 1, setAct);
}

void registerVis(Bindings &b) {
    // Vis modes: 0 = spectrum, 1 = oscilloscope, 2 = none.
    b.registerMethod(kVisGuid(), QStringLiteral("setMode"), 1,
        [](IObject *self, const QVector<Slot> &args) -> Slot {
            RuntimeWidget *w = asWidget(self);
            if (!w || !w->widget || !w->runtime) return {};
            w->runtime->mutate(w->widget->id).xmlParams.insert(
                "vismode", QString::number(args.value(0).asInt()));
            return Slot::makeInt(0);
        });
    b.registerMethod(kVisGuid(), QStringLiteral("nextMode"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
}

// Container — minimal stub.  newDynamicContainer / getContainer
// return a SystemObject-typed stand-in; switchToLayout is a no-op
// until layouts are wired into the host.
void registerContainer(Bindings &b) {
    b.registerMethod(kContainerGuid(), QStringLiteral("switchToLayout"), 1,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(kContainerGuid(), QStringLiteral("getCurLayout"), 0,
        [](IObject *self, const QVector<Slot> &) -> Slot {
            return Slot::makeObject(self);   // self-reference so chained calls don't crash
        });
    b.registerMethod(kContainerGuid(), QStringLiteral("getLayout"), 1,
        [](IObject *self, const QVector<Slot> &) -> Slot {
            return Slot::makeObject(self);
        });
    b.registerMethod(kContainerGuid(), QStringLiteral("show"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(kContainerGuid(), QStringLiteral("hide"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(kContainerGuid(), QStringLiteral("isVisible"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(1); });
    b.registerMethod(kContainerGuid(), QStringLiteral("getName"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeString({}); });
    b.registerMethod(kContainerGuid(), QStringLiteral("getContainer"), 1,
        [](IObject *self, const QVector<Slot> &) -> Slot { return Slot::makeObject(self); });
    b.registerMethod(kLayoutGuid(),    QStringLiteral("getContainer"), 0,
        [](IObject *self, const QVector<Slot> &) -> Slot { return Slot::makeObject(self); });
    // ScriptFrame / AnimatedLayer numerics.
    static const QUuid scriptFrameGuid("{e2bbc14d-84f6-4173-bdb3-b2eb2f665550}");
    static const QUuid animatedLayerGuid("{6b64cd27-5a26-4c4b-8c59-e6a70cf6493a}");
    b.registerMethod(scriptFrameGuid, QStringLiteral("getPosition"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("getLength"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(1); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("play"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("stop"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("pause"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("gotoFrame"), 1,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("setSpeed"), 1,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
    b.registerMethod(animatedLayerGuid, QStringLiteral("getCurFrame"), 0,
        [](IObject *, const QVector<Slot> &) -> Slot { return Slot::makeInt(0); });
}

void registerText(Bindings &b) {
    b.registerMethod(kTextGuid(), QStringLiteral("setText"), 1,
        [](IObject *self, const QVector<Slot> &args) -> Slot {
            RuntimeWidget *w = asWidget(self);
            if (!w || !w->widget || !w->runtime) return {};
            w->runtime->mutate(w->widget->id).textOverride = args.value(0).s;
            return Slot::makeInt(0);
        });
    b.registerMethod(kTextGuid(), QStringLiteral("getText"), 0,
        [](IObject *self, const QVector<Slot> &) -> Slot {
            RuntimeWidget *w = asWidget(self);
            if (!w || !w->widget || !w->runtime) return {};
            const WidgetOverride *ov = w->runtime->peek(w->widget->id);
            if (ov && !ov->textOverride.isEmpty())
                return Slot::makeString(ov->textOverride);
            return Slot::makeString(w->widget->text);
        });
}

} // namespace

void registerStandardBindings(Bindings &b) {
    registerSystemObject(b);
    // GuiObject methods are registered against EVERY widget GUID
    // because Maki scripts call them on the concrete subclass via
    // its own type tag.
    for (const QUuid &g : {kGuiObjectGuid(), kGroupGuid(), kLayerGuid(),
                           kButtonGuid(), kSliderGuid(), kTextGuid()}) {
        registerGuiObjectMethods(b, g);
    }
    registerGroup(b);
    registerSlider(b);
    registerText(b);
    registerToggleButton(b);
    registerVis(b);
    registerContainer(b);
}

} // namespace wasabiq::maki
