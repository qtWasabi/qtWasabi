// M3 — skin XML parser tests.
//
// Parse a few real Modern-family skins end-to-end and verify we walk
// the entire <include> tree.  We don't validate every individual
// element here — just that the parser doesn't drop pieces and
// extracts the metadata accessible from the top.

#include <WasabiQt/SkinXml.h>

#include <QDir>
#include <QObject>
#include <QtTest/QtTest>

using namespace WasabiQt::SkinXml;

namespace {
int countTagsRecursive(const Element &e, const QString &tag) {
    int n = (e.tag == tag) ? 1 : 0;
    for (const auto &c : e.children) n += countTagsRecursive(c, tag);
    return n;
}
}

class SkinXmlTest : public QObject {
    Q_OBJECT
private slots:

    void parsesWinampModern() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        Document d; QString err;
        QVERIFY2(parse(p, d, &err), qPrintable(err));
        QCOMPARE(d.skinName, QStringLiteral("Winamp5 Base Skin"));
        qInfo().noquote() << QStringLiteral(
            "Modern: %1 elements, %2 files, %3 scripts, %4 warnings")
            .arg(d.elementCount).arg(d.includesResolved)
            .arg(d.scriptFiles.size()).arg(d.warnings.size());
        for (const auto &w : d.warnings) qWarning().noquote() << " " << w;

        QCOMPARE(d.warnings.size(), 0);
        QVERIFY(d.includesResolved >= 5);
        QVERIFY(d.elementCount >= 3000);    // Modern has ~3500 elements
        QVERIFY(!d.scriptFiles.isEmpty());

        // Spot-check that the major widget kinds all parsed.
        for (auto tag : {"group", "layer", "button", "slider",
                         "container", "layout", "groupdef"}) {
            QVERIFY2(countTagsRecursive(d.root, QString::fromLatin1(tag)) > 0,
                     qPrintable(QStringLiteral("missing tag '%1'").arg(tag)));
        }
    }

    void parsesBento() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Bento/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Bento skin available");

        Document d; QString err;
        QVERIFY2(parse(p, d, &err), qPrintable(err));
        QCOMPARE(d.skinName, QStringLiteral("Bento"));
        QVERIFY(d.elementCount > 0);

        qDebug() << "Bento:" << d.elementCount << "elements,"
                 << d.includesResolved << "files,"
                 << d.scriptFiles.size() << "scripts";
    }

    void parsesBigBento() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Big Bento/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Big Bento skin available");

        Document d; QString err;
        QVERIFY2(parse(p, d, &err), qPrintable(err));
        QVERIFY(d.elementCount > 0);

        qDebug() << "Big Bento:" << d.elementCount << "elements,"
                 << d.includesResolved << "files";
    }

    void rejectsNonExistentFile() {
        Document d; QString err;
        QVERIFY(!parse(QStringLiteral("/dev/null/no-such-file.xml"), d, &err));
        QVERIFY(!err.isEmpty());
    }

    void detectsIncludeCycle() {
        // Synthesise a minimal cycle in /tmp and check it's caught
        // (parse succeeds with a warning, since we don't fail-fast).
        const QString a = QDir::temp().filePath("wasabiqt_cycle_a.xml");
        const QString b = QDir::temp().filePath("wasabiqt_cycle_b.xml");
        QFile fa(a), fb(b);
        QVERIFY(fa.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fa.write("<?xml version=\"1.0\"?><WasabiXML>"
                 "<include file=\"wasabiqt_cycle_b.xml\"/>"
                 "</WasabiXML>");
        fa.close();
        QVERIFY(fb.open(QIODevice::WriteOnly | QIODevice::Truncate));
        fb.write("<?xml version=\"1.0\"?><WasabiXML>"
                 "<include file=\"wasabiqt_cycle_a.xml\"/>"
                 "</WasabiXML>");
        fb.close();

        Document d; QString err;
        QVERIFY(parse(a, d));
        QVERIFY(!d.warnings.isEmpty());

        QFile::remove(a);
        QFile::remove(b);
    }
};

QTEST_GUILESS_MAIN(SkinXmlTest)
#include "skin_xml_test.moc"
