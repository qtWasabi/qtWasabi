// HeadPreferences implementation (V5e).
#include <qtWasabi/head/HeadPreferences.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cstdio>

#include <qtWasabi/GammasetRegistry.h>
#include <qtWasabi/head/HeadWindow.h>

namespace qtWasabi::head {

// ── persistence ([connection]/[backends] in the head settings) ──────

QList<BackendEntry> HeadPreferences::loadBackends(
    const QString &settingsFile) {
    QList<BackendEntry> out;
    if (settingsFile.isEmpty()) return out;
    QSettings s(settingsFile, QSettings::IniFormat);
    const int n = s.beginReadArray(QStringLiteral("backends"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        BackendEntry e;
        e.name = s.value(QStringLiteral("name")).toString();
        e.url = s.value(QStringLiteral("url")).toString();
        e.token = s.value(QStringLiteral("token")).toString();
        if (!e.name.isEmpty()) out.append(e);
    }
    s.endArray();
    return out;
}

void HeadPreferences::saveBackends(const QString &settingsFile,
                                   const QList<BackendEntry> &entries) {
    if (settingsFile.isEmpty()) return;
    QSettings s(settingsFile, QSettings::IniFormat);
    // Scrub first: beginWriteArray only rewrites size + live indexes,
    // so a removed entry's token would otherwise linger on disk.
    s.remove(QStringLiteral("backends"));
    s.beginWriteArray(QStringLiteral("backends"), entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("name"), entries[i].name);
        s.setValue(QStringLiteral("url"), entries[i].url);
        s.setValue(QStringLiteral("token"), entries[i].token);
    }
    s.endArray();
}

QString HeadPreferences::activeBackend(const QString &settingsFile) {
    if (settingsFile.isEmpty()) return QString();
    return QSettings(settingsFile, QSettings::IniFormat)
        .value(QStringLiteral("connection/active"))
        .toString();
}

void HeadPreferences::setActiveBackend(const QString &settingsFile,
                                       const QString &name) {
    if (settingsFile.isEmpty()) return;
    QSettings(settingsFile, QSettings::IniFormat)
        .setValue(QStringLiteral("connection/active"), name);
}

// ── dialog ───────────────────────────────────────────────────────────

HeadPreferences::HeadPreferences(HeadWindow *head, QWidget *parent)
    : QDialog(parent), m_head(head) {
    setWindowTitle(QStringLiteral("Preferences"));
    setObjectName(QStringLiteral("HeadPreferences"));
    resize(560, 380);
    setStyleSheet(m_head->themedDialogStyle());

    auto *rootLay = new QHBoxLayout(this);
    m_pageList = new QListWidget(this);
    m_pageList->setObjectName(QStringLiteral("prefs.pages"));
    m_pageList->setMaximumWidth(150);
    m_pages = new QStackedWidget(this);
    rootLay->addWidget(m_pageList);
    rootLay->addWidget(m_pages, 1);

    addPage(QStringLiteral("Connection"), new ConnectionPage(head, this));
    buildPresentationPage();
    // Embedder pages (player-contributed pages come later via the
    // GraphQL uiExtensions contract).
    m_head->contributePrefPages(*this);

    connect(m_pageList, &QListWidget::currentRowChanged, m_pages,
            &QStackedWidget::setCurrentIndex);
    m_pageList->setCurrentRow(0);
}

void HeadPreferences::addPage(const QString &title, QWidget *page) {
    m_pageList->addItem(title);
    m_pages->addWidget(page);
}

ConnectionPage::ConnectionPage(HeadWindow *head, QWidget *parent)
    : QWidget(parent), m_head(head) {
    setObjectName(QStringLiteral("prefs.page.connection"));
    auto *lay = new QVBoxLayout(this);

    auto *info = new QLabel(
        QStringLiteral("Backend — the player this head renders.  The "
                       "local player is the desktop default; remote "
                       "players connect over GraphQL."),
        this);
    info->setWordWrap(true);
    lay->addWidget(info);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("prefs.backends"));
    lay->addWidget(m_list, 1);

    auto *btns = new QHBoxLayout();
    auto *addBtn = new QPushButton(QStringLiteral("Add..."), this);
    addBtn->setObjectName(QStringLiteral("prefs.backend.add"));
    auto *removeBtn = new QPushButton(QStringLiteral("Remove"), this);
    removeBtn->setObjectName(QStringLiteral("prefs.backend.remove"));
    auto *useBtn = new QPushButton(QStringLiteral("Use"), this);
    useBtn->setObjectName(QStringLiteral("prefs.backend.use"));
    btns->addWidget(addBtn);
    btns->addWidget(removeBtn);
    btns->addStretch(1);
    btns->addWidget(useBtn);
    lay->addLayout(btns);

    auto *hint = new QLabel(
        QStringLiteral("Switching applies on the next start unless the "
                       "head supports live reconnect."),
        this);
    hint->setObjectName(QStringLiteral("prefs.backend.hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    m_backends = HeadPreferences::loadBackends(m_head->settingsFile());
    refresh();

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        // Small inline editor: name, URL, optional bearer token.
        QDialog dlg(this);
        dlg.setWindowTitle(QStringLiteral("Add backend"));
        dlg.setStyleSheet(m_head->themedDialogStyle());
        auto *form = new QFormLayout(&dlg);
        auto *name = new QLineEdit(&dlg);
        auto *url = new QLineEdit(&dlg);
        url->setPlaceholderText(
            QStringLiteral("graphql+https://host  or  "
                           "graphql+unix:///path.sock"));
        auto *token = new QLineEdit(&dlg);
        token->setEchoMode(QLineEdit::Password);
        form->addRow(QStringLiteral("Name:"), name);
        form->addRow(QStringLiteral("URL:"), url);
        form->addRow(QStringLiteral("Bearer token:"), token);
        auto *bb = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted) return;
        if (name->text().isEmpty() || url->text().isEmpty()) return;
        // The name keys the active selection — keep it unique.
        for (const BackendEntry &e : std::as_const(m_backends))
            if (e.name == name->text()) return;
        m_backends.append({name->text(), url->text(), token->text()});
        HeadPreferences::saveBackends(m_head->settingsFile(), m_backends);
        refresh();
    });
    connect(removeBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        // Row 0 is the fixed local-player default.
        if (row <= 0 || row > m_backends.size()) return;
        const QString removed = m_backends[row - 1].name;
        m_backends.removeAt(row - 1);
        HeadPreferences::saveBackends(m_head->settingsFile(), m_backends);
        // A dangling active name would silently resolve to nothing on
        // the next start — fall back to the local default.
        if (HeadPreferences::activeBackend(m_head->settingsFile()) ==
            removed)
            HeadPreferences::setActiveBackend(m_head->settingsFile(),
                                              QString());
        refresh();
    });
    connect(useBtn, &QPushButton::clicked, this, [this]() {
        const int row = m_list->currentRow();
        if (row < 0) return;
        if (row == 0) {
            HeadPreferences::setActiveBackend(m_head->settingsFile(),
                                              QString());
            m_head->connectToBackend(QString(), QString());
        } else if (row <= m_backends.size()) {
            const BackendEntry &e = m_backends[row - 1];
            HeadPreferences::setActiveBackend(m_head->settingsFile(),
                                              e.name);
            m_head->connectToBackend(e.url, e.token);
        }
        refresh();
    });
}

void ConnectionPage::refresh() {
    m_list->clear();
    const QString active =
        HeadPreferences::activeBackend(m_head->settingsFile());
    // Row 0: the fixed desktop default (the launcher's local player).
    m_list->addItem(
        active.isEmpty()
            ? QStringLiteral("Local player (default)  • active")
            : QStringLiteral("Local player (default)"));
    for (const BackendEntry &e : std::as_const(m_backends)) {
        QString row = e.name + QStringLiteral("  — ") + e.url;
        if (!e.token.isEmpty()) row += QStringLiteral("  [token]");
        if (e.name == active) row += QStringLiteral("  • active");
        m_list->addItem(row);
    }
}

void HeadPreferences::buildPresentationPage() {
    auto *page = new QWidget(this);
    page->setObjectName(QStringLiteral("prefs.page.presentation"));
    auto *lay = new QVBoxLayout(page);

    // Visualization mode — head-local per the sync model.
    auto *visBox = new QGroupBox(QStringLiteral("Visualization"), page);
    auto *visLay = new QVBoxLayout(visBox);
    static const char *const kVis[] = {"Off", "Spectrum analyzer",
                                       "Oscilloscope", "VU meter"};
    for (int i = 0; i < 4; ++i) {
        auto *rb = new QRadioButton(QString::fromUtf8(kVis[i]), visBox);
        rb->setObjectName(QStringLiteral("prefs.vis.%1").arg(i));
        rb->setChecked(m_head->visMode() == i);
        connect(rb, &QRadioButton::toggled, this, [this, i](bool on) {
            if (on) m_head->setVisMode(i);
        });
        visLay->addWidget(rb);
    }
    lay->addWidget(visBox);

    // Time display — the per-skin slot the skin scripts share.
    auto *timeBox = new QGroupBox(QStringLiteral("Time display"), page);
    auto *timeLay = new QVBoxLayout(timeBox);
    auto *elapsed = new QRadioButton(QStringLiteral("Time elapsed"),
                                     timeBox);
    elapsed->setObjectName(QStringLiteral("prefs.time.elapsed"));
    auto *remaining = new QRadioButton(QStringLiteral("Time remaining"),
                                       timeBox);
    remaining->setObjectName(QStringLiteral("prefs.time.remaining"));
    (m_head->timeDisplayMode() == 2 ? remaining : elapsed)
        ->setChecked(true);
    connect(elapsed, &QRadioButton::toggled, this, [this](bool on) {
        if (on) m_head->setTimeDisplayMode(1);
    });
    connect(remaining, &QRadioButton::toggled, this, [this](bool on) {
        if (on) m_head->setTimeDisplayMode(2);
    });
    timeLay->addWidget(elapsed);
    timeLay->addWidget(remaining);
    lay->addWidget(timeBox);

    // Colour theme — head-local; persists like the drawer's Switch.
    auto *themeBox = new QGroupBox(QStringLiteral("Color theme"), page);
    auto *themeLay = new QVBoxLayout(themeBox);
    auto *combo = new QComboBox(themeBox);
    combo->setObjectName(QStringLiteral("prefs.colortheme"));
    combo->addItem(QStringLiteral("Default colors"));
    const QStringList names = m_head->gammasets().names();
    combo->addItems(names);
    if (const Gammaset *a = m_head->gammasets().active();
        a && names.contains(a->name))
        combo->setCurrentText(a->name);
    connect(combo, &QComboBox::currentTextChanged, this,
            [this](const QString &name) {
        const QString applied =
            m_head->gammasets().find(name)
                ? name
                : m_head->gammasets().defaultThemeName();
        m_head->setActiveGammaset(applied);
        if (!m_head->settingsFile().isEmpty())
            QSettings(m_head->settingsFile(), QSettings::IniFormat)
                .setValue(QStringLiteral("player/colortheme"), applied);
    });
    themeLay->addWidget(combo);
    lay->addWidget(themeBox);
    lay->addStretch(1);

    addPage(QStringLiteral("Presentation"), page);
}

// ── the default head Preferences entry point ─────────────────────────

bool HeadWindow::showPreferences() {
    HeadPreferences dlg(this);
    if (qEnvironmentVariableIsSet("WASABIQT_DUMP_PREFS")) {
        dlg.dumpStructure();
        return true;
    }
    dlg.exec();
    return true;
}

// ── headless gate ────────────────────────────────────────────────────

void HeadPreferences::dumpStructure() const {
    fprintf(stderr, "[prefs-dump] begin\n");
    for (int p = 0; p < m_pageList->count(); ++p) {
        fprintf(stderr, "[prefs-dump] page \"%s\"\n",
                m_pageList->item(p)->text().toLocal8Bit().constData());
        QWidget *page = m_pages->widget(p);
        if (!page) continue;
        const auto widgets = page->findChildren<QWidget *>();
        for (QWidget *w : widgets) {
            const QString name = w->objectName();
            if (name.isEmpty() || name.startsWith(QLatin1String("qt_")))
                continue;
            QString extra;
            if (auto *rb = qobject_cast<QRadioButton *>(w))
                extra = rb->isChecked() ? QStringLiteral(" checked")
                                        : QString();
            fprintf(stderr, "[prefs-dump]   %s%s\n",
                    name.toLocal8Bit().constData(),
                    extra.toLocal8Bit().constData());
        }
    }
    fprintf(stderr, "[prefs-dump] end\n");
}

}  // namespace qtWasabi::head
