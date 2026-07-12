// HeadPreferences (V5e): page structure, backend persistence, and the
// presentation controls driving head-local state.  Dialogs are
// invisible to the pixel suite — this is their gate.
#include <QApplication>
#include <QComboBox>
#include <QListWidget>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

#include <qtWasabi/FakeHost.h>
#include <qtWasabi/head/HeadPreferences.h>
#include <qtWasabi/head/HeadWindow.h>

using qtWasabi::head::BackendEntry;
using qtWasabi::head::HeadPreferences;
using qtWasabi::head::HeadWindow;

static int failures = 0;
static void check(bool ok, const char *label) {
    printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
    if (!ok) ++failures;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString conf = tmp.filePath("head.ini");

    // Backend persistence round-trips through the settings file.
    HeadPreferences::saveBackends(
        conf, {{"Netcup", "graphql+https://player.example", "sekrit"},
               {"LAN", "graphql+http://10.0.0.7:8788", ""}});
    const auto loaded = HeadPreferences::loadBackends(conf);
    check(loaded.size() == 2 && loaded[0].name == "Netcup" &&
              loaded[0].token == "sekrit" && loaded[1].token.isEmpty(),
          "backend entries round-trip");
    HeadPreferences::setActiveBackend(conf, "Netcup");
    check(HeadPreferences::activeBackend(conf) == "Netcup",
          "active choice persists");
    HeadPreferences::setActiveBackend(conf, QString());
    check(HeadPreferences::activeBackend(conf).isEmpty(),
          "local default = empty active");

    qtWasabi::FakeHost host;
    HeadWindow head(&host);
    head.setSettingsFile(conf);

    HeadPreferences dlg(&head);
    auto *pages = dlg.findChild<QListWidget *>("prefs.pages");
    check(pages && pages->count() == 2 &&
              pages->item(0)->text() == "Connection" &&
              pages->item(1)->text() == "Presentation",
          "framework pages: Connection + Presentation");

    auto *backends = dlg.findChild<QListWidget *>("prefs.backends");
    check(backends && backends->count() == 3 &&
              backends->item(0)->text().contains("Local player") &&
              backends->item(1)->text().contains("[token]"),
          "backend list: fixed local default + stored entries, "
          "token flagged");

    // Presentation controls drive head-local state + persistence.
    auto *vis2 = dlg.findChild<QRadioButton *>("prefs.vis.2");
    check(vis2 && !vis2->isChecked(), "vis radios reflect visMode");
    vis2->setChecked(true);
    check(head.visMode() == 2, "vis radio drives setVisMode");
    check(QSettings(conf, QSettings::IniFormat)
                  .value("visualization/mode")
                  .toInt() == 2,
          "vis mode persisted to the settings file");

    auto *remaining = dlg.findChild<QRadioButton *>("prefs.time.remaining");
    remaining->setChecked(true);
    check(head.timeDisplayMode() == 2, "time radio drives the shared slot");

    // The structure dump (headless gate format) emits begin/end.
    dlg.dumpStructure();

    printf(failures == 0 ? "PASS\n" : "FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
