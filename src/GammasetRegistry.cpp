// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/SkinXml.h>

#include <QImage>
#include <QString>
#include <algorithm>

namespace qtWasabi {

namespace {

void parseValue(const QString &csv, GammaGroup &out) {
    const auto parts = csv.split(QChar(','));
    if (parts.size() >= 1) out.r = parts[0].toInt();
    if (parts.size() >= 2) out.g = parts[1].toInt();
    if (parts.size() >= 3) out.b = parts[2].toInt();
}

void collect(const SkinXml::Element &el, QHash<QString, Gammaset> &sets,
             QString &firstName) {
    if (el.tag == QStringLiteral("gammaset")) {
        Gammaset s;
        s.name = el.attrs.value(QStringLiteral("id"));
        for (const auto &child : el.children) {
            if (child.tag != QStringLiteral("gammagroup")) continue;
            const QString id = child.attrs.value(QStringLiteral("id"));
            if (id.isEmpty()) continue;
            GammaGroup g;
            parseValue(child.attrs.value(QStringLiteral("value")), g);
            g.gray  = child.attrs.value(QStringLiteral("gray")).toInt();
            g.boost = child.attrs.value(QStringLiteral("boost")).toInt();
            s.groups.insert(id, g);
        }
        if (!s.name.isEmpty()) {
            if (firstName.isEmpty()) firstName = s.name;
            sets.insert(s.name, std::move(s));
        }
    }
    for (const auto &child : el.children) collect(child, sets, firstName);
}

}  // namespace

int GammasetRegistry::loadFromDocument(const SkinXml::Document &doc) {
    m_sets.clear();
    m_active = nullptr;
    QString firstName;
    collect(doc.root, m_sets, firstName);
    // Winamp-Modern skins (Bento/Big Bento) mark the auto-selected theme
    // with a leading '*': <gammaset id="*Default">.  Without honouring it
    // m_active stays null and NO gammagroup tints are applied to any
    // bitmap — leaving frame borders, button bevels etc. their near-black
    // source colour instead of the skin's gray.  Prefer "*Default", then
    // fall back to bare "Default" for non-Modern skin formats.
    // Remember whether the skin shipped any Color Themes of its own.  When
    // it didn't, the embedder will (after the bitmaps are tagged) ask us to
    // synthesize the per-role recolor themes — a richly-themed skin is
    // never touched.
    m_hadNoNativeThemes = m_sets.isEmpty();
    m_injectedSynthetic = false;
    if (auto it = m_sets.constFind(QStringLiteral("*Default"));
        it != m_sets.constEnd())
        m_active = &it.value();
    else if (auto it2 = m_sets.constFind(QStringLiteral("Default"));
             it2 != m_sets.constEnd())
        m_active = &it2.value();
    else if (auto it3 = m_sets.constFind(firstName);
             it3 != m_sets.constEnd())
        // No set is named Default at all: the reference GammaManager
        // activates the first gammaset in document order (skins like
        // Winamp2000SP4 rely on that; without it no tint applies and
        // the whole window renders its untinted near-gray sources).
        m_active = &it3.value();
    return m_sets.size();
}

void GammasetRegistry::injectGlobalTheme(const QString &name,
                                         const GammaGroup &xform) {
    if (name.isEmpty()) return;
    // m_active points INTO m_sets; insert() may rehash and invalidate it,
    // so remember the active name and re-resolve afterwards.
    const QString activeName = m_active ? m_active->name : QString();
    Gammaset gs;
    gs.name        = name;
    gs.global      = true;
    gs.globalXform = xform;
    m_sets.insert(name, std::move(gs));
    m_active = activeName.isEmpty() ? nullptr : find(activeName);
}

namespace {
// The visual role a gammagroup plays, inferred from its NAME.  Skins —
// even closed ones — name their groups semantically ("Backgrounds",
// "Buttons", "Display Fonts", "tickercolor", "frames1color", …), and the
// synthetic-role bitmaps the engine tags carry "syn.<role>" names, so a
// single keyword pass classifies both.  Order matters (a "playbutton" is
// a button first).
QString roleOfGroup(const QString &gIn) {
    const QString n = gIn.toLower();
    auto has = [&](const char *k) { return n.contains(QLatin1String(k)); };
    if (has("button") || has("btn") || has("hover")) return QStringLiteral("button");
    if (has("slider") || has("thumb") || has("knob") || has("posbar") ||
        has("volume") || has("balance") || has("seek")) return QStringLiteral("slider");
    // The display read-out and ticker glow in the accent colour (classic
    // green) — distinct from plain UI labels, which stay light.
    if (has("ticker") || (has("display") && (has("font") || has("text"))) ||
        has("spectrum") || has("analyzer") || has("osc") || has("vis"))
        return QStringLiteral("vis");
    if (has("font") || has("text") || has("number") || has("digit") ||
        has("title")) return QStringLiteral("text");
    if (has("display") || has("lcd") || has("screen")) return QStringLiteral("display");
    // Genuine accent elements (LED / glow / progress / active-state) carry
    // the theme's accent colour — Good Ol' Winamp's yellow.  Frames/bevels
    // stay background (they are slate in the real theme, not yellow).
    if (has("accent") || has("glow") || has("led") || has("progress") ||
        has("active") || has("status"))
        return QStringLiteral("accent");
    return QStringLiteral("background");   // frame*, main*, window, base, bg…
}
}  // namespace

void GammasetRegistry::injectSyntheticThemes(const QStringList &groupNames) {
    if (m_injectedSynthetic || groupNames.isEmpty()) return;
    // m_active points INTO m_sets; insert() may rehash and invalidate it
    // (a themed skin has a live *Default), so remember + re-resolve by name.
    const QString activeName = m_active ? m_active->name : QString();

    // "Good Ol' Winamp" is the REAL Winamp Modern gammaset, verbatim — so on
    // any skin that uses the standard gammagroup names (Backgrounds, Buttons,
    // Led, ProgressBar, Display*, ListText, …) the synthetic theme is
    // byte-identical to the native one: green display, yellow LED/progress,
    // slate body, silver buttons.  A skin-specific group the table doesn't
    // know (e.g. "frames1color") is routed through its role to a
    // representative Good Ol' Winamp group.
    static const struct { const char *name; int r, g, b, gray, boost; } kGOW[] = {
        {"Backgrounds",-2848,-2848,-2368,2,0}, {"Backgrounds2",-2880,-2880,-2336,2,0},
        {"Bolt",0,0,0,0,0}, {"Menubar",-2880,-2880,-2336,1,0},
        {"MenubarText",2220,2220,2220,1,1}, {"Titlebar",-2848,-2848,-2368,2,0},
        {"TitlebarText",0,0,0,0,0}, {"TitlebarElements",192,-128,-1632,2,0},
        {"TitlebarHover",1152,608,-1920,2,0}, {"Text",-4096,1024,-4096,1,1},
        {"Text Inverse",0,0,0,0,0}, {"Buttons",-484,-356,-356,2,0},
        {"ButtonsHover",0,128,128,2,0}, {"Buttons2",-484,-356,-356,2,0},
        {"ButtonsHover2",0,128,128,2,0}, {"ButtonGlow",1696,2976,-2464,0,0},
        {"ButtonsActive",-4096,-1024,-4096,1,0}, {"ButtonStatus",1696,2976,-2464,2,0},
        {"ButtonText",0,0,0,1,0}, {"Scrollbar Buttons",-484,-356,-356,2,0},
        {"Scrollbar Hover",0,128,128,2,0}, {"Led",1696,2976,-2464,0,0},
        {"Display",-2432,-2432,-1696,2,0}, {"DisplaySongtickerBG",-2432,-2432,-1696,2,0},
        {"DisplayElements",-4096,0,-4096,1,0}, {"DisplayVis",-4096,0,-4096,1,0},
        {"DisplayBeatVis",-4096,0,-4096,1,0}, {"DisplaySongticker",-4096,0,-4096,1,0},
        {"DisplaySongtickerShade",-4096,0,-4096,1,0}, {"ProgressBar",1696,2976,-2464,0,0},
        {"BGOverlayDark",200,300,300,1,1}, {"BGOverlayBright",-1096,-1096,-1096,1,0},
        {"BG2Textoverlay",1200,1300,1300,1,1}, {"TextDark",2700,2800,2800,1,1},
        {"ListText",-4096,1024,-4096,1,0}, {"ListTextSelected",-4096,1024,-4096,1,0},
        {"ListTextCurrent",4096,4096,4096,1,0}, {"ListBackground",-4096,-4096,-4096,1,0},
        {"ListSelBackground",-4096,-4096,1400,2,0}, {"ListColumnText",2400,2400,2400,0,1},
        {"Tooltips Background",-2848,-2848,-2368,2,0}, {"Tooltips Text",-4096,3727,-4096,1,1},
    };
    auto gowByName = [](const char *nm) -> GammaGroup {
        for (const auto &e : kGOW)
            if (QLatin1String(e.name) == QLatin1String(nm))
                return GammaGroup{e.r, e.g, e.b, e.gray, e.boost};
        return GammaGroup{};
    };
    auto gowFor = [&](const QString &grp) -> GammaGroup {
        // Synthetic body group (theme-less skins) — a dark, saturated purple,
        // the way "Good Ol' Winamp" darkens the Winamp Modern body.  Tunable
        // for development via WASABIQT_SYN_BODY="r,g,b,gray,boost".
        if (grp == QLatin1String("syn.body")) {
            static const GammaGroup body = [] {
                const QByteArray ov = qgetenv("WASABIQT_SYN_BODY");
                const QList<QByteArray> p = ov.split(',');
                if (p.size() >= 3)
                    return GammaGroup{p[0].trimmed().toInt(), p[1].trimmed().toInt(),
                                      p[2].trimmed().toInt(),
                                      p.size() > 3 ? p[3].trimmed().toInt() : 2,
                                      p.size() > 4 ? p[4].trimmed().toInt() : 0};
                return GammaGroup{-708, -1144, -205, 2, 0};
            }();
            return body;
        }
        for (const auto &e : kGOW)
            if (grp.compare(QLatin1String(e.name), Qt::CaseInsensitive) == 0)
                return GammaGroup{e.r, e.g, e.b, e.gray, e.boost};
        const QString role = roleOfGroup(grp);
        if (role == QLatin1String("button"))  return gowByName("Buttons");
        if (role == QLatin1String("slider"))  return gowByName("Scrollbar Buttons");
        if (role == QLatin1String("vis"))     return gowByName("DisplaySongticker");
        if (role == QLatin1String("text"))    return gowByName("Text");
        if (role == QLatin1String("display")) return gowByName("Display");
        // Accent fallback (synthetic close/minimize etc.): a pure yellow that
        // reads the same over any source — desaturate, then boost R+G equally
        // and kill blue.  (A real "Led" group, present in kGOW, still gets the
        // exact native value above, so Winamp Modern stays byte-identical.)
        if (role == QLatin1String("accent"))
            return GammaGroup{2400, 2400, -3200, 2, 0};
        return gowByName("Backgrounds");
    };
    {
        Gammaset s;
        s.name = QStringLiteral("Synthetic: Good Ol' Winamp");
        for (const QString &grp : groupNames) s.groups.insert(grp, gowFor(grp));
        s.hasChrome        = true;
        s.chromeBg         = QColor(QStringLiteral("#292939"));
        s.chromeText       = QColor(QStringLiteral("#ffffff"));
        s.chromeField      = QColor(QStringLiteral("#000000"));
        s.chromeFieldText  = QColor(QStringLiteral("#00ff00"));
        s.chromeSelBg      = QColor(QStringLiteral("#0000c3"));
        s.chromeSelText    = QColor(QStringLiteral("#00ff00"));
        s.chromeBorder     = QColor(QStringLiteral("#a8a8a8"));
        s.chromeButtonText = QColor(QStringLiteral("#000000"));
        m_sets.insert(s.name, std::move(s));
    }

    // Role-based styles with no single native reference to copy.  `body` =
    // the synthetic body group, `accent` = the synthetic accent (seek/logo).
    auto g = [](int r, int gg, int b) {
        GammaGroup t; t.r = r; t.g = gg; t.b = b; t.gray = 2; return t;
    };
    struct Style {
        const char *name;
        GammaGroup body, accent, bg, btn, disp, sld, vis, txt;
        const char *cBg, *cText, *cField, *cFieldText,
                   *cSelBg, *cSelText, *cBorder, *cButtonText;
    };
    const Style styles[] = {
        // Winamp Modern: cream/beige body, silver buttons, blue accents and
        // blue display read-out.
        { "Synthetic: Winamp Modern",
          g(2800, 2700, 2000), g(-3000, -400, 1900),
          g(2800, 2700, 2000), g(1200, 1300, 1600), g(-3200, -2900, -2000),
          g(1000, 1100, 1500), g(-2600, -600, 1900), g(-2000, -400, 1600),
          "#dfe1e8", "#202830", "#ffffff", "#203040",
          "#2a6fd0", "#ffffff", "#9aa0ac", "#000000" },
        { "Synthetic: Grayscale",
          g(-1400, -1400, -1400), g(1600, 1600, 1600),
          g(-1400, -1400, -1400), g(-450, -450, -450), g(-3200, -3200, -3200),
          g(1100, 1100, 1100), g(1600, 1600, 1600), g(1700, 1700, 1700),
          "#0a0a0a", "#d0d0d0", "#000000", "#c0c0c0",
          "#444444", "#ffffff", "#555555", "#000000" },
    };
    for (const Style &st : styles) {
        Gammaset s;
        s.name = QString::fromLatin1(st.name);
        for (const QString &grp : groupNames) {
            const QString role = roleOfGroup(grp);
            const GammaGroup t =
                grp  == QLatin1String("syn.body")   ? st.body :
                grp  == QLatin1String("syn.accent") ? st.accent :
                role == QLatin1String("accent")  ? st.accent :
                role == QLatin1String("button")  ? st.btn :
                role == QLatin1String("slider")  ? st.sld :
                role == QLatin1String("vis")     ? st.vis :
                role == QLatin1String("text")    ? st.txt :
                role == QLatin1String("display") ? st.disp : st.bg;
            s.groups.insert(grp, t);
        }
        s.hasChrome        = true;
        s.chromeBg         = QColor(QString::fromLatin1(st.cBg));
        s.chromeText       = QColor(QString::fromLatin1(st.cText));
        s.chromeField      = QColor(QString::fromLatin1(st.cField));
        s.chromeFieldText  = QColor(QString::fromLatin1(st.cFieldText));
        s.chromeSelBg      = QColor(QString::fromLatin1(st.cSelBg));
        s.chromeSelText    = QColor(QString::fromLatin1(st.cSelText));
        s.chromeBorder     = QColor(QString::fromLatin1(st.cBorder));
        s.chromeButtonText = QColor(QString::fromLatin1(st.cButtonText));
        m_sets.insert(s.name, std::move(s));
    }
    m_active = activeName.isEmpty() ? nullptr : find(activeName);
    m_injectedSynthetic = true;
}

GammaGroup GammasetRegistry::globalTransform() const {
    if (m_active && m_active->global) return m_active->globalXform;
    return GammaGroup{};
}

const Gammaset *GammasetRegistry::find(const QString &name) const {
    auto it = m_sets.constFind(name);
    return it == m_sets.constEnd() ? nullptr : &it.value();
}

QString GammasetRegistry::defaultThemeName() const {
    if (m_sets.contains(QStringLiteral("*Default")))
        return QStringLiteral("*Default");
    if (m_sets.contains(QStringLiteral("Default")))
        return QStringLiteral("Default");
    return QString();   // skin ships no theme — its raw colours are "default"
}

void GammasetRegistry::setActiveGammaset(const QString &name) {
    auto it = m_sets.constFind(name);
    m_active = (it == m_sets.constEnd()) ? nullptr : &it.value();
}

GammaGroup GammasetRegistry::transformFor(const QString &gammagroup) const {
    if (!m_active || gammagroup.isEmpty()) return {};
    auto it = m_active->groups.constFind(gammagroup);
    return it == m_active->groups.constEnd() ? GammaGroup{} : it.value();
}

void GammasetRegistry::applyToImage(QImage &img, const GammaGroup &t,
                                   int chromaMin) {
    if (img.isNull()) return;
    const bool identity = t.r == 0 && t.g == 0 && t.b == 0 &&
                          t.gray == 0 && t.boost == 0;
    if (identity) return;

    if (img.format() != QImage::Format_ARGB32 &&
        img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32);

    // Pre-compute multipliers for the linear-RGB transform.  Mirrors
    // upstream Winamp's GammaFilter::filterBitmap.
    const int rm = 65535 + (t.r << 4);
    const int gm = 65535 + (t.g << 4);
    const int bm = 65535 + (t.b << 4);

    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            QRgb p = line[x];
            int A = qAlpha(p);
            int R = qRed(p);
            int G = qGreen(p);
            int B = qBlue(p);

            // Saturation gate: leave near-neutral pixels (the silver speaker
            // cones) untouched, only recolour the saturated panel.
            if (chromaMin > 0) {
                const int mx = std::max({R, G, B});
                const int mn = std::min({R, G, B});
                if (mx - mn < chromaMin) continue;
            }

            // gray=1: take MAX of channels (luminance-by-max).
            // gray=2: take average.
            if (t.gray == 1) {
                const int m = std::max({R, G, B});
                R = G = B = m;
            } else if (t.gray == 2) {
                const int m = (R + G + B) / 3;
                R = G = B = m;
            }

            // boost: shift each channel toward white by alpha/2.
            if (t.boost) {
                const int add = A >> 1;
                R = std::min(255, (R >> 1) + add);
                G = std::min(255, (G >> 1) + add);
                B = std::min(255, (B >> 1) + add);
            }

            // Linear per-channel transform — channel_out = R * rm / 65535.
            R = (R * rm) >> 16;
            G = (G * gm) >> 16;
            B = (B * bm) >> 16;
            R = std::clamp(R, 0, 255);
            G = std::clamp(G, 0, 255);
            B = std::clamp(B, 0, 255);

            line[x] = qRgba(R, G, B, A);
        }
    }
}

}  // namespace qtWasabi
