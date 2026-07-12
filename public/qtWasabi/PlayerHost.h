// PlayerHost — the framework's host base over qtWasabi::Host.
//
// The engine reaches the player exclusively through the abstract
// qtWasabi::Host vtable.  A head (the window/menu layer around the
// engine) needs a slightly larger surface than that vtable: playback
// entry points (open a path, enqueue), the current source URL, the EQ
// band store, capability discovery, plus change notifications the head
// wires to its repaint/title machinery.
//
// PlayerHost gathers exactly that surface so more than one host can
// back the same head: a local player host, remote::RemoteHost (a
// networked player synced over GraphQL) and FakeHost (the deterministic
// scripted host).  Heads depend only on PlayerHost, never on a concrete
// subclass — the embedder's factory decides which one to build.
#pragma once

#include <QObject>
#include <QUrl>
#include <QList>

#include <functional>

#include <qtWasabi/Host.h>

class QWidget;

namespace qtWasabi {

// What this host can do locally.  Heads gate affordances on it (file
// dialogs, drag-and-drop ingest, in-process visualizer overlays)
// instead of asking "is it remote" — a capability describes the seam,
// a location doesn't.
struct HostCapabilities {
    // Native file/folder dialogs and filesystem ingest make sense here.
    bool localFiles = true;
    // An in-process PCM analyzer exists (vis overlays that need raw
    // audio, e.g. MilkDrop, are possible).
    bool localAnalyzer = true;
    // The host implements its own file picker (pickFile /
    // openFilesAndEnqueue open real dialogs).  When false and
    // localFiles is true, the HEAD supplies the file dialog
    // (HeadWindow's picker); when localFiles is false too, file-pick
    // flows are silently consumed (a remote player must never pop a
    // local dialog).
    bool providesFilePicker = false;
};

class PlayerHost : public QObject, public Host {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual HostCapabilities hostCapabilities() const { return {}; }

    // ── Playback entry points the head calls directly ─────────────────
    // Decode and play a path from the top (pledit double-click, CLI).
    virtual void openPath(const QUrl &u) = 0;
    // Enqueue (optionally without starting) and play.
    virtual void enqueueAndPlay(const QUrl &u, bool enqueueOnly = false) = 0;
    // File/folder pickers — only meaningful with localFiles; hosts
    // without it return empty.
    virtual QList<QUrl> openFilesAndEnqueue(QWidget *embedder,
                                            bool enqueueOnly = false) {
        Q_UNUSED(embedder);
        Q_UNUSED(enqueueOnly);
        return {};
    }
    virtual QList<QUrl> openFolderAndEnqueue(QWidget *embedder,
                                             bool enqueueOnly = false) {
        Q_UNUSED(embedder);
        Q_UNUSED(enqueueOnly);
        return {};
    }

    // ── State the head pulls that isn't on the Host vtable ────────────
    // Full filesystem path of the current item (Maki "playitem:string").
    virtual QString songPath() const { return songFilename(); }
    // The current media source (bookmarks "add current", About skin path).
    virtual QUrl currentSourceUrl() const = 0;
    // Reload spectrum/peak visual preferences.
    virtual void reloadVisPrefs() {}

    // ── EQ band store (Maki System.setEqBand/getEqBand, -127..127) ────
    // Default routes through the EQ_BAND slider axis already on the Host
    // vtable, so a RemoteHost gets working EQ callbacks for free; a
    // local player host overrides these with its direct DSP store.
    //
    // The axis is a slider POSITION (0 = top = +12 dB boost, 1 = bottom
    // = -12 dB cut, 31/63 flat — Winamp's classic EQ slider), so the
    // Maki gain scale maps INVERTED onto it.
    virtual void setEqBandValue(int band, int val) {
        const int slider = qBound(0, qRound(31.0 - val * 31.0 / 127.0), 63);
        setSliderPosition(QStringLiteral("EQ_BAND"), slider / 63.0,
                          QString::number(band));
    }
    virtual int eqBandValue(int band) const {
        const double p = sliderPosition(QStringLiteral("EQ_BAND"),
                                        QString::number(band));
        if (p < 0.0) return 0;
        const int slider = qRound(p * 63.0);
        return qBound(-127, qRound((31 - slider) * 127.0 / 31.0), 127);
    }

    // Opens the Preferences dialog (wired by the head that owns it).
    std::function<void()> showPreferencesFn;

    // External model owners (a playlist window) report row changes here;
    // signals cannot be emitted from outside the class.
    void notifyPlaylistChanged() { emit playlistChanged(); }

signals:
    // Replace direct media-pipeline signal connections in the head, so
    // every host drives the same repaint/title machinery.
    void sourceChanged();
    void playbackStateChanged();
    void metaDataChanged();
    void playlistChanged();
};

}  // namespace qtWasabi
