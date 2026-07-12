// HeadMenu — the declarative surface embedders use to extend the
// head's menus (V5d).
//
// The framework owns the Winamp-parity menu SKELETON (context menu +
// the WA5:* menu-bar popups); an embedder contributes items at named
// anchors through HeadWindow::contributeMenu and receives its action
// ids back through HeadWindow::handleMenuAction.  The same anchor
// vocabulary serves the GraphQL uiExtensions contract, so in-process
// and remote/declarative contributions share one namespace.
//
// Ids: framework items use `wa5:` (wa5:play.files, wa5:vis.2,
// wa5:help.about, ...); embedder items use an app prefix
// (`qtamp:recent:<path>`).  handleMenuAction gets first refusal on
// every id, so an embedder may also take over a framework item
// (qtamp handles wa5:help.about with its own About dialog).
#pragma once

#include <QList>
#include <QString>

class QMenu;

namespace qtWasabi::head {

// One contributed menu entry.  Non-empty `children` makes a submenu
// (the id of a submenu itself is ignored — only leaves dispatch); an
// id of "-" is a separator.
// Enabled/checkable/checked are BUILD-TIME snapshots: menus are
// rebuilt on every popup, there is no live-update channel.
struct MenuItem {
    QString id;
    QString label;          // may embed "\t<accel>" column
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    QList<MenuItem> children;
};

// Wraps a QMenu insertion point handed to contributeMenu.  Items are
// appended in call order at the anchor; the skeleton's own entries
// around the anchor are fixed.
class MenuBuilder {
public:
    MenuBuilder(QMenu *menu, const QString &menuStyle)
        : m_menu(menu), m_style(menuStyle) {}

    void addItem(const MenuItem &item);
    void addSeparator();

private:
    QMenu *m_menu;
    QString m_style;
};

}  // namespace qtWasabi::head
