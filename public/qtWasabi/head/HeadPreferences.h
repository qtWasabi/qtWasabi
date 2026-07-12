// HeadPreferences — the framework's Preferences dialog (V5e).
//
// Two framework-owned pages:
//   - Connection: the backend picker.  Desktop default is the local
//     player (unix socket); remote players are addable with a name,
//     a graphql+http(s):// or graphql+unix:// URL and an optional
//     bearer token.  Entries persist in the head's settings file;
//     applying a switch goes through HeadWindow::connectToBackend
//     (heads without live-switch persist the choice for the next
//     start — orchestrated live-switching lands in V6).
//   - Presentation: head-local state per the sync model (never
//     synced between heads): visualization mode, time display,
//     colour theme.
//
// Player-contributed pages arrive later through the GraphQL
// uiExtensions contract; in-process embedders can already append
// pages via HeadWindow::contributePrefPages.
//
// The dialog is themed with themedDialogStyle and re-tints on colour
// theme switches like every head dialog (restyleOpenChrome sweep).
#pragma once

#include <QDialog>
#include <QWidget>
#include <QList>
#include <QString>

class QListWidget;
class QStackedWidget;

namespace qtWasabi::head {

class HeadWindow;

// One persisted backend entry ([backends] array in the settings file).
struct BackendEntry {
    QString name;
    QString url;    // graphql+unix:///path or graphql+http(s)://host
    QString token;  // optional bearer token for non-loopback TCP
};

// The backend picker as a self-contained page, so an embedder with its
// own Preferences dialog can host the SAME Connection page the
// framework dialog shows (qtamp appends it to its dialog tree).
class ConnectionPage : public QWidget {
    Q_OBJECT
public:
    explicit ConnectionPage(HeadWindow *head, QWidget *parent = nullptr);

private:
    void refresh();

    HeadWindow *m_head;
    QListWidget *m_list = nullptr;
    QList<BackendEntry> m_backends;
};

class HeadPreferences : public QDialog {
    Q_OBJECT
public:
    explicit HeadPreferences(HeadWindow *head, QWidget *parent = nullptr);

    // Settings-file persistence for the backend list + active choice.
    static QList<BackendEntry> loadBackends(const QString &settingsFile);
    static void saveBackends(const QString &settingsFile,
                             const QList<BackendEntry> &entries);
    static QString activeBackend(const QString &settingsFile);
    static void setActiveBackend(const QString &settingsFile,
                                 const QString &name);

    // Append an embedder page (contributePrefPages hook).
    void addPage(const QString &title, QWidget *page);

    // Headless gate: print the page/widget structure as stable
    // [prefs-dump] lines (dialogs are invisible to the pixel suite).
    void dumpStructure() const;

private:
    void buildPresentationPage();

    HeadWindow *m_head;
    QListWidget *m_pageList = nullptr;
    QStackedWidget *m_pages = nullptr;
};

}  // namespace qtWasabi::head
