// SidecarService implementation — api/player.proto v1 over gRPC,
// backed by a qtWasabi::PlayerHost on the Qt main thread.
//
// Ported 1:1 from the reference embedder's hand-rolled control-channel
// server (revision/epoch bookkeeping, per-section change fingerprints
// with position excluded from the transport fingerprint, apply-time
// push, playlist revision guard, music-root confinement); the wire is
// now the contract's protobuf, EQ in Maki scale.
#include <qtWasabi/serve/SidecarService.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QWaitCondition>

#include <grpcpp/grpcpp.h>

#include <deque>
#include <string>
#include <vector>

#include <qtWasabi/PlayerHost.h>

#include "player.grpc.pb.h"

namespace wp = wasabi::player::v1;

namespace qtWasabi::serve {
namespace {

// The host's EQ_BAND axis is a slider POSITION (0 = top = +12 dB,
// 63 = bottom = -12 dB, 31 flat); the proto carries the Maki GAIN
// scale (-127..127, +127 = boost).  Same inverted mapping as
// PlayerHost::eqBandValue.
int sliderToMaki(int s) {
    return qBound(-127, qRound((31 - s) * 127.0 / 31.0), 127);
}
int makiToSlider(int m) {
    return qBound(0, qRound(31.0 - m * 31.0 / 127.0), 63);
}

// One per open Events stream: filled on the Qt thread, drained on the
// stream's gRPC thread.
struct EventSink {
    QMutex mutex;
    QWaitCondition cond;
    std::deque<wp::Event> items;

    void push(const wp::Event &e) {
        QMutexLocker lock(&mutex);
        items.push_back(e);
        cond.wakeOne();
    }
};

}  // namespace

// ── the Qt-thread core ───────────────────────────────────────────────
// Owns epoch/revision/fingerprints and answers every host read; all
// methods run on the host's thread (the service marshals into it).
class SidecarCore : public QObject {
public:
    SidecarCore(PlayerHost *host, ServeHooks hooks)
        : QObject(host),
          m_host(host),
          m_hooks(std::move(hooks)),
          m_epoch(QUuid::createUuid()
                      .toString(QUuid::WithoutBraces)
                      .toStdString()) {
        m_clock.start();

        connect(host, &PlayerHost::sourceChanged, this,
                &SidecarCore::pushChanges);
        connect(host, &PlayerHost::metaDataChanged, this,
                &SidecarCore::pushChanges);
        connect(host, &PlayerHost::playbackStateChanged, this,
                &SidecarCore::pushChanges);
        connect(host, &PlayerHost::playlistChanged, this, [this]() {
            m_playlistDirty = true;
            pushChanges();
        });

        // The 250 ms tick catches signal-less drift; the 5 s ping is
        // the stream keepalive and clock beacon.
        auto *tick = new QTimer(this);
        tick->setInterval(250);
        connect(tick, &QTimer::timeout, this, &SidecarCore::pushChanges);
        tick->start();
        auto *ping = new QTimer(this);
        ping->setInterval(5000);
        connect(ping, &QTimer::timeout, this, [this]() {
            wp::Event e;
            stampEnvelope(&e);
            e.mutable_ping();
            broadcast(e);
        });
        ping->start();
    }

    // ── section fills (Qt thread) ─────────────────────────────────────
    void fillTransport(wp::Transport *t) const {
        t->set_playing(m_host->isPlaying());
        t->set_paused(m_host->isPaused());
        t->set_position_ms(m_host->positionMs());
        t->set_position_at_ms(m_clock.elapsed());
        t->set_duration_ms(m_host->durationMs());
        t->set_volume(m_host->volume());
        t->set_pan(m_host->sliderPosition(QStringLiteral("PAN")));
        t->set_shuffle(false);
        t->set_repeat(wp::REPEAT_OFF);
    }

    void fillTrack(wp::Track *tr) const {
        auto meta = [this](const char *f) {
            return m_host->playItemMetaData(QLatin1String(f)).toStdString();
        };
        tr->set_title(m_host->songTitle().toStdString());
        tr->set_artist(meta("artist"));
        tr->set_album(meta("album"));
        tr->set_album_artist(meta("albumartist"));
        tr->set_genre(meta("genre"));
        tr->set_year(meta("year"));
        tr->set_track_no(meta("track"));
        tr->set_disc(meta("disc"));
        tr->set_composer(meta("composer"));
        tr->set_publisher(meta("publisher"));
        tr->set_stream_genre(meta("streamgenre"));
        const std::string path = m_host->songPath().toStdString();
        tr->set_filename(path);
        tr->set_display_title(
            m_host->playItemDisplayTitle().toStdString());
        tr->set_decoder(m_host->decoderName().toStdString());
        tr->set_bitrate(m_host->bitrate());
        tr->set_sample_rate(m_host->sampleRate());
        tr->set_channels(m_host->channelCount());
        tr->set_art_token(path);
    }

    void fillPlaylist(wp::Playlist *p, bool withRows) const {
        p->set_revision(qint64(m_playlistRevision));
        p->set_current_index(m_host->playlistCurrentRow());
        const int count = m_host->playlistRowCount();
        p->set_row_count(count);
        if (!withRows) return;
        for (int i = 0; i < count; ++i) {
            wp::PlaylistRow *r = p->add_rows();
            r->set_index(i);
            r->set_text(m_host->playlistRowText(i).toStdString());
            r->set_duration_ms(m_host->playlistRowDurationMs(i));
        }
    }

    void fillEq(wp::Eq *e) const {
        e->set_on(m_hooks.eqOn ? m_hooks.eqOn() : false);
        e->set_auto_(m_hooks.eqAuto ? m_hooks.eqAuto() : false);
        e->set_preamp(sliderToMaki(sliderValue(QStringLiteral("preamp"))));
        for (int b = 0; b < 10; ++b)
            e->add_bands(sliderToMaki(sliderValue(QString::number(b))));
    }

    void fillCapabilities(wp::Capabilities *c) const {
        const HostCapabilities hc = m_host->hostCapabilities();
        c->set_local_files(hc.localFiles);
        c->set_ingest(false);
        c->set_playlist_edit(bool(m_hooks.playlistRemoveRows) &&
                             bool(m_hooks.playlistClear));
        c->set_library(false);        // V8
        c->set_media_library(false);  // V8
        c->set_vis_pcm(false);        // V7
        c->set_pcm_stream(false);     // V7
        c->set_preferences(false);
        c->set_shuffle_repeat(false);
        c->set_music_root(m_hooks.musicRoot.toStdString());
    }

    void fillState(const google::protobuf::RepeatedField<int> &sections,
                   wp::State *out) const {
        out->set_epoch(m_epoch);
        out->set_revision(qint64(m_revision));
        out->set_server_now_ms(m_clock.elapsed());
        std::vector<wp::Section> want;
        for (int s : sections) want.push_back(wp::Section(s));
        if (want.empty())
            want = {wp::SECTION_TRANSPORT, wp::SECTION_TRACK,
                    wp::SECTION_PLAYLIST_META, wp::SECTION_EQ};
        for (wp::Section s : want) {
            switch (s) {
                case wp::SECTION_TRANSPORT:
                    fillTransport(out->mutable_transport());
                    break;
                case wp::SECTION_TRACK:
                    fillTrack(out->mutable_track());
                    break;
                case wp::SECTION_PLAYLIST_META:
                    fillPlaylist(out->mutable_playlist(), false);
                    break;
                case wp::SECTION_PLAYLIST_ROWS:
                    fillPlaylist(out->mutable_playlist(), true);
                    break;
                case wp::SECTION_EQ:
                    fillEq(out->mutable_eq());
                    break;
                case wp::SECTION_CAPABILITIES:
                    fillCapabilities(out->mutable_capabilities());
                    break;
                case wp::SECTION_UI_EXTENSIONS:
                    out->mutable_ui_extensions();  // contract-complete, empty
                    break;
                default:
                    break;
            }
        }
    }

    // ── commands (Qt thread) ──────────────────────────────────────────
    void execute(const wp::Command &cmd, wp::CommandResult *res) {
        const auto fail = [&](const char *why) {
            res->set_ok(false);
            res->set_error(why);
            res->set_revision(qint64(m_revision));
        };
        const auto guardOk = [&](bool has, qint64 expect) {
            return !has || quint64(expect) == m_playlistRevision;
        };

        switch (cmd.op_case()) {
            case wp::Command::kPlay:
                // Cold start plays the current row — same as a
                // double-click — so autoplay works before any UI touch.
                if (!m_host->isPaused() && !m_host->isPlaying() &&
                    m_host->playlistRowCount() > 0) {
                    const int row =
                        qBound(0, m_host->playlistCurrentRow(),
                               m_host->playlistRowCount() - 1);
                    m_host->playlistPlayRow(row);
                } else {
                    m_host->play();
                }
                break;
            case wp::Command::kPause: m_host->pause(); break;
            case wp::Command::kStop: m_host->stop(); break;
            case wp::Command::kNext: m_host->next(); break;
            case wp::Command::kPrev: m_host->prev(); break;
            case wp::Command::kSeek:
                m_host->seekMs(cmd.seek().ms());
                break;
            case wp::Command::kSetVolume:
                m_host->setVolume(qBound(0, cmd.set_volume().v(), 100));
                break;
            case wp::Command::kSetPan:
                m_host->setSliderPosition(
                    QStringLiteral("PAN"),
                    qBound(0.0, cmd.set_pan().v(), 1.0));
                break;
            case wp::Command::kSetEqOn:
                if (!m_hooks.setEqOn) return fail("unsupported");
                m_hooks.setEqOn(cmd.set_eq_on().on());
                break;
            case wp::Command::kSetEqAuto:
                if (!m_hooks.setEqAuto) return fail("unsupported");
                m_hooks.setEqAuto(cmd.set_eq_auto().on());
                break;
            case wp::Command::kSetEqPreamp:
                m_host->setSliderPosition(
                    QStringLiteral("EQ_BAND"),
                    makiToSlider(cmd.set_eq_preamp().value()) / 63.0,
                    QStringLiteral("preamp"));
                break;
            case wp::Command::kSetEqBand: {
                const int band = cmd.set_eq_band().band();
                if (band < 0 || band > 9) return fail("bad band");
                m_host->setSliderPosition(
                    QStringLiteral("EQ_BAND"),
                    makiToSlider(cmd.set_eq_band().value()) / 63.0,
                    QString::number(band));
                break;
            }
            case wp::Command::kPlayRow: {
                const auto &op = cmd.play_row();
                if (!guardOk(op.has_expect_playlist_revision(),
                             op.expect_playlist_revision()))
                    return fail("playlistRevision mismatch");
                if (op.row() < 0 ||
                    op.row() >= m_host->playlistRowCount())
                    return fail("bad row");
                m_host->playlistPlayRow(op.row());
                break;
            }
            case wp::Command::kSetCurrentRow: {
                const auto &op = cmd.set_current_row();
                if (!guardOk(op.has_expect_playlist_revision(),
                             op.expect_playlist_revision()))
                    return fail("playlistRevision mismatch");
                if (op.row() < 0 ||
                    op.row() >= m_host->playlistRowCount())
                    return fail("bad row");
                m_host->playlistSetCurrentRow(op.row());
                break;
            }
            case wp::Command::kPlaylistAdd: {
                const auto &op = cmd.playlist_add();
                if (op.paths_size() == 0) return fail("no paths");
                for (const std::string &p : op.paths())
                    if (!pathAllowed(QString::fromStdString(p)))
                        return fail("path outside music root");
                for (const std::string &p : op.paths())
                    m_host->enqueueAndPlay(
                        QUrl::fromLocalFile(QString::fromStdString(p)),
                        /*enqueueOnly=*/true);
                break;
            }
            case wp::Command::kPlaylistRemove: {
                const auto &op = cmd.playlist_remove();
                if (!m_hooks.playlistRemoveRows)
                    return fail("unsupported");
                if (!guardOk(op.has_expect_playlist_revision(),
                             op.expect_playlist_revision()))
                    return fail("playlistRevision mismatch");
                QList<int> rows;
                for (int r : op.rows()) rows.append(r);
                m_hooks.playlistRemoveRows(rows);
                break;
            }
            case wp::Command::kPlaylistClear:
                if (!m_hooks.playlistClear) return fail("unsupported");
                m_hooks.playlistClear();
                break;
            case wp::Command::kOpen: {
                const QUrl u(
                    QString::fromStdString(cmd.open().url()));
                if (!u.isLocalFile() || !pathAllowed(u.toLocalFile()))
                    return fail("path outside music root");
                m_host->openPath(u);
                break;
            }
            case wp::Command::kSetShuffle:
            case wp::Command::kSetRepeat:
            case wp::Command::kPleditVerb:
            case wp::Command::kMlPlayTracks:
            case wp::Command::kShowPreferences:
            case wp::Command::kSetPref:
            case wp::Command::kInvokeMenuItem:
                return fail("unsupported");
            default:
                return fail("unknown op");
        }

        // Apply-time push: the mutation's effect reaches every open
        // stream before the RPC even returns.
        pushChanges();
        res->set_ok(true);
        res->set_revision(qint64(m_revision));
        fillState(cmd.echo(), res->mutable_state());
    }

    // ── album art (Qt thread) ─────────────────────────────────────────
    void artPng(std::string *png, std::string *mime,
                std::string *token) const {
        const QImage art = m_host->albumArt();
        *token = m_host->songPath().toStdString();
        if (art.isNull()) return;
        QByteArray bytes;
        QBuffer out(&bytes);
        out.open(QIODevice::WriteOnly);
        art.save(&out, "PNG");
        png->assign(bytes.constData(), size_t(bytes.size()));
        *mime = "image/png";
    }

    void version(wp::Version *v) const {
        v->set_protocol("1.0.0");
        v->set_player(m_hooks.playerName.toStdString());
    }

    // ── stream sinks (Qt thread) ──────────────────────────────────────
    void addSink(EventSink *s) { m_sinks.append(s); }
    void removeSink(EventSink *s) { m_sinks.removeAll(s); }

private:
    int sliderValue(const QString &param) const {
        const double p =
            m_host->sliderPosition(QStringLiteral("EQ_BAND"), param);
        return p < 0.0 ? 31 : qBound(0, qRound(p * 63.0), 63);
    }

    bool pathAllowed(const QString &path) const {
        if (m_hooks.musicRoot.isEmpty()) return false;
        const QString canonical = QFileInfo(path).canonicalFilePath();
        const QString root =
            QFileInfo(m_hooks.musicRoot).canonicalFilePath();
        if (canonical.isEmpty() || root.isEmpty()) return false;
        return canonical == root ||
               canonical.startsWith(root + QLatin1Char('/'));
    }

    void stampEnvelope(wp::Event *e) const {
        e->set_epoch(m_epoch);
        e->set_revision(qint64(m_revision));
        e->set_server_now_ms(m_clock.elapsed());
    }

    void broadcast(const wp::Event &e) {
        for (EventSink *s : std::as_const(m_sinks)) s->push(e);
    }

    // Per-section fingerprints; the transport one EXCLUDES position, so
    // ordinary playback never pushes — consumers interpolate, and edges
    // (play/pause/seek/track change) surface as changed flags/sections.
    void pushChanges() {
        wp::Transport t;
        fillTransport(&t);
        wp::Transport tNoPos = t;
        tNoPos.set_position_ms(0);
        tNoPos.set_position_at_ms(0);
        wp::Track tr;
        fillTrack(&tr);
        wp::Playlist pl;
        fillPlaylist(&pl, true);
        wp::Eq eq;
        fillEq(&eq);

        const std::string fpT = tNoPos.SerializeAsString();
        const std::string fpTr = tr.SerializeAsString();
        // Rows change without the meta changing (rename), so the
        // fingerprint covers rows; revision excluded (it feeds itself).
        wp::Playlist plNoRev = pl;
        plNoRev.set_revision(0);
        const std::string fpP = plNoRev.SerializeAsString();
        const std::string fpE = eq.SerializeAsString();

        const bool tChanged = fpT != m_fpTransport;
        const bool trChanged = fpTr != m_fpTrack;
        const bool pChanged = m_playlistDirty || fpP != m_fpPlaylist;
        const bool eChanged = fpE != m_fpEq;
        if (!tChanged && !trChanged && !pChanged && !eChanged) return;

        if (pChanged) {
            ++m_playlistRevision;
            m_playlistDirty = false;
        }
        ++m_revision;

        if (tChanged) {
            wp::Event e;
            stampEnvelope(&e);
            fillTransport(e.mutable_transport());
            broadcast(e);
        }
        if (trChanged) {
            wp::Event e;
            stampEnvelope(&e);
            fillTrack(e.mutable_track());
            broadcast(e);
        }
        if (pChanged) {
            wp::Event e;
            stampEnvelope(&e);
            fillPlaylist(e.mutable_playlist_rows(), true);
            broadcast(e);
        }
        if (eChanged) {
            wp::Event e;
            stampEnvelope(&e);
            fillEq(e.mutable_eq());
            broadcast(e);
        }
        m_fpTransport = fpT;
        m_fpTrack = fpTr;
        m_fpPlaylist = fpP;
        m_fpEq = fpE;
    }

    PlayerHost *m_host;
    ServeHooks m_hooks;
    std::string m_epoch;
    QElapsedTimer m_clock;
    quint64 m_revision = 1;
    quint64 m_playlistRevision = 1;
    bool m_playlistDirty = false;
    std::string m_fpTransport, m_fpTrack, m_fpPlaylist, m_fpEq;
    QList<EventSink *> m_sinks;
};

// ── the gRPC service (gRPC threads; marshals into the core) ─────────
class PlayerService final : public wp::Player::Service {
public:
    explicit PlayerService(SidecarCore *core) : m_core(core) {}

    grpc::Status GetVersion(grpc::ServerContext *,
                            const wp::StateRequest *,
                            wp::Version *out) override {
        onCore([&] { m_core->version(out); });
        return grpc::Status::OK;
    }

    grpc::Status GetCapabilities(grpc::ServerContext *,
                                 const wp::StateRequest *,
                                 wp::Capabilities *out) override {
        onCore([&] { m_core->fillCapabilities(out); });
        return grpc::Status::OK;
    }

    grpc::Status GetState(grpc::ServerContext *,
                          const wp::StateRequest *req,
                          wp::State *out) override {
        onCore([&] { m_core->fillState(req->sections(), out); });
        return grpc::Status::OK;
    }

    grpc::Status Execute(grpc::ServerContext *, const wp::Command *cmd,
                         wp::CommandResult *out) override {
        onCore([&] { m_core->execute(*cmd, out); });
        return grpc::Status::OK;
    }

    grpc::Status Events(grpc::ServerContext *ctx,
                        const wp::EventsRequest *,
                        grpc::ServerWriter<wp::Event> *writer) override {
        EventSink sink;
        onCore([&] { m_core->addSink(&sink); });

        while (!ctx->IsCancelled()) {
            wp::Event next;
            {
                QMutexLocker lock(&sink.mutex);
                while (sink.items.empty()) {
                    if (!sink.cond.wait(&sink.mutex, 500)) break;
                }
                if (sink.items.empty()) continue;  // re-check cancel
                next = std::move(sink.items.front());
                sink.items.pop_front();
            }
            if (!writer->Write(next)) break;
        }
        onCore([&] { m_core->removeSink(&sink); });
        return grpc::Status::OK;
    }

    grpc::Status GetArt(grpc::ServerContext *, const wp::ArtRequest *,
                        grpc::ServerWriter<wp::ArtChunk> *writer) override {
        std::string png, mime, token;
        onCore([&] { m_core->artPng(&png, &mime, &token); });
        if (png.empty())
            return {grpc::StatusCode::NOT_FOUND, "no art"};
        constexpr size_t kChunk = 256 * 1024;
        for (size_t off = 0; off < png.size(); off += kChunk) {
            wp::ArtChunk c;
            c.set_token(token);
            if (off == 0) c.set_mime(mime);
            c.set_data(png.substr(off, kChunk));
            if (!writer->Write(c)) break;
        }
        return grpc::Status::OK;
    }

    grpc::Status SpectrumFrames(
        grpc::ServerContext *, const wp::SpectrumRequest *,
        grpc::ServerWriter<wp::SpectrumFrame> *) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "V7"};
    }
    grpc::Status PcmFrames(grpc::ServerContext *, const wp::PcmRequest *,
                           grpc::ServerWriter<wp::PcmFrame> *) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "V7"};
    }
    grpc::Status Library(grpc::ServerContext *, const wp::LibraryRequest *,
                         wp::LibraryReply *) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "V8"};
    }
    grpc::Status MlFilterValues(grpc::ServerContext *,
                                const wp::MlFilterRequest *,
                                wp::MlFilterReply *) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "V8"};
    }
    grpc::Status MlTracks(grpc::ServerContext *,
                          const wp::MlTracksRequest *,
                          wp::MlTracksReply *) override {
        return {grpc::StatusCode::UNIMPLEMENTED, "V8"};
    }

private:
    // Unary pattern: block this gRPC thread until the Qt thread ran f.
    template <typename F>
    void onCore(F f) {
        QMetaObject::invokeMethod(m_core, std::move(f),
                                  Qt::BlockingQueuedConnection);
    }

    SidecarCore *m_core;
};

// ── lifecycle ────────────────────────────────────────────────────────
class SidecarPrivate {
public:
    SidecarCore *core = nullptr;  // owned by the host (QObject child)
    std::unique_ptr<PlayerService> service;
    std::unique_ptr<grpc::Server> server;
    QString socketPath;
};

SidecarService::SidecarService(PlayerHost *host, ServeHooks hooks)
    : d(std::make_unique<SidecarPrivate>()) {
    d->core = new SidecarCore(host, std::move(hooks));
    d->service = std::make_unique<PlayerService>(d->core);
}

SidecarService::~SidecarService() {
    if (d->server) d->server->Shutdown();
    if (!d->socketPath.isEmpty()) QFile::remove(d->socketPath);
}

bool SidecarService::listen(const QString &socketPath) {
    QFileInfo(socketPath).dir().mkpath(QStringLiteral("."));
    QFile::remove(socketPath);  // unlink-before-bind
    grpc::ServerBuilder builder;
    builder.AddListeningPort("unix:" + socketPath.toStdString(),
                             grpc::InsecureServerCredentials());
    builder.RegisterService(d->service.get());
    d->server = builder.BuildAndStart();
    if (!d->server) return false;
    d->socketPath = socketPath;
    fprintf(stderr, "qtwasabi-sidecar: player protocol on unix:%s\n",
            qPrintable(socketPath));
    return true;
}

QString SidecarService::socketPath() const { return d->socketPath; }

}  // namespace qtWasabi::serve
