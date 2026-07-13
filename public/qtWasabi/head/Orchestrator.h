// Orchestrator — the launcher's process trio (V6).
//
// Desktop-orchestrated mode: the launcher spawns the player (gRPC on a
// per-instance unix socket), then the framework pylon (GraphQL on a
// second unix socket, player socket passed via env), and the head
// connects over graphql+unix.  Lifecycle per the okf spec:
//   - children die with the launcher (PR_SET_PDEATHSIG on Linux; the
//     destructor kills them everywhere),
//   - the pylon respawns with capped backoff — audio survives a pylon
//     crash, the head reconnects on the fresh epoch,
//   - a player crash is fatal (emit failed),
//   - teardown order: head (caller) → pylon → player.
// Sockets live in $XDG_RUNTIME_DIR/qtwasabi (0700), per-instance
// names (pid suffix) so two players never collide; readiness is
// CONNECTABILITY, never bare existence (stale sockets lie).
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace qtWasabi::head {

class Orchestrator : public QObject {
    Q_OBJECT
public:
    explicit Orchestrator(QObject *parent = nullptr);
    ~Orchestrator() override;

    // The player process: program + args; the player socket path is
    // appended as the final argument (the `--serve-player <sock>`
    // convention).
    void setPlayerCommand(const QString &program, const QStringList &args);
    // Extra environment for the player child (musicRoot policy etc.).
    void setPlayerEnv(const QString &key, const QString &value);
    // Directory containing the built pylon (.pylon/index.js).
    void setPylonDir(const QString &dir);

    // Resolution order for the pylon dir: QTWASABI_PYLON_DIR, then
    // packaged locations relative to the executable, then the
    // development tree.  Empty when nothing was found.
    static QString defaultPylonDir();

    // Spawn player → wait connectable → spawn pylon → wait connectable.
    // False (with failed() emitted) when anything cannot start.
    bool start();
    void stop();

    QString playerSocket() const { return m_playerSocket; }
    QString graphqlSocket() const { return m_graphqlSocket; }

signals:
    // The pylon crashed and was respawned; the head's reconnect picks
    // up the fresh epoch on its own — this is informational.
    void pylonRestarted();
    void failed(const QString &why);

private:
    bool waitConnectable(const QString &socketPath, int timeoutMs);
    bool spawnPylon();

    QString m_playerProgram;
    QStringList m_playerArgs;
    QList<QPair<QString, QString>> m_playerEnv;
    QString m_pylonDir;
    QString m_playerSocket;
    QString m_graphqlSocket;
    QProcess *m_player = nullptr;
    QProcess *m_pylon = nullptr;
    bool m_stopping = false;
    bool m_started = false;      // start() completed successfully
    bool m_startFailed = false;  // a child died — abort waits/refuse ok
    int m_respawnMs = 500;
};

}  // namespace qtWasabi::head
