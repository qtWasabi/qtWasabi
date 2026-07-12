// SidecarService — the framework's stock player-protocol server.
//
// A player process embeds this to become a Wasabi 2 player: it exposes
// api/player.proto v1 (gRPC) over a unix socket, backed by any
// qtWasabi::PlayerHost.  The framework's pylon pairs with it as the
// GraphQL server; consumers never speak this protocol.
//
// Threading: the host lives on the Qt main thread; gRPC delivers
// handlers on its own threads.  Unary calls marshal via
// Qt::BlockingQueuedConnection, streams bridge through mutex/waitcond
// queues fed on the Qt thread (patterns proven in spikes/v0/grpc).
//
// Sync semantics served here (epoch per boot, global revision,
// per-section change events, position never pushed on a timer, 5 s
// ping beacon) are the contract's — see api/player.proto.
#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <memory>

namespace qtWasabi {
class PlayerHost;
}

namespace qtWasabi::serve {

// Player-side extras the PlayerHost vtable doesn't carry (they die
// into the host in a later milestone; kept hook-shaped for the port).
struct ServeHooks {
    std::function<void()> playlistClear;
    std::function<void(const QList<int> &)> playlistRemoveRows;
    std::function<bool()> eqOn;
    std::function<void(bool)> setEqOn;
    std::function<bool()> eqAuto;
    std::function<void(bool)> setEqAuto;
    // Absolute directory OpenOp/PlaylistAddOp paths are confined to.
    QString musicRoot;
    // Implementation tag for GetVersion, e.g. "qtamp-player 0.5".
    QString playerName;
};

class SidecarPrivate;

class SidecarService {
public:
    // `host` must outlive the service and live on the Qt main thread.
    SidecarService(PlayerHost *host, ServeHooks hooks);
    ~SidecarService();

    SidecarService(const SidecarService &) = delete;
    SidecarService &operator=(const SidecarService &) = delete;

    // Bind the gRPC server to `unix:<socketPath>` (unlink-before-bind).
    // Returns false when the server failed to start.
    bool listen(const QString &socketPath);
    QString socketPath() const;

private:
    std::unique_ptr<SidecarPrivate> d;
};

}  // namespace qtWasabi::serve
