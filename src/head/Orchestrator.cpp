// Orchestrator implementation (V6).
#include <qtWasabi/head/Orchestrator.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <csignal>
#endif

namespace qtWasabi::head {

namespace {

QString socketDir() {
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(
            QStandardPaths::TempLocation);
    const QString dir = base + QStringLiteral("/qtwasabi");
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner |
                                   QFile::ExeOwner);
    return dir;
}

void armDeathSignal(QProcess *p) {
#ifdef Q_OS_LINUX
    // The child must not outlive the launcher, whatever kills us.
    p->setChildProcessModifier(
        []() { ::prctl(PR_SET_PDEATHSIG, SIGKILL); });
#else
    Q_UNUSED(p);
#endif
}

}  // namespace

Orchestrator::Orchestrator(QObject *parent) : QObject(parent) {}

Orchestrator::~Orchestrator() { stop(); }

void Orchestrator::setPlayerCommand(const QString &program,
                                    const QStringList &args) {
    m_playerProgram = program;
    m_playerArgs = args;
}

void Orchestrator::setPlayerEnv(const QString &key, const QString &value) {
    m_playerEnv.append({key, value});
}

void Orchestrator::setPylonDir(const QString &dir) { m_pylonDir = dir; }

QString Orchestrator::defaultPylonDir() {
    const QString env = qEnvironmentVariable("QTWASABI_PYLON_DIR");
    if (!env.isEmpty() &&
        QFile::exists(env + QStringLiteral("/.pylon/index.js")))
        return env;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        // Packaged (Fedora RPM / macOS bundle).
        appDir + QStringLiteral("/../share/qtamp/pylon"),
        appDir + QStringLiteral("/../share/qtwasabi/pylon"),
        // Development trees: the embedder build dir sits next to the
        // repo; the framework's own build dir sits inside it.
        appDir + QStringLiteral("/../deps/qtWasabi/api/pylon"),
        appDir + QStringLiteral("/../../deps/qtWasabi/api/pylon"),
        appDir + QStringLiteral("/../../../deps/qtWasabi/api/pylon"),
        appDir + QStringLiteral("/../../../../deps/qtWasabi/api/pylon"),
    };
    for (const QString &c : candidates) {
        if (QFile::exists(c + QStringLiteral("/.pylon/index.js")))
            return QFileInfo(c).canonicalFilePath();
    }
    return QString();
}

bool Orchestrator::waitConnectable(const QString &socketPath,
                                   int timeoutMs) {
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < timeoutMs) {
        if (m_startFailed) return false;  // a child died mid-start
        QLocalSocket probe;
        probe.connectToServer(socketPath);
        if (probe.waitForConnected(100)) {
            probe.disconnectFromServer();
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return false;
}

bool Orchestrator::spawnPylon() {
    m_pylon = new QProcess(this);
    armDeathSignal(m_pylon);
    m_pylon->setWorkingDirectory(m_pylonDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("QTAMP_PLAYER_SOCKET"), m_playerSocket);
    env.insert(QStringLiteral("PYLON_SOCKET"), m_graphqlSocket);
    env.insert(QStringLiteral("PYLON_DISABLE_TELEMETRY"),
               QStringLiteral("true"));
    env.remove(QStringLiteral("PORT"));  // unix only for the local trio
    m_pylon->setProcessEnvironment(env);
    if (!qEnvironmentVariableIsSet("QTWASABI_ORCH_DEBUG")) {
        m_pylon->setStandardOutputFile(QProcess::nullDevice());
        m_pylon->setStandardErrorFile(QProcess::nullDevice());
    } else {
        m_pylon->setProcessChannelMode(QProcess::ForwardedChannels);
    }
    const QString node =
        qEnvironmentVariable("QTWASABI_NODE", QStringLiteral("node"));
    connect(m_pylon,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
                if (m_stopping) return;
                if (!m_started) {
                    // Boot crash-loop: never respawn while start() is
                    // still on the stack (the timer would fire inside
                    // waitConnectable's processEvents and recurse) —
                    // flag the failure so start() returns false and
                    // the embedder falls back.
                    m_startFailed = true;
                    return;
                }
                // Audio survives a pylon crash: respawn with capped
                // backoff; the head reconnects on the fresh process.
                fprintf(stderr,
                        "qtamp: pylon exited — respawn in %d ms\n",
                        m_respawnMs);
                QTimer::singleShot(m_respawnMs, this, [this]() {
                    if (m_stopping) return;
                    m_pylon->deleteLater();
                    m_pylon = nullptr;
                    if (spawnPylon()) emit pylonRestarted();
                });
                m_respawnMs = qMin(m_respawnMs * 2, 5000);
            });
    m_pylon->start(node, {QStringLiteral(".pylon/index.js")});
    if (!m_pylon->waitForStarted(5000)) {
        emit failed(QStringLiteral("pylon (node) failed to start"));
        return false;
    }
    if (!waitConnectable(m_graphqlSocket, 15000)) {
        emit failed(QStringLiteral("pylon socket never became "
                                   "connectable"));
        return false;
    }
    return true;
}

bool Orchestrator::start() {
    // Test hook: force a startup failure (exercises the embedder's
    // fallback path deterministically, without breaking pylon
    // discovery in the dev tree).
    if (qEnvironmentVariableIsSet("QTWASABI_ORCH_FAIL")) {
        emit failed(QStringLiteral("forced failure (QTWASABI_ORCH_FAIL)"));
        return false;
    }
    const QString dir = socketDir();
    const qint64 pid = QCoreApplication::applicationPid();
    m_playerSocket =
        dir + QStringLiteral("/qtamp-player-%1.sock").arg(pid);
    m_graphqlSocket =
        dir + QStringLiteral("/qtamp-gql-%1.sock").arg(pid);

    if (m_pylonDir.isEmpty()) m_pylonDir = defaultPylonDir();
    if (m_pylonDir.isEmpty()) {
        emit failed(QStringLiteral("no pylon build found "
                                   "(set QTWASABI_PYLON_DIR)"));
        return false;
    }

    m_player = new QProcess(this);
    armDeathSignal(m_player);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const auto &kv : std::as_const(m_playerEnv))
        env.insert(kv.first, kv.second);
    m_player->setProcessEnvironment(env);
    if (!qEnvironmentVariableIsSet("QTWASABI_ORCH_DEBUG")) {
        m_player->setStandardOutputFile(QProcess::nullDevice());
        m_player->setStandardErrorFile(QProcess::nullDevice());
    } else {
        m_player->setProcessChannelMode(QProcess::ForwardedChannels);
    }
    connect(m_player,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
                if (m_stopping) return;
                m_startFailed = true;
                // The player owns the audio — its death is fatal.  A
                // death during start() is reported by start()'s false
                // return (nothing is connected to failed() yet).
                if (m_started)
                    emit failed(QStringLiteral("player exited (code %1)")
                                    .arg(code));
            });
    QStringList args = m_playerArgs;
    args << m_playerSocket;
    m_player->start(m_playerProgram, args);
    if (!m_player->waitForStarted(5000)) {
        emit failed(QStringLiteral("player failed to start"));
        return false;
    }
    if (!waitConnectable(m_playerSocket, 15000)) {
        emit failed(QStringLiteral("player socket never became "
                                   "connectable"));
        return false;
    }
    if (!spawnPylon()) return false;
    if (m_startFailed) {
        // A child died while the other was coming up (the pylon binds
        // its socket regardless of player health) — this is NOT a
        // working trio.
        emit failed(QStringLiteral("a child died during startup"));
        return false;
    }
    m_started = true;
    return true;
}

void Orchestrator::stop() {
    m_stopping = true;
    // Teardown order: (the head is the caller's) → pylon → player.
    for (QProcess *p : {m_pylon, m_player}) {
        if (!p) continue;
        p->terminate();
        if (!p->waitForFinished(2000)) p->kill();
    }
    m_pylon = nullptr;
    m_player = nullptr;
}

}  // namespace qtWasabi::head
