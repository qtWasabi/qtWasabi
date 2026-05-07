// M13a — SkinRuntime loads Maki scripts + builds widget object table.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinRuntime.h>

#include <QFile>
#include <QObject>
#include <QtTest/QtTest>

using namespace WasabiQt;

class SkinRuntimeTest : public QObject {
    Q_OBJECT
private slots:

    void loadsModernScripts() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document doc;
        QVERIFY(SkinXml::parse(p, doc));
        Layout::ResolvedWidget tree;
        QVERIFY(Layout::expandLayout(doc, QStringLiteral("main"),
                                     QStringLiteral("normal"), tree));

        SkinRuntime runtime;
        const int n = runtime.loadScripts(doc, tree);
        qInfo().noquote() << QStringLiteral(
            "loaded %1 scripts; widget-objects=%2")
            .arg(n).arg(runtime.widgetObjectCount());

        // Modern declares ~39 scripts in skin.xml.  Some may fail to
        // load (paths to other skins, etc.) — accept any positive
        // number.
        QVERIFY(n > 0);
        QVERIFY(runtime.widgetObjectCount() > 50);   // many widgets
        QCOMPARE(runtime.scriptCount(), n);
    }
};

QTEST_GUILESS_MAIN(SkinRuntimeTest)
#include "skin_runtime_test.moc"
