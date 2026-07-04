// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "Gradient.h"

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/PaintCtx.h>

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <vector>

namespace qtWasabi {

namespace {

struct Stop {
    double pos = 0.0;
    QColor color;
};

double attrDouble(const QHash<QString, QString> &attrs,
                  const QString &key, double def) {
    bool ok = false;
    const double v = attrs.value(key).toDouble(&ok);
    return ok ? v : def;
}

// "r,g,b" or "r,g,b,a"; falls back to a named colour token so themed
// ids work in point lists too.
QColor parseStopColor(const QString &value, PaintCtx &ctx) {
    const auto parts = value.split(QChar(','));
    if (parts.size() == 3 || parts.size() == 4) {
        bool ok0 = false, ok1 = false, ok2 = false;
        const int r = parts[0].trimmed().toInt(&ok0);
        const int g = parts[1].trimmed().toInt(&ok1);
        const int b = parts[2].trimmed().toInt(&ok2);
        if (ok0 && ok1 && ok2) {
            int a = 255;
            if (parts.size() == 4) a = parts[3].trimmed().toInt();
            return QColor(qBound(0, r, 255), qBound(0, g, 255),
                          qBound(0, b, 255), qBound(0, a, 255));
        }
    }
    if (ctx.colors)
        return ctx.colors->resolve(value, ctx.gammasets,
                                   QColor(255, 0, 255));
    return QColor(255, 0, 255);
}

// "pos=r,g,b,a;pos=r,g,b,a;…".  Without a points attribute the
// reference renders a green→blue→red debug ramp; keep that so a
// missing attribute is visible rather than silently blank.
std::vector<Stop> parsePoints(const QString &pointList, PaintCtx &ctx) {
    std::vector<Stop> stops;
    for (const QString &entry :
         pointList.split(QChar(';'), Qt::SkipEmptyParts)) {
        const int eq = entry.indexOf(QChar('='));
        if (eq <= 0) continue;
        bool ok = false;
        const double pos = entry.left(eq).trimmed().toDouble(&ok);
        if (!ok) continue;
        stops.push_back({ pos,
            parseStopColor(entry.mid(eq + 1).trimmed(), ctx) });
    }
    if (stops.empty()) {
        stops.push_back({ 0.0, QColor(0, 255, 0) });
        stops.push_back({ 0.5, QColor(0, 0, 255, 0) });
        stops.push_back({ 1.0, QColor(255, 0, 0) });
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const Stop &a, const Stop &b) {
                         return a.pos < b.pos;
                     });
    return stops;
}

}  // namespace

void GradientWidget::paint(QPainter *p, PaintCtx &ctx,
                           const QSize &canvas) {
    if (attrs.value(QStringLiteral("visible")) == QStringLiteral("0"))
        return;
    const int stateAlpha = ctx.windowActive
        ? attrs.value(QStringLiteral("activealpha"),
                      QStringLiteral("255")).toInt()
        : attrs.value(QStringLiteral("inactivealpha"),
                      QStringLiteral("255")).toInt();
    const int layerAlpha = attrs.value(QStringLiteral("alpha"),
                                       QStringLiteral("255")).toInt();
    if (stateAlpha <= 0 || layerAlpha <= 0) return;

    const QRect r = resolveRect(canvas);
    if (r.width() <= 0 || r.height() <= 0) return;

    std::vector<Stop> stops =
        parsePoints(attrs.value(QStringLiteral("points")), ctx);

    // Stop colours are declared neutral and tinted by the gammagroup of
    // the ACTIVE Color Theme — that is how the Win9x titlebar gradients
    // pick up the theme hue from grey 128,128,128 bases.
    if (ctx.gammasets) {
        const QString gg = attrs.value(QStringLiteral("gammagroup"));
        const GammaGroup group = ctx.gammasets->transformFor(gg);
        const GammaGroup global = ctx.gammasets->globalTransform();
        for (Stop &s : stops) {
            s.color = GammasetRegistry::applyToColor(s.color, group);
            s.color = GammasetRegistry::applyToColor(s.color, global);
        }
    }

    const double x1 = attrDouble(attrs, QStringLiteral("gradient_x1"), 0.0);
    const double y1 = attrDouble(attrs, QStringLiteral("gradient_y1"), 0.0);
    const double x2 = attrDouble(attrs, QStringLiteral("gradient_x2"), 1.0);
    const double y2 = attrDouble(attrs, QStringLiteral("gradient_y2"), 1.0);

    const qreal prevOpacity = p->opacity();
    p->setOpacity(prevOpacity * (stateAlpha / 255.0)
                              * (layerAlpha / 255.0));

    if (stops.size() == 1) {
        p->fillRect(r, stops[0].color);
    } else if (attrs.value(QStringLiteral("mode"))
                   .compare(QStringLiteral("circular"),
                            Qt::CaseInsensitive) == 0) {
        // Distances are measured in the box's NORMALISED space, so on a
        // non-square box the iso-lines are ellipses; a device-space
        // QRadialGradient would draw circles instead.  Walk the pixels.
        const double tot = std::hypot(x1 - x2, y1 - y2);
        QImage img(r.size(), QImage::Format_ARGB32);
        for (int py = 0; py < r.height(); ++py) {
            QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(py));
            const double fy = double(py) / r.height();
            for (int px = 0; px < r.width(); ++px) {
                const double fx = double(px) / r.width();
                const double dist = std::hypot(fx - x1, fy - y1);
                QColor c;
                if (dist <= stops.front().pos * tot) {
                    c = stops.front().color;
                } else if (dist >= stops.back().pos * tot) {
                    c = stops.back().color;
                } else {
                    for (size_t i = 0; i + 1 < stops.size(); ++i) {
                        const double d1 = stops[i].pos * tot;
                        const double d2 = stops[i + 1].pos * tot;
                        if (dist < d1 || dist > d2 || d2 <= d1) continue;
                        const double t = (dist - d1) / (d2 - d1);
                        const QColor &a = stops[i].color;
                        const QColor &b = stops[i + 1].color;
                        c = QColor(
                            int(a.red()   + (b.red()   - a.red())   * t),
                            int(a.green() + (b.green() - a.green()) * t),
                            int(a.blue()  + (b.blue()  - a.blue())  * t),
                            int(a.alpha() + (b.alpha() - a.alpha()) * t));
                        break;
                    }
                }
                line[px] = c.rgba();
            }
        }
        p->drawImage(r.topLeft(), img);
    } else {
        QLinearGradient grad(r.x() + x1 * r.width(),
                             r.y() + y1 * r.height(),
                             r.x() + x2 * r.width(),
                             r.y() + y2 * r.height());
        grad.setSpread(QGradient::PadSpread);
        for (const Stop &s : stops)
            grad.setColorAt(qBound(0.0, s.pos, 1.0), s.color);
        p->fillRect(r, grad);
    }

    p->setOpacity(prevOpacity);
}

}  // namespace qtWasabi
