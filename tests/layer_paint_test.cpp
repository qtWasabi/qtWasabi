// M4 — first widget paints through Qt.
//
// Load Winamp Modern's skin XML, build the BitmapRegistry, fetch a
// known sub-rect bitmap, paint it as a <layer> onto a QImage, and
// verify the result is the right dimensions and visibly non-empty.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/LayerPainter.h>

#include <QDir>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QtTest/QtTest>

using namespace WasabiQt;
using namespace WasabiQt::SkinXml;

namespace {
QImage paintSingleLayer(BitmapRegistry &reg,
                        const QHash<QString, QString> &attrs,
                        const QSize &canvas) {
    QImage out(canvas, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    LayerPainter::paintLayer(&p, reg, attrs, canvas);
    return out;
}
}  // namespace

class LayerPaintTest : public QObject {
    Q_OBJECT
private slots:

    void registryFindsBitmaps() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d;
        QVERIFY(parse(p, d));

        BitmapRegistry reg;
        const int n = reg.loadFromDocument(d);
        qInfo().noquote() << QStringLiteral(
            "Registered %1 bitmaps").arg(n);
        QVERIFY(n > 100);          // Modern has hundreds of bitmaps

        // Spot-check one with a sub-rect that we know exists.
        const auto *def = reg.find(QStringLiteral("wasabi.frame.top.left"));
        QVERIFY(def != nullptr);
        QCOMPARE(def->srcRect, QRect(0, 0, 10, 18));
    }

    void titlebarCornerSubrect() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d;
        QVERIFY(parse(p, d));

        BitmapRegistry reg;
        reg.loadFromDocument(d);
        QImage corner = reg.imageFor(QStringLiteral("wasabi.frame.top.left"));
        QVERIFY2(!corner.isNull(),
                 "wasabi.frame.top.left bitmap couldn't be loaded");
        QCOMPARE(corner.size(), QSize(10, 18));

        // Confirm the sub-rect is visibly non-empty (not all transparent).
        bool sawPixel = false;
        for (int y = 0; y < corner.height() && !sawPixel; ++y) {
            for (int x = 0; x < corner.width(); ++x) {
                if (qAlpha(corner.pixel(x, y)) > 0 ||
                    qRed (corner.pixel(x, y)) > 0 ||
                    qGreen(corner.pixel(x, y)) > 0 ||
                    qBlue(corner.pixel(x, y)) > 0) {
                    sawPixel = true;
                    break;
                }
            }
        }
        QVERIFY(sawPixel);
    }

    void layerPaintsToCanvas() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d;
        QVERIFY(parse(p, d));

        BitmapRegistry reg;
        reg.loadFromDocument(d);

        // Synthesise a <layer image="…"/> at (5, 5) on a 100x100 canvas.
        QHash<QString, QString> layer;
        layer.insert(QStringLiteral("image"),
                     QStringLiteral("wasabi.frame.top.left"));
        layer.insert(QStringLiteral("x"), QStringLiteral("5"));
        layer.insert(QStringLiteral("y"), QStringLiteral("5"));
        // No w/h — should default to bitmap natural size (10x18).

        QImage out = paintSingleLayer(reg, layer, QSize(100, 100));
        QCOMPARE(out.size(), QSize(100, 100));

        // The bitmap was drawn at (5,5) with size (10, 18).  Verify
        // a pixel inside that range is non-transparent and a pixel
        // outside is transparent.
        QVERIFY(qAlpha(out.pixel(8, 8)) > 0 ||
                qRed  (out.pixel(8, 8)) > 0);
        QCOMPARE(qAlpha(out.pixel(50, 50)), 0);

        // Save the artefact for human inspection.
        const QString outPath = QDir::temp().filePath(
            QStringLiteral("wasabiqt-m4-layer.png"));
        QVERIFY(out.save(outPath));
        qInfo().noquote() << QStringLiteral("Wrote %1").arg(outPath);
    }
};

QTEST_GUILESS_MAIN(LayerPaintTest)
#include "layer_paint_test.moc"
