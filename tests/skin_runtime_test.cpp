// M13a — SkinRuntime loads Maki scripts + builds widget object table.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinRuntime.h>

#include "../wasabi-port/maki-bridge.h"

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

        // Dump the DLF names of the first script for diagnostics.
        char buf[8192];
        const int count = WasabiQt::Maki::dumpDlfNames(0, buf, sizeof(buf));
        QVERIFY2(count > 0, "script 0 has no DLFs");
        const QString names = QString::fromUtf8(buf);
        // Every Modern script that uses System.* has an onScriptLoaded
        // handler — at least one of them should appear in script 0's
        // DLF table.  Verifies that addScript actually populated the
        // DLF entries and our fireEventByName lookup is sound.
        QVERIFY2(names.contains(QStringLiteral("onScriptLoaded")),
                 qPrintable(QStringLiteral("missing onScriptLoaded in:\n%1")
                                .arg(names)));
    }
};

QTEST_GUILESS_MAIN(SkinRuntimeTest)
#include "skin_runtime_test.moc"
