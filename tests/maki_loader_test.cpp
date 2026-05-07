// Maki loader tests — exercise the parser against real .maki blobs
// shipped with Winamp Modern.
//
// We pick a few scripts of varying complexity (small / medium / large)
// to confirm the loader doesn't choke on real-world variation.

#include <WasabiQt/Maki.h>
#include "../maki/Vcpu.h"

#include <QFile>
#include <QObject>
#include <QtTest/QtTest>

using namespace wasabiq::maki;

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

    void parsesValidV4Header() {
        // We need a .maki to be available.  Skip if the shipped skin
        // tree isn't installed in this environment.
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/scripts/eq.maki");
        QByteArray blob = readBlob(p);
        if (blob.isEmpty()) QSKIP("no Winamp Modern eq.maki available");

        Script s; QString err;
        QVERIFY2(loadScript(blob, s, &err), qPrintable(err));
        QCOMPARE(s.version, HeaderVersion::V4);
        QVERIFY(!s.typeTable.isEmpty());
        QVERIFY(!s.dlfTable.isEmpty());
        QVERIFY(!s.code.isEmpty());
    }

    void rejectsBogusHeader() {
        QByteArray bogus("not a maki blob at all");
        Script s; QString err;
        QVERIFY(!loadScript(bogus, s, &err));
        QVERIFY(!err.isEmpty());
    }

    void rejectsTruncated() {
        QByteArray hdr; hdr.append("FG\x03\x04\x17\x00\x00\x00", 8);
        // No type-count after — should fail at "type table count truncated".
        Script s; QString err;
        QVERIFY(!loadScript(hdr, s, &err));
    }

    void parsesAllShippedScripts() {
        const QString skinDir = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/scripts");
        QDir dir(skinDir);
        if (!dir.exists()) QSKIP("Winamp Modern scripts dir missing");

        const QStringList files = dir.entryList({"*.maki"}, QDir::Files);
        QVERIFY(!files.isEmpty());
        int parsed = 0, failed = 0;
        for (const QString &f : files) {
            QByteArray blob = readBlob(dir.filePath(f));
            if (blob.isEmpty()) continue;
            Script s; QString err;
            if (loadScript(blob, s, &err)) {
                parsed++;
            } else {
                qWarning().noquote() << f << "→" << err;
                failed++;
            }
        }
        qDebug() << "parsed" << parsed << "failed" << failed;
        // Expect every shipped script to parse — any failure is a
        // real loader bug.
        QCOMPARE(failed, 0);
    }

    void parsesBentoAndBigBento() {
        const QStringList skinDirs = {
            QStringLiteral("/home/snekmin/.winamp/skins/Bento/scripts"),
            QStringLiteral("/home/snekmin/.winamp/skins/Big Bento/scripts"),
        };
        int totalParsed = 0, totalFailed = 0;
        for (const QString &skinDir : skinDirs) {
            QDir dir(skinDir);
            if (!dir.exists()) continue;
            for (const QString &f : dir.entryList({"*.maki"}, QDir::Files)) {
                QByteArray blob = readBlob(dir.filePath(f));
                if (blob.isEmpty()) continue;
                Script s; QString err;
                if (loadScript(blob, s, &err)) totalParsed++;
                else { qWarning().noquote() << skinDir << f << "→" << err; totalFailed++; }
            }
        }
        qDebug() << "Bento parsed" << totalParsed << "failed" << totalFailed;
        QCOMPARE(totalFailed, 0);
    }

    void vmAcceptsLoadedScript() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/scripts/eq.maki");
        QByteArray blob = readBlob(p);
        if (blob.isEmpty()) QSKIP("no eq.maki");
        Script s;
        QVERIFY(loadScript(blob, s));
        VM vm;
        int id = vm.addScript(s);
        QCOMPARE(id, 0);
        // dispatchEvent should not crash on a non-existent (var,dlf)
        // combination — it just returns false.
        QCOMPARE(vm.dispatchEvent(0, 9999, 9999), false);
    }
};

QTEST_GUILESS_MAIN(MakiLoaderTest)
#include "maki_loader_test.moc"
