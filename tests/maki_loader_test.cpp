// M2 — sanity test for the opensourced Maki VM compiled into libwasabiqt.
//
// We don't run scripts here; we just verify the VM links and accepts
// a real .maki blob via VCPU::addScript() (forwarded through the
// WasabiQt::Maki bridge) without crashing.  Real script dispatch
// needs the script bindings (M3+).

#include "../wasabi-port/maki-bridge.h"

#include <QFile>
#include <QObject>
#include <QtTest/QtTest>

using WasabiQt::Maki::addScript;
using WasabiQt::Maki::removeScript;
using WasabiQt::Maki::scriptCount;

namespace {
QByteArray readBlob(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
} // namespace

class MakiLoaderTest : public QObject {
    Q_OBJECT
private slots:

    void linksVcpuClass() {
        QCOMPARE(scriptCount(), 0);
    }

    void addScriptAcceptsBlob() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/scripts/eq.maki");
        QByteArray blob = readBlob(p);
        if (blob.isEmpty()) QSKIP("no Winamp Modern eq.maki available");

        int id = addScript(blob.data(), blob.size(), 0);
        QVERIFY2(id >= 0, "VCPU::addScript rejected a real .maki blob");

        removeScript(id);
    }
};

QTEST_GUILESS_MAIN(MakiLoaderTest)
#include "maki_loader_test.moc"
