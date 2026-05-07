// M10 — TextPainter renders bitmap-font text widgets.
//
// Loads Winamp Modern, expands main/normal, renders the layout with
// a stub display resolver returning known time/bitrate/frequency
// strings, then samples the canvas at the timer's expected location
// and verifies non-background pixels show up where the digits should
// be.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/TreePainter.h>

#include <QFile>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QtTest/QtTest>

using namespace WasabiQt;

class TextPaintTest : public QObject {
    Q_OBJECT
private slots:

    void registryFindsBitmapFonts() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document doc;
        QVERIFY(SkinXml::parse(p, doc));
        FontRegistry fonts;
        const int n = fonts.loadFromDocument(doc);
        QVERIFY2(n >= 5, qPrintable(QStringLiteral(
            "Modern declares 6 bitmapfonts; got %1").arg(n)));

        const auto *bn = fonts.find(QStringLiteral("player.BIGNUM"));
        QVERIFY(bn != nullptr);
        QCOMPARE(bn->charWidth,  13);
        QCOMPARE(bn->charHeight, 20);
    }

    void glyphCoordMatchesUpstream() {
        // Sanity: BIGNUM digits live on row 1 of the source bitmap
        // (y = charHeight = 20), columns 0-9.
        for (int i = 0; i < 10; ++i) {
            const QPoint p = FontRegistry::glyphCoord(
                QChar(QLatin1Char('0' + i)), 13, 20);
            QCOMPARE(p, QPoint(i * 13, 20));
        }
        // Letters live on row 0.
        QCOMPARE(FontRegistry::glyphCoord(QChar('A'), 13, 20), QPoint(0,  0));
        QCOMPARE(FontRegistry::glyphCoord(QChar('Z'), 13, 20), QPoint(25 * 13, 0));
        // Lowercase folds in.
        QCOMPARE(FontRegistry::glyphCoord(QChar('a'), 13, 20), QPoint(0,  0));
    }

    void timerDigitsAppearOnCanvas() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document doc;
        QVERIFY(SkinXml::parse(p, doc));
        Layout::ResolvedWidget tree;
        QVERIFY(Layout::expandLayout(doc,
                QStringLiteral("main"), QStringLiteral("normal"), tree));
        BitmapRegistry reg;  reg.loadFromDocument(doc);
        FontRegistry fonts;  fonts.loadFromDocument(doc);

        const QSize canvas(354, 280);
        QImage out(canvas, QImage::Format_ARGB32);
        out.fill(Qt::transparent);
        QPainter painter(&out);
        TreePainter::DisplayResolver resolver = [](const QString &k) {
            if (k == QStringLiteral("time")) return QStringLiteral("00:42");
            return QString();
        };
        TreePainter::paintTree(&painter, tree, reg, fonts, canvas, resolver);
        painter.end();

        // The timer occupies roughly (33-103, 36-56) in canvas coords.
        // Count pale pixels (the digit glyphs) — should be > 0 if the
        // text painter wrote them.
        int pale = 0;
        for (int y = 36; y < 56; ++y) {
            for (int x = 33; x < 103; ++x) {
                const QRgb c = out.pixel(x, y);
                if (qRed(c) > 180 && qGreen(c) > 180 && qBlue(c) > 180)
                    ++pale;
            }
        }
        qInfo() << "pale pixels in timer zone:" << pale;
        QVERIFY2(pale > 30, "no bitmap-font glyphs in timer zone");
    }
};

QTEST_MAIN(TextPaintTest)
#include "text_paint_test.moc"
