// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// WasabiQt::Host — abstract player-side interface that Modern skins
// query for live state (current position, song title, volume, …) and
// drive for transport actions (PLAY / PAUSE / STOP / NEXT / …).
//
// qtWasabi ships two ready-made glue helpers built on top of Host:
//
//   * `makeDefaultDisplayResolver(host)` — returns a DisplayResolver
//     suitable for `SkinView::setDisplayResolver()`.  Maps Wasabi's
//     standard `display="…"` keys (time, timeleft, duration,
//     songname, songtitle, songinfo, filename, kbps, khz) onto the
//     corresponding Host methods, formatting time as `m:ss` etc.
//
//   * `dispatchAction(action, host, embedder)` — maps Wasabi's
//     standard action keywords (PLAY, PAUSE, STOP, EJECT, CLOSE,
//     MINIMIZE, NEXT, PREV) onto Host methods.  EJECT calls
//     `host->pickFile(embedder)`.  CLOSE / MINIMIZE call the
//     matching virtual on Host (which the embedder overrides if it
//     wants window-control to actually do something).  Returns
//     `true` when the action was recognised + handled.
//
// Embedders implement Host once against their actual audio engine
// and window, and qtWasabi's defaults take care of the rest of the
// skin convention.
//

#include <QtCore/qglobal.h>
#include <QImage>
#include <QString>
#include <QUrl>
#include <functional>

class QWidget;

namespace WasabiQt {

class Host {
public:
    virtual ~Host() = default;

    // ── Audio state (read) ──────────────────────────────────────
    virtual qint64  positionMs()   const = 0;        // current play position
    virtual qint64  durationMs()   const = 0;        // total duration
    virtual int     bitrate()      const { return 0; }      // kbps; 0 = unknown
    virtual int     sampleRate()   const { return 0; }      // Hz;   0 = unknown
    virtual int     channelCount() const { return 0; }      // 1=mono 2=stereo, 0=unknown
    virtual int     volume()       const { return 100; }    // 0..100 linear
    virtual bool    isPlaying()    const = 0;
    virtual bool    isPaused()     const = 0;

    // ── Track metadata (read) ───────────────────────────────────
    virtual QString songTitle()    const = 0;
    virtual QString songFilename() const { return songTitle(); }

    // ── Transport actions (write) ───────────────────────────────
    virtual void    play()  = 0;
    virtual void    pause() = 0;
    virtual void    stop()  = 0;
    virtual void    next()  {}
    virtual void    prev()  {}
    virtual void    seekMs(qint64 ms) { Q_UNUSED(ms); }
    virtual void    setVolume(int v)  { Q_UNUSED(v); }

    // ── EJECT — pick a file to play.  Default opens a QFileDialog
    //    parented to `embedder` and returns the chosen URL, or an
    //    empty QUrl if the user cancels.
    virtual QUrl    pickFile(QWidget *embedder = nullptr);

    // ── Window-control actions.  Defaults are no-ops that return
    //    false so dispatchAction can fall through.  Embedders that
    //    host the skin in a real window override these.
    virtual bool    close()    { return false; }
    virtual bool    minimize() { return false; }

    // ── Sliders.  Returns a normalised position [0..1] for the
    //    given action keyword, or a negative value if the action
    //    isn't slider-shaped.  The default handles VOLUME + SEEK
    //    + PAN from the standard audio-state methods; embedders
    //    override for EQ_BAND etc.
    virtual double sliderPosition(const QString &action) const;
    virtual void   setSliderPosition(const QString &action, double v);

    // ── Visualisation.  Returns the recent audio's normalised
    //    amplitude (RMS, [0..1]) so <vis> bars can bounce with the
    //    audio.  Default returns 0 (silent / no audio tap).
    //    Embedders that wire QAudioBufferOutput / similar override.
    virtual double audioLevel() const { return 0.0; }

    // ── Album art for <albumart> widgets.  Returns an embedded cover
    //    for the current track, or an empty QImage to fall back to
    //    the renderer's placeholder.  Default: empty.  Embedders
    //    integrate with their tag parser (e.g. TagLib / Qt6
    //    QMediaMetaData::CoverArtImage) and return the QImage here.
    virtual QImage albumArt() const { return QImage(); }
};

using DisplayResolver = std::function<QString(const QString &)>;

// Returns a DisplayResolver lambda that resolves Wasabi's standard
// `display=` keys onto live `host` state.  Pass `nullptr` to get a
// no-op resolver (every key returns "").
//
// Recognised keys (case-insensitive):
//   time, timeleft, duration  →  m:ss
//   songname, songtitle, songinfo, filename
//   kbps, bitrate              →  bitrate()
//   khz, samplerate            →  sampleRate() / 1000
//   volume                     →  volume() as integer percent
DisplayResolver makeDefaultDisplayResolver(Host *host);

// Dispatches a Wasabi action keyword to the right Host method.
// Returns true when handled.  Recognised keywords:
//
//   PLAY  PAUSE  STOP  EJECT  CLOSE  MINIMIZE  NEXT  PREV
//
// `embedder` is forwarded to `host->pickFile()` for EJECT so the
// dialog gets parented correctly.  Case-insensitive on `action`.
bool dispatchAction(const QString &action, Host *host,
                    QWidget *embedder = nullptr);

}  // namespace WasabiQt
