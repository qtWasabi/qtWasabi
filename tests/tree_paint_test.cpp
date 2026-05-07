// M7 — paint a resolved widget tree onto a QImage.
//
// Loads Modern's main/normal layout, expands it, paints it at the
// layout's declared minimum size onto a transparent canvas, and
// verifies a sizeable fraction of the canvas got drawn on.  Saves
// the artefact to /tmp for visual inspection.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/TreePainter.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QtTest/QtTest>

using namespace WasabiQt;

namespace {
int countOpaquePixels(const QImage &img) {
    int n = 0;
    const QImage a = img.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            if (qAlpha(a.pixel(x, y)) > 0) ++n;
        }
    }
    return n;
}
}

class TreePaintTest : public QObject {
    Q_OBJECT
private slots:

    void paintsModernMain() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document doc;
        QVERIFY(SkinXml::parse(p, doc));

        Layout::ResolvedWidget tree;
        QString err;
        QVERIFY2(Layout::expandLayout(doc, QStringLiteral("main"),
                                      QStringLiteral("normal"), tree, &err),
                 qPrintable(err));

        BitmapRegistry reg;
        reg.loadFromDocument(doc);
        FontRegistry fonts;
        fonts.loadFromDocument(doc);

        // Modern's main layout declares minimum_w=354 minimum_h=280.
        const QSize canvas(354, 280);
        QImage out(canvas, QImage::Format_ARGB32);
        out.fill(Qt::transparent);
        {
            QPainter painter(&out);
            TreePainter::DisplayResolver resolver = [](const QString &k) {
                if (k == QStringLiteral("time"))      return QStringLiteral("00:42");
                if (k == QStringLiteral("Bitrate"))   return QStringLiteral("128");
                if (k == QStringLiteral("Frequency")) return QStringLiteral("44");
                return QString();
            };
            TreePainter::paintTree(&painter, tree, reg, fonts,
                                    canvas, resolver);
        }

        const int painted = countOpaquePixels(out);
        const int total   = canvas.width() * canvas.height();
        const double pct  = 100.0 * painted / total;
        qInfo().noquote() << QStringLiteral(
            "Modern main/normal: %1/%2 px painted (%3%)")
            .arg(painted).arg(total).arg(pct, 0, 'f', 1);

        QVERIFY2(pct > 5.0,
                 qPrintable(QStringLiteral("only %1% painted").arg(pct)));

        const QString outPath = QDir::temp().filePath(
            QStringLiteral("wasabiqt-m7-modern-main.png"));
        QVERIFY(out.save(outPath));
        qInfo().noquote() << QStringLiteral("Wrote %1").arg(outPath);
    }
};

QTEST_MAIN(TreePaintTest)
#include "tree_paint_test.moc"
