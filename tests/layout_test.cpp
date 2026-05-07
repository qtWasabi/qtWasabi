// M6 — layout expansion: groupdef instantiation + sendparams.

#include <WasabiQt/Layout.h>
#include <WasabiQt/SkinXml.h>

#include <QFile>
#include <QObject>
#include <QtTest/QtTest>

using namespace WasabiQt;
using namespace WasabiQt::Layout;

namespace {

int countTags(const ResolvedWidget &w, const QString &tag) {
    int n = (w.tag == tag) ? 1 : 0;
    for (const auto &c : w.children) n += countTags(c, tag);
    return n;
}
int totalNodes(const ResolvedWidget &w) {
    int n = 1;
    for (const auto &c : w.children) n += totalNodes(c);
    return n;
}
const ResolvedWidget *findById(const ResolvedWidget &w, const QString &id) {
    if (w.id == id) return &w;
    for (const auto &c : w.children)
        if (auto *r = findById(c, id)) return r;
    return nullptr;
}

}  // namespace

class LayoutTest : public QObject {
    Q_OBJECT
private slots:

    void enumeratesContainers() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document d;
        QVERIFY(SkinXml::parse(p, d));
        const QStringList ids = containerIds(d);
        qInfo().noquote() << "Containers:" << ids.join(QStringLiteral(", "));
        QVERIFY(ids.contains(QStringLiteral("main")));
        QVERIFY(ids.size() >= 5);   // main + playlist + ml + vid + albumart...
    }

    void expandsMainNormal() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document d;
        QVERIFY(SkinXml::parse(p, d));

        ResolvedWidget tree;
        QString err;
        QVERIFY2(expandLayout(d, QStringLiteral("main"),
                              QStringLiteral("normal"), tree, &err),
                 qPrintable(err));

        const int total = totalNodes(tree);
        const int layers  = countTags(tree, QStringLiteral("layer"));
        const int buttons = countTags(tree, QStringLiteral("button"));
        const int groups  = countTags(tree, QStringLiteral("group"));
        const int sliders = countTags(tree, QStringLiteral("slider"));
        qInfo().noquote() << QStringLiteral(
            "main/normal expanded: %1 nodes, %2 layers, %3 buttons, "
            "%4 groups, %5 sliders")
            .arg(total).arg(layers).arg(buttons).arg(groups).arg(sliders);

        QVERIFY(total   > 50);     // Modern's main layout is busy
        QVERIFY(layers  > 5);
        QVERIFY(buttons > 5);
    }

    void instantiatesGroupdefAndAppliesSendparams() {
        // The titlebar groupdef gets instantiated as left + right
        // streaks with different sendparam overrides.  After
        // expansion, both instances should exist and the right
        // streak's center.active layer should have its `w` overridden
        // to "-20" by the sendparam.
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document d;
        QVERIFY(SkinXml::parse(p, d));

        ResolvedWidget tree;
        QVERIFY(expandLayout(d, QStringLiteral("main"),
                             QStringLiteral("normal"), tree));

        // The titlebar group is itself an instantiation.  Find it.
        const auto *streakLeft  = findById(tree, QStringLiteral("wasabi.titlebar.streak"));
        QVERIFY2(streakLeft != nullptr,
                 "titlebar streak group missing from main/normal");

        // Inside, the groupdef expanded to layers including
        // titlebar.center.active.  After sendparams, its w must be -20.
        const auto *center =
            findById(*streakLeft, QStringLiteral("titlebar.center.active"));
        QVERIFY2(center != nullptr,
                 "titlebar.center.active not in expanded streak");
        QCOMPARE(center->attrs.value(QStringLiteral("w")),
                 QStringLiteral("-20"));
    }

    void rejectsUnknownContainer() {
        const QString p = QStringLiteral(
            "/home/snekmin/.winamp/skins/Winamp Modern/skin.xml");
        if (!QFile::exists(p)) QSKIP("no Winamp Modern skin available");

        SkinXml::Document d;
        QVERIFY(SkinXml::parse(p, d));

        ResolvedWidget tree; QString err;
        QVERIFY(!expandLayout(d, QStringLiteral("nope-no-such"),
                              QStringLiteral("normal"), tree, &err));
        QVERIFY(!err.isEmpty());
    }
};

QTEST_GUILESS_MAIN(LayoutTest)
#include "layout_test.moc"
