// V0 spike (c): grpc++ server wrapping a QObject-owning host.  The
// host lives on the Qt MAIN thread (QTimer mutates state, like the
// real player); gRPC delivers handlers on ITS OWN threads, so:
//   - GetState marshals into the Qt thread via QMetaObject::invokeMethod
//     with Qt::BlockingQueuedConnection (the unary pattern),
//   - Events bridges a Qt signal into a stream writer through a
//     mutex+waitcond queue fed by a queued connection (the stream
//     pattern).
// Listens on a unix socket (grpc `unix:` target).
#include <QCoreApplication>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <grpcpp/grpcpp.h>

#include <atomic>
#include <cstdio>
#include <deque>

#include "spike.grpc.pb.h"

// ── the Qt-side host ─────────────────────────────────────────────────
class SpikeHost : public QObject {
    Q_OBJECT
public:
    explicit SpikeHost(QObject *parent = nullptr) : QObject(parent) {
        m_timer.setInterval(200);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            m_revision += 1;
            m_positionMs += 200;
            m_playing = (m_revision % 2) == 0;
            emit changed();
        });
        m_timer.start();
    }

    // Called ONLY on the Qt thread (enforced by the marshalling below).
    void fillState(wasabi::spike::State *out) const {
        Q_ASSERT(QThread::currentThread() == thread());
        out->set_epoch("spike-epoch-1");
        out->set_revision(m_revision);
        out->set_server_now_ms(1000 + m_revision);
        auto *t = out->mutable_transport();
        t->set_playing(m_playing);
        t->set_position_ms(m_positionMs);
        t->set_volume(70);
    }

signals:
    void changed();

private:
    QTimer m_timer;
    qint64 m_revision = 1;
    qint64 m_positionMs = 0;
    bool m_playing = false;
};

// ── gRPC service on gRPC threads ─────────────────────────────────────
class SpikeService final : public wasabi::spike::SpikePlayer::Service {
public:
    explicit SpikeService(SpikeHost *host) : m_host(host) {}

    grpc::Status GetState(grpc::ServerContext *,
                          const wasabi::spike::StateRequest *,
                          wasabi::spike::State *out) override {
        // Unary pattern: block this gRPC thread until the Qt thread
        // filled the message.
        QMetaObject::invokeMethod(
            m_host, [this, out]() { m_host->fillState(out); },
            Qt::BlockingQueuedConnection);
        return grpc::Status::OK;
    }

    grpc::Status Events(grpc::ServerContext *ctx,
                        const wasabi::spike::EventsRequest *,
                        grpc::ServerWriter<wasabi::spike::State> *writer)
        override {
        // Stream pattern: a queued connection (runs on the Qt thread)
        // snapshots state into a guarded queue; this gRPC thread pops
        // and writes.  Connection lifetime == stream lifetime.
        struct Queue {
            QMutex mutex;
            QWaitCondition cond;
            std::deque<wasabi::spike::State> items;
        } queue;

        QMetaObject::Connection conn = QObject::connect(
            m_host, &SpikeHost::changed, m_host, [this, &queue]() {
                wasabi::spike::State s;
                m_host->fillState(&s);
                QMutexLocker lock(&queue.mutex);
                queue.items.push_back(std::move(s));
                queue.cond.wakeOne();
            });

        int sent = 0;
        while (!ctx->IsCancelled() && sent < 100) {
            wasabi::spike::State next;
            {
                QMutexLocker lock(&queue.mutex);
                while (queue.items.empty()) {
                    if (!queue.cond.wait(&queue.mutex, 500)) break;
                }
                if (queue.items.empty()) continue;
                next = std::move(queue.items.front());
                queue.items.pop_front();
            }
            if (!writer->Write(next)) break;
            ++sent;
        }
        QObject::disconnect(conn);
        return grpc::Status::OK;
    }

private:
    SpikeHost *m_host;
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString sock = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : qEnvironmentVariable("SPIKE_GRPC_SOCKET");
    if (sock.isEmpty()) {
        fprintf(stderr, "usage: grpc-spike-server <socket path>\n");
        return 2;
    }
    QFile::remove(sock);

    SpikeHost host;
    SpikeService service(&host);

    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + sock.toStdString(),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    if (!server) {
        fprintf(stderr, "grpc server failed to start\n");
        return 1;
    }
    printf("grpc spike server on unix:%s\n", qPrintable(sock));
    fflush(stdout);

    // Qt event loop runs the host; gRPC threads serve concurrently.
    return app.exec();
}

#include "server.moc"
