// HeadChrome — skin-derived chrome styling for the Qt widgets a head
// puts around the engine: popup/context menus, dialogs (Preferences),
// and the Wayland popup-grab dance.  Free functions over the engine's
// public registries; every head (qtamp, the reference head app) builds
// its QSS through these so chrome re-tints with the active colour
// theme exactly like the skin.
#pragma once

#include <QColor>
#include <QString>

class QMenu;
class QWindow;

namespace qtWasabi {
class ColorRegistry;
class GammasetRegistry;
}

namespace qtWasabi::head {

// Shared menu QSS builder (popup menus + context menu use the same
// colours).  `sel` names the widget the rules apply to so a nested
// QMenu inside a styled QDialog can reuse it.
QString menuStyleFor(const GammasetRegistry &gammasets,
                     const ColorRegistry &colors, const QString &sel);

inline QString themedMenuStyle(const GammasetRegistry &gammasets,
                               const ColorRegistry &colors) {
    return menuStyleFor(gammasets, colors, QStringLiteral("QMenu"));
}

// Build the dialog QSS from a resolved set of role colours (shared by
// the skin-derived path and the synthetic-chrome path).
QString dialogQss(QColor windowBg, QColor windowText, QColor fieldBg,
                  QColor selbg, QColor seltxt, QColor border,
                  QColor buttonBg, QColor fieldText, QColor buttonText);

// A QSS stylesheet for the head's own dialogs (Preferences, etc.),
// derived from the active skin's wa_dlg palette so they match the
// skin (and re-tint with the colour theme).
QString themedDialogStyle(const GammasetRegistry &gammasets,
                          const ColorRegistry &colors);

// Re-style every open themed dialog/menu after a colour-theme switch.
// General by construction: every QDialog / QMenu a head creates is
// themed, so a single sweep of the top-level widgets (and their
// nested submenus) keeps them all in sync with the active theme.
void restyleOpenChrome(const QString &dialogStyle,
                       const QString &menuStyle);

// Print a menu tree (labels, separators, enabled/checkable/checked,
// nesting) to stderr in a stable line format — the headless gate for
// menu work: Qt menus are invisible to the pixel suite, so old-vs-new
// extraction diffs run on this dump instead.
void dumpMenuTree(const QMenu &menu, const QString &tag);

// Wayland will only create a *grabbing* popup (one that holds keyboard
// focus) if the popup declares a transient parent and that parent
// recently held input.  A head's main window is a QQuickWindow with no
// QWidget ancestor for a QMenu to inherit from, so a parentless menu
// fails to grab — and without the grab the menu gets no keyboard
// focus.  Realise the menu's platform window and point it at `parent`
// (falls back to the focused window when null).
void prepareMenuForWayland(QMenu &menu, QWindow *parent);

}  // namespace qtWasabi::head
