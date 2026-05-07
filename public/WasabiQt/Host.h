// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// WasabiQT::Host — embedder-implemented bridge between WasabiQT and
// the host application's playback engine, playlist, EQ, and config
// store.  WasabiQT renders the skin and runs Maki scripts; everything
// else (decode, mix, library, settings) belongs to the host.
//
// Audacious, WACUP, a custom Qt media player — each implements this
// virtual interface, hands it to WasabiQt::Skin, and gets a fully
// rendered, scripted Modern-skin window in return.
//

#include <QString>
#include <QVariant>

namespace WasabiQt {

class Host {
public:
    virtual ~Host() = default;

    // ── Playback state (read) ─────────────────────────────────
    virtual int     playbackState() const = 0;       // 0 stop, 1 play, 2 pause
    virtual qint64  position()      const = 0;       // ms
    virtual qint64  duration()      const = 0;       // ms
    virtual int     bitrate()       const = 0;       // kbps, 0 if unknown
    virtual int     sampleRate()    const = 0;       // kHz
    virtual int     channels()      const = 0;       // 0/1/2
    virtual QString songTitle()     const = 0;
    virtual QString songArtist()    const = 0;
    virtual QString songAlbum()     const = 0;
    virtual QString currentFile()   const = 0;

    // ── Mixer ─────────────────────────────────────────────────
    virtual int     volume()        const = 0;       // 0..255
    virtual int     balance()       const = 0;       // -127..+127
    virtual bool    eqEnabled()     const = 0;
    virtual bool    eqAutoEnabled() const = 0;
    virtual int     eqBand(int i)   const = 0;       // 0..63
    virtual int     eqPreamp()      const = 0;       // 0..63

    // ── Visualisation samples (~50 Hz) ────────────────────────
    virtual const float *spectrum(int *outBands)   const = 0;
    virtual const float *waveform(int *outSamples) const = 0;

    // ── Player flags ──────────────────────────────────────────
    virtual bool shuffleOn()   const = 0;
    virtual bool repeatOn()    const = 0;
    virtual bool repeatTrack() const = 0;

    // ── Sub-windows ───────────────────────────────────────────
    virtual bool isPlaylistVisible()     const = 0;
    virtual bool isMediaLibraryVisible() const = 0;
    virtual bool isVideoVisible()        const { return false; }

    // ── Actions (write) ───────────────────────────────────────
    virtual void play()  = 0;
    virtual void pause() = 0;
    virtual void stop()  = 0;
    virtual void next()  = 0;
    virtual void prev()  = 0;
    virtual void setPosition(qint64 ms)        = 0;
    virtual void setVolume(int v)              = 0;
    virtual void setBalance(int b)             = 0;
    virtual void toggleEq()                    = 0;
    virtual void toggleEqAuto()                = 0;
    virtual void setEqBand(int i, int v)       = 0;
    virtual void setEqPreamp(int v)            = 0;
    virtual void setShuffle(bool b)            = 0;
    virtual void cycleRepeat()                 = 0;
    virtual void showPlaylist(bool b)          = 0;
    virtual void showMediaLibrary(bool b)      = 0;
    virtual void openFileDialog()              = 0;

    // ── Config store ──────────────────────────────────────────
    virtual QVariant getConfig(const QString &guid, const QString &key) const = 0;
    virtual void     setConfig(const QString &guid, const QString &key,
                               const QVariant &value) = 0;

    // ── Notification (engine → host) ──────────────────────────
    // Default no-op so embedders only override what they care about.
    virtual void onSkinEvent(const QString &name, const QVariantList &args) {
        Q_UNUSED(name); Q_UNUSED(args);
    }
};

} // namespace WasabiQt
