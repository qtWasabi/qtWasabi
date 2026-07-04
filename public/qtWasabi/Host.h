// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// qtWasabi::Host — abstract player-side interface that Modern skins
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
#include <QList>
#include <QString>
#include <QUrl>
#include <functional>

class QWidget;

namespace qtWasabi {

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
    // Rich per-field metadata for the skin's file-info display
    // (fileinfo.maki reads these via System.getPlayItemMetaDataString).
    // `field` is the canonical Wasabi/Winamp tag name, lower-case:
    // "title", "artist", "album", "albumartist", "track", "year",
    // "genre", "disc", "composer", "publisher", "streamgenre", …
    // Default empty → the field's line stays hidden (idle behaviour).
    virtual QString playItemMetaData(const QString &field) const {
        Q_UNUSED(field);
        return {};
    }
    // Display title (artist - title style) the skin shows as the
    // primary track label; falls back to the plain title.
    virtual QString playItemDisplayTitle() const { return songTitle(); }
    // Decoder/codec name shown on the "Decoder:" line (e.g. the MPEG
    // audio decoder description).  Empty → line hidden.
    virtual QString decoderName() const { return {}; }

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
    virtual bool    maximize() { return false; }
    // Toggle "shade" / compact mode — many skins map this to the
    // middle titlebar button.  Default no-op-true so dispatchAction
    // consumes the click instead of falling through to a window drag.
    virtual bool    toggleShade() { return true; }
    // Show the system menu / context menu.  Default no-op-true.
    virtual bool    showSystemMenu(QWidget *embedder = nullptr) {
        Q_UNUSED(embedder); return true;
    }

    // ── Sliders.  Returns a normalised position [0..1] for the
    //    given action keyword, or a negative value if the action
    //    isn't slider-shaped.  The default handles VOLUME + SEEK
    //    + PAN from the standard audio-state methods; embedders
    //    override for EQ_BAND etc.
    //
    //    `param` is the slider's XML `param=` attr (empty when the
    //    slider doesn't carry one).  Required for `EQ_BAND` where
    //    `param` names the band ("1".."10" or "preamp"), and for
    //    any future param-aware slider action.  Hosts that don't
    //    care about per-band routing leave the default
    //    implementations alone — they forward to the no-param
    //    overload below, which is what every pre-existing host
    //    override hooked into.
    virtual double sliderPosition(const QString &action,
                                   const QString &param) const {
        Q_UNUSED(param);
        return sliderPosition(action);
    }
    virtual void   setSliderPosition(const QString &action, double v,
                                      const QString &param) {
        Q_UNUSED(param);
        setSliderPosition(action, v);
    }
    virtual double sliderPosition(const QString &action) const;
    virtual void   setSliderPosition(const QString &action, double v);

    // ── Visualisation.  Returns the recent audio's normalised
    //    amplitude (RMS, [0..1]) so <vis> bars can bounce with the
    //    audio.  Default returns 0 (silent / no audio tap).
    //    Embedders that wire QAudioBufferOutput / similar override.
    virtual double audioLevel() const { return 0.0; }

    // ── Spectrum / oscilloscope / VU snapshots used by VisWidget
    //    (modern skin) and by the classic-skin display.  Default
    //    implementations return nullptr / 0 so embedders that don't
    //    decode the audio buffer fall back to the chrome-only paint.
    //
    //    Sizes:
    //      spectrum: 19 floats, log-scaled per-band magnitudes [0..1]
    //      oscilloscope: 75 floats, raw waveform samples [-1..1]
    //    VU left/right: [0..1] RMS per channel.
    virtual const float *spectrumData() const { return nullptr; }
    virtual const float *peakData()     const { return nullptr; }
    virtual const float *oscData()      const { return nullptr; }
    virtual float vuLeft()  const { return 0.0f; }
    virtual float vuRight() const { return 0.0f; }
    virtual bool peaksVisible() const { return true; }

    // ── Album art for <albumart> widgets.  Returns an embedded cover
    //    for the current track, or an empty QImage to fall back to
    //    the renderer's placeholder.  Default: empty.  Embedders
    //    integrate with their tag parser (e.g. TagLib / Qt6
    //    QMediaMetaData::CoverArtImage) and return the QImage here.
    virtual QImage albumArt() const { return QImage(); }

    // ── Playlist accessors used by the engine-level <playlistpro>
    //    renderer (and any windowholder embedding the canonical
    //    Playlist Editor GUID `{45F3F7C1-A6F3-4ee6-A15E-125E92FC3F8D}`).
    //    Defaults give an empty playlist; embedders override
    //    forwarding to their own playlist data model.
    virtual int     playlistRowCount() const                  { return 0; }
    virtual QString playlistRowText(int row) const            { Q_UNUSED(row); return {}; }
    virtual qint64  playlistRowDurationMs(int row) const      { Q_UNUSED(row); return 0; }
    virtual int     playlistCurrentRow() const                { return -1; }
    virtual void    playlistSetCurrentRow(int row)            { Q_UNUSED(row); }
    virtual void    playlistPlayRow(int row)                  { Q_UNUSED(row); }

    // Playlist-editor chrome buttons (the modern skin's Wasabi
    // `action="PE_Add|PE_Rem|PE_Sel|PE_Misc|PE_List"` buttons).  The verb
    // is the UPPERCASED action; embedders pop the matching menu (Add /
    // Remove / Select / Misc / Manage Playlist) at the cursor and run the
    // chosen operation on their playlist model.  Default: unhandled.
    virtual bool    pleditCommand(const QString &verb)        { Q_UNUSED(verb); return false; }

    // ── Library tree accessors used by the engine-level
    //    <playlistdirectory> renderer (and any windowholder
    //    embedding the canonical Media Library GUID
    //    `{6B0EDF80-C9A5-11D3-9F26-00C04F39FFC6}`).  `parent` is an
    //    opaque path-shaped token; the empty string means "root".
    //    Embedders typically forward to QFileSystemModel or a real
    //    tag-indexed library.  Default: empty tree.
    virtual int     libraryRowCount(const QString &parent) const         { Q_UNUSED(parent); return 0; }
    virtual QString libraryRowLabel(const QString &parent, int row) const { Q_UNUSED(parent); Q_UNUSED(row); return {}; }
    virtual QString libraryRowPath (const QString &parent, int row) const { Q_UNUSED(parent); Q_UNUSED(row); return {}; }
    virtual bool    libraryRowHasChildren(const QString &parent, int row) const { Q_UNUSED(parent); Q_UNUSED(row); return false; }

    // ── Media Library (artist → album → track) accessors used by the
    //    gen_ml Media Library renderer's three content panes.  Rows are
    //    plain value structs the embedder fills from its tag-indexed
    //    library (qtamp backs this with a DuckDB + Parquet index).  An
    //    empty `artist`/`album` means "all".  Default: empty library.
    struct MlArtistRow { QString name; int albumCount = 0; int trackCount = 0; };
    struct MlAlbumRow  { QString name; int year = 0; int trackCount = 0; };
    struct MlTrackRow  {
        QString artist, album, title, genre;
        int     track = 0, year = 0;
        qint64  lengthMs = 0;
        QString path;
    };
    virtual QList<MlArtistRow> mlArtists() const { return {}; }
    virtual QList<MlAlbumRow>  mlAlbums(const QString &artist) const { Q_UNUSED(artist); return {}; }
    virtual QList<MlTrackRow>  mlTracks(const QString &artist, const QString &album) const { Q_UNUSED(artist); Q_UNUSED(album); return {}; }
    virtual int                mlTotalTracks() const { return 0; }

    // ── Generic Media Library filter/query surface.  ml_local's view
    //    presets ("Artist\Album", "Genre\Artist\Album", …) browse the
    //    library through a CHAIN of filter panes; each pane lists the
    //    distinct values of one tag field, narrowed by the selections in
    //    the panes before it.  `equals` carries those upstream
    //    (field, value) selections.  Fields: "artist" (album-artist
    //    folded, guest suffixes stripped), "albumartist", "album",
    //    "genre", "year".  `countField` names the field whose DISTINCT
    //    count fills the pane's second column ("" ⇒ track count).
    struct MlFilterRow { QString name; int count = 0; };
    virtual QList<MlFilterRow> mlFilterValues(
        const QString &field, const QString &countField,
        const QList<QPair<QString, QString>> &equals) const {
        Q_UNUSED(field); Q_UNUSED(countField); Q_UNUSED(equals);
        return {};
    }
    // Track query joining the filter selections with one of ml_local's
    // default smart views.  The numeric ids mirror the stock queries:
    //   0 All            (Audio: type = 0)
    //   1 Video          (type = 1)
    //   2 MostPlayed     (playcount > 0)
    //   3 RecentlyAdded  (dateadded > [3 days ago])
    //   4 RecentlyPlayed (lastplay > [2 weeks ago])
    //   5 NeverPlayed    (playcount = 0 | playcount isempty)
    //   6 TopRated       (rating >= 3)
    enum MlSmartView {
        MlViewAll = 0, MlViewVideo, MlViewMostPlayed, MlViewRecentlyAdded,
        MlViewRecentlyPlayed, MlViewNeverPlayed, MlViewTopRated
    };
    virtual QList<MlTrackRow> mlTracksQuery(
        const QList<QPair<QString, QString>> &equals, int smartView) const {
        Q_UNUSED(equals); Q_UNUSED(smartView);
        return {};
    }

    // "Media Library Preferences..." from gen_ml's Library button menu.
    // Default: no-op (embedder without a preferences dialog).
    virtual void mlShowPreferences() {}

    // Send Media Library tracks to the player.  `paths` is the current
    // view (e.g. the visible track grid); the embedder appends them to
    // its playlist and, unless `enqueueOnly`, starts playback from
    // `startRow`.  Wired from the ML renderer's double-click and Play
    // button.  Default: no-op.
    virtual void mlPlayTracks(const QList<QString> &paths, int startRow,
                              bool enqueueOnly) {
        Q_UNUSED(paths); Q_UNUSED(startRow); Q_UNUSED(enqueueOnly);
    }
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

}  // namespace qtWasabi
