// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// titlebar_test — first pixel-regression target.
//
// Composes Winamp Modern's titlebar streak (the silver chevron) by
// hand from its constituent <bitmap> definitions and compares the
// rendered QImage against a golden reference saved alongside the
// test.  Failure on any pixel — that's the point of the harness.
//
// We render JUST the active streak strip (354x9), not the full 18px
// titlebar with text + overlay + inactive layers.  Full layout
// rendering with sendparams + Maki-driven activeAlpha lands later;
// this test exists so any regression in the BitmapRegistry sub-rect
// extraction or LayerPainter blit positioning fails immediately.
//
// Reference image: tests/golden/titlebar_active_streak.png.  If
// missing, the first run writes it (and the test passes); subsequent
// runs compare bit-for-bit.  Set WASABIQT_REGEN_GOLDENS=1 to refresh.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QtTest/QtTest>

using namespace WasabiQt;
using namespace WasabiQt::SkinXml;

namespace {

// Build the active-streak strip at the given width (which the real
// skin sets dynamically via sendparams).  Layout matches the
// `wasabi.titlebar.streak` groupdef in titlebar.xml — left bitmap
// pinned to x=0, center stretched between, right pinned to the right.
QImage renderActiveStreak(BitmapRegistry &reg, int totalW) {
    constexpr int H = 9;
    QImage out(totalW, H, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    const QSize canvas(totalW, H);

    auto layer = [&](const QString &id, int x, int y, int w, int h,
                     bool relatx, bool relatw) {
        QHash<QString, QString> a;
        a.insert(QStringLiteral("image"), id);
        a.insert(QStringLiteral("x"),     QString::number(x));
        a.insert(QStringLiteral("y"),     QString::number(y));
        a.insert(QStringLiteral("w"),     QString::number(w));
        a.insert(QStringLiteral("h"),     QString::number(h));
        if (relatx) a.insert(QStringLiteral("relatx"), QStringLiteral("1"));
        if (relatw) a.insert(QStringLiteral("relatw"), QStringLiteral("1"));
        return LayerPainter::paintLayer(&p, reg, a, canvas);
    };

    // From titlebar.xml's `wasabi.titlebar.streak` groupdef:
    //   <layer image="wasabi.titlebar.left.active"   x="0"  y="1"/>
    //   <layer image="wasabi.titlebar.center.active" x="10" y="1"
    //          w="-10" relatw="1"/>
    //   <layer image="wasabi.titlebar.right.active"  x="-10" relatx="1" y="1"/>
    // y=1 inside the streak group; we render at y=0 in this strip.
    bool ok = true;
    ok &= layer(QStringLiteral("wasabi.titlebar.left.active"),
                0, 0, 0, H, false, false);
    ok &= layer(QStringLiteral("wasabi.titlebar.center.active"),
                10, 0, -10, H, false, true);
    ok &= layer(QStringLiteral("wasabi.titlebar.right.active"),
                -10, 0, 0, H, true, false);
    Q_ASSERT(ok);
    return out;
}

QString goldenPath() {
    // Source-tree resolution via QT_TESTCASE_BUILDDIR — points at the
    // test binary's build dir; we walk back up to the source tree.
    return QStringLiteral(WASABIQT_SOURCE_DIR) +
           QStringLiteral("/tests/golden/titlebar_active_streak.png");
}

}  // namespace

class TitlebarTest : public QObject {
    Q_OBJECT
private slots:

    void streakComposites() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d;
        QVERIFY(parse(p, d));

        BitmapRegistry reg;
        reg.loadFromDocument(d);

        const QImage rendered = renderActiveStreak(reg, 354);
        QCOMPARE(rendered.size(), QSize(354, 9));

        // Spot-check three regions: left chevron opaque around x=2..8,
        // center opaque, right chevron opaque around x=347..352.
        for (int x : {2, 5, 8}) {
            QVERIFY2(qAlpha(rendered.pixel(x, 4)) > 0,
                qPrintable(QStringLiteral("left edge transparent at x=%1").arg(x)));
        }
        for (int x : {180, 200, 250}) {
            QVERIFY2(qAlpha(rendered.pixel(x, 4)) > 0,
                qPrintable(QStringLiteral("middle transparent at x=%1").arg(x)));
        }
        for (int x : {347, 350, 352}) {
            QVERIFY2(qAlpha(rendered.pixel(x, 4)) > 0,
                qPrintable(QStringLiteral("right edge transparent at x=%1").arg(x)));
        }
    }

    void matchesGoldenReference() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d;
        QVERIFY(parse(p, d));
        BitmapRegistry reg;
        reg.loadFromDocument(d);

        const QImage rendered = renderActiveStreak(reg, 354);
        const QString gp = goldenPath();
        const bool regen = qEnvironmentVariableIsSet("WASABIQT_REGEN_GOLDENS");

        QFileInfo gfi(gp);
        QDir().mkpath(gfi.absolutePath());

        if (regen || !QFile::exists(gp)) {
            QVERIFY2(rendered.save(gp),
                qPrintable(QStringLiteral("failed to write %1").arg(gp)));
            qInfo().noquote() << QStringLiteral("Wrote golden: %1").arg(gp);
            return;
        }

        QImage golden;
        QVERIFY2(golden.load(gp),
            qPrintable(QStringLiteral("could not load golden %1").arg(gp)));
        QCOMPARE(rendered.size(), golden.size());

        // Bit-exact pixel compare; collect up to 10 mismatches for diagnosis.
        QList<QPair<int,int>> mismatches;
        const QImage a = rendered.convertToFormat(QImage::Format_ARGB32);
        const QImage b = golden  .convertToFormat(QImage::Format_ARGB32);
        int total = 0;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                if (a.pixel(x, y) != b.pixel(x, y)) {
                    ++total;
                    if (mismatches.size() < 10)
                        mismatches.append({x, y});
                }
            }
        }
        if (total > 0) {
            qWarning().noquote() << QStringLiteral(
                "Pixel mismatches: %1 (first %2 shown)")
                .arg(total).arg(mismatches.size());
            for (const auto &m : mismatches) {
                qWarning().noquote() << QStringLiteral(
                    "  (%1,%2): rendered=%3 golden=%4")
                    .arg(m.first).arg(m.second)
                    .arg(a.pixel(m.first, m.second), 8, 16, QChar('0'))
                    .arg(b.pixel(m.first, m.second), 8, 16, QChar('0'));
            }
        }
        QCOMPARE(total, 0);
    }
};

QTEST_GUILESS_MAIN(TitlebarTest)
#include "titlebar_test.moc"
