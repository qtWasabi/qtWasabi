// HeadChrome implementation — moved verbatim from the reference
// embedder's window layer (V5a); the QSS strings are byte-identical.
#include <qtWasabi/head/HeadChrome.h>

#include <QApplication>
#include <QDialog>
#include <QGuiApplication>
#include <QMenu>
#include <QWindow>

#include <cstdio>
#include <cstdlib>

#include <qtWasabi/ColorRegistry.h>
#include <qtWasabi/GammasetRegistry.h>

namespace qtWasabi::head {

QString menuStyleFor(const GammasetRegistry &gammasets,
                     const ColorRegistry &colors, const QString &sel) {
    // A synthetic theme ships an absolute chrome palette (the skin's own
    // colours carry no gammagroup, so they can't be tinted) — follow it.
    if (const qtWasabi::Gammaset *a = gammasets.active();
        a && a->hasChrome) {
        return QStringLiteral(
            "%8 { background-color:%1; color:%2; border:1px solid %3; font-size:9pt; }"
            "%8::item:selected { background-color:%4; color:%5; }"
            "%8::item:checked { font-weight:bold; }"
            "%8::item:disabled { color:%6; }"
            "%8::separator { height:1px; background:%7; margin:2px 4px; }")
            .arg(a->chromeBg.name(), a->chromeFieldText.name(),
                 a->chromeBorder.name(), a->chromeSelBg.name(),
                 a->chromeSelText.name(), a->chromeFieldText.darker(180).name(),
                 a->chromeBorder.name(), sel);
    }
    auto pick = [&](std::initializer_list<const char *> keys,
                    QColor base) -> QString {
        for (const char *k : keys) {
            const QColor r = colors.resolve(QString::fromLatin1(k),
                                            &gammasets, QColor());
            if (r.isValid()) return r.name();
        }
        return base.name();
    };
    // bg/text follow the window (WNDBG/WNDFG); selection follows the
    // list highlight; frame/inactive follow the dimmed button text.
    const QString bg = pick({"wasabi.popupmenu.background",
                             "wasabi.window.background",
                             "color.display.bg"}, QColor(0x38, 0x37, 0x57));
    const QString text = pick({"wasabi.popupmenu.text",
                               "wasabi.window.text",
                               "color.display"}, QColor(0xFF, 0xFF, 0xFF));
    const QString frame = pick({"wasabi.popupmenu.frame",
                                "wasabi.button.dimmedText",
                                "wasabi.window.text"},
                               QColor(0x75, 0x74, 0x8B));
    const QString selbg = pick({"wasabi.popupmenu.background.selected",
                                "wasabi.list.text.selected.background",
                                "color.selected.active.bg"},
                               QColor(0x75, 0x74, 0x8B));
    const QString seltxt = pick({"wasabi.popupmenu.text.selected",
                                 "wasabi.list.text.selected",
                                 "wasabi.window.text"},
                                QColor(0xFF, 0xFF, 0xFF));
    const QString dim = pick({"wasabi.popupmenu.text.inactive",
                              "wasabi.button.dimmedText"},
                             QColor(0x73, 0x73, 0x89));
    const QString sep = pick({"wasabi.popupmenu.separator",
                              "wasabi.button.dimmedText"},
                             QColor(0x75, 0x74, 0x8B));
    return QStringLiteral(
        "%8 { background-color:%1; color:%2; border:1px solid %3; font-size:9pt; }"
        "%8::item:selected { background-color:%4; color:%5; }"
        "%8::item:checked { font-weight:bold; }"
        "%8::item:disabled { color:%6; }"
        "%8::separator { height:1px; background:%7; margin:2px 4px; }")
        .arg(bg, text, frame, selbg, seltxt, dim, sep, sel);
}

QString dialogQss(QColor windowBg, QColor windowText, QColor fieldBg,
                  QColor selbg, QColor seltxt, QColor border,
                  QColor buttonBg, QColor fieldText, QColor buttonText) {
    return QStringLiteral(
        "QDialog, QWidget { background-color:%1; color:%2; }"
        "QLineEdit, QListWidget, QListView, QTreeView, QComboBox, QSpinBox,"
        " QPlainTextEdit, QTextEdit, QAbstractScrollArea"
        " { background-color:%3; color:%8; border:1px solid %6; }"
        "QListWidget::item:selected, QListView::item:selected,"
        " QTreeView::item:selected { background-color:%4; color:%5; }"
        "QPushButton { background-color:%7; color:%9; border:1px solid %6;"
        " padding:3px 10px; }"
        "QPushButton:hover { background-color:%4; color:%5; }"
        "QTabBar::tab { background:%1; color:%2; padding:4px 10px;"
        " border:1px solid %6; }"
        "QTabBar::tab:selected { background:%4; color:%5; }"
        // Reserve room above the frame for the title and give it its own
        // position + padding.  Without this the group-box title overlaps
        // the border and the first row of content once the UI font is
        // large (macOS defaults to 13pt vs ~9pt on Windows/Linux).
        "QGroupBox { border:1px solid %6; margin-top:14px; padding-top:8px; }"
        "QGroupBox::title { subcontrol-origin:margin;"
        " subcontrol-position:top left; left:10px; padding:0 4px; }"
        "QCheckBox, QRadioButton, QLabel { background:transparent;"
        " color:%2; }")
        .arg(windowBg.name(), windowText.name(), fieldBg.name(),
             selbg.name(), seltxt.name(), border.name(),
             buttonBg.name(), fieldText.name(), buttonText.name());
}

QString themedDialogStyle(const GammasetRegistry &gammasets,
                          const ColorRegistry &colors) {
    // A synthetic theme supplies an absolute chrome palette (the skin's
    // colours can't be tinted) — use it so Preferences re-themes exactly
    // like a native Color Theme would.
    if (const qtWasabi::Gammaset *a = gammasets.active();
        a && a->hasChrome) {
        const QColor btnBg = a->chromeBorder.lighter(135);
        return dialogQss(a->chromeBg, a->chromeText, a->chromeField,
                         a->chromeSelBg, a->chromeSelText, a->chromeBorder,
                         btnBg, a->chromeFieldText, a->chromeButtonText);
    }
    auto pick = [&](std::initializer_list<const char *> keys,
                    QColor hard) -> QColor {
        for (const char *k : keys) {
            const QColor r = colors.resolve(QString::fromLatin1(k),
                                            &gammasets, QColor());
            if (r.isValid()) return r;
        }
        return hard;
    };
    // Map the wa_dlg roles a real Winamp dialog uses: the window
    // surface (WNDBG/WNDFG) for panels, labels and tabs; the list
    // surface (ITEMBG/ITEMFG = wasabi.list.*) for editable fields and
    // lists; the list highlight for selection; the dimmed button text
    // for frames.  Every one of these carries a gammagroup, so the
    // dialog re-tints with the active colour theme just like the skin.
    const QColor windowBg = pick({"wasabi.window.background",
                                  "color.display.bg"},
                                 QColor(0x2b, 0x2d, 0x3d));
    const QColor windowText = pick({"wasabi.window.text", "color.display"},
                                   QColor(0xdd, 0xdd, 0xdd));
    const QColor fieldBg = pick({"wasabi.list.background",
                                 "wasabi.edit.background"},
                                windowBg.lightness() < 128
                                    ? windowBg.lighter(135)
                                    : windowBg.darker(115));
    const QColor fieldText = pick({"wasabi.list.text", "wasabi.window.text",
                                   "color.display"}, windowText);
    const QColor selbg = pick({"wasabi.list.text.selected.background",
                               "color.selected.active.bg",
                               "studio.list.item.selected"},
                              QColor(0x31, 0x35, 0x40));
    const QColor seltxt = pick({"wasabi.list.text.selected",
                                "color.selected.active",
                                "wasabi.window.text"},
                               QColor(0xff, 0xff, 0xff));
    const QColor border = pick({"wasabi.button.dimmedText",
                                "wasabi.border.sunken"},
                               QColor(0x55, 0x55, 0x55));
    const QColor buttonText = pick({"wasabi.button.text", "wasabi.window.text"},
                                   windowText);
    // Buttons sit slightly raised off the window surface.
    const QColor buttonBg = windowBg.lightness() < 128
                                ? windowBg.lighter(140)
                                : windowBg.darker(112);
    return dialogQss(windowBg, windowText, fieldBg, selbg, seltxt,
                     border, buttonBg, fieldText, buttonText);
}

void restyleOpenChrome(const QString &dialogStyle,
                       const QString &menuStyle) {
    const auto tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (!w || !w->isVisible()) continue;
        if (qobject_cast<QDialog *>(w)) w->setStyleSheet(dialogStyle);
        else if (qobject_cast<QMenu *>(w)) w->setStyleSheet(menuStyle);
        const auto subs = w->findChildren<QMenu *>();
        for (QMenu *sub : subs) sub->setStyleSheet(menuStyle);
    }
}

void prepareMenuForWayland(QMenu &menu, QWindow *parent) {
    menu.ensurePolished();
    menu.winId();   // force-create the platform window so it has a handle
    QWindow *mw = menu.windowHandle();
    // Prefer the head's window; fall back to whatever window Qt currently
    // considers focused (the surface the compositor will tie the grab to).
    if (!parent) parent = QGuiApplication::focusWindow();
    if (mw && parent) mw->setTransientParent(parent);
    if (::getenv("WASABIQT_TRACE_MAKI"))
        fprintf(stderr,
                "[menuprep] handle=%p parent=%p focusWin=%p set=%d\n",
                (void *)mw, (void *)parent,
                (void *)QGuiApplication::focusWindow(),
                int(mw && parent));
}

}  // namespace qtWasabi::head
