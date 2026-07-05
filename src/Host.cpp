// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <qtWasabi/Host.h>

#include <QChar>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QWidget>

namespace qtWasabi {

namespace {
QString fmtMs(qint64 ms) {
    if (ms < 0) ms = 0;
    const qint64 sec = ms / 1000;
    // Match Winamp's BIGNUM time-display convention: MM:SS with
    // zero-padded minutes (e.g. "00:02").  Hours fold into minutes —
    // tracks rarely exceed 99 minutes, and the LCD display widgets
    // can clip if they do.
    return QStringLiteral("%1:%2")
        .arg(sec / 60, 2, 10, QChar('0'))
        .arg(sec % 60, 2, 10, QChar('0'));
}
}  // namespace

double Host::sliderPosition(const QString &action) const {
    const QString a = action.toUpper();
    if (a == QLatin1String("VOLUME"))
        return qBound(0.0, volume() / 100.0, 1.0);
    if (a == QLatin1String("SEEK") || a == QLatin1String("SEEKBAR")) {
        const qint64 dur = durationMs();
        if (dur <= 0) return 0.0;
        return qBound(0.0, double(positionMs()) / double(dur), 1.0);
    }
    if (a == QLatin1String("PAN")) return 0.5;     // centred
    return -1.0;                                    // unknown
}

void Host::setSliderPosition(const QString &action, double v) {
    v = qBound(0.0, v, 1.0);
    const QString a = action.toUpper();
    if (a == QLatin1String("VOLUME")) {
        setVolume(int(v * 100));
        return;
    }
    if (a == QLatin1String("SEEK") || a == QLatin1String("SEEKBAR")) {
        const qint64 dur = durationMs();
        if (dur > 0) seekMs(qint64(v * dur));
        return;
    }
}

QUrl Host::pickFile(QWidget *embedder) {
    const QString musicDir = QStandardPaths::writableLocation(
        QStandardPaths::MusicLocation);
    const QString path = QFileDialog::getOpenFileName(
        embedder,
        QStringLiteral("Open audio file"),
        musicDir,
        QStringLiteral(
            "Audio (*.mp3 *.flac *.ogg *.opus *.wav *.m4a *.aac);;"
            "All files (*)"));
    if (path.isEmpty()) return QUrl();
    return QUrl::fromLocalFile(path);
}

DisplayResolver makeDefaultDisplayResolver(Host *host) {
    if (!host) {
        return [](const QString &) { return QString(); };
    }
    return [host](const QString &key) -> QString {
        const QString k = key.toLower();
        if (k == QStringLiteral("time"))
            return fmtMs(host->positionMs());
        if (k == QStringLiteral("duration"))
            return fmtMs(host->durationMs());
        if (k == QStringLiteral("timeleft")) {
            const qint64 left =
                qMax<qint64>(0, host->durationMs() - host->positionMs());
            return QStringLiteral("-") + fmtMs(left);
        }
        if (k == QStringLiteral("songname")  ||
            k == QStringLiteral("songtitle") ||
            k == QStringLiteral("songinfo")  ||
            k == QStringLiteral("songticker")) {
            // Winamp's classic marquee format: "N. <title> (M:SS)"
            // whenever a playlist entry is current, matching how the
            // reference engine composes the main window title.  Bare
            // title when nothing is enqueued, empty when no track is
            // loaded (real Winamp shows an empty songticker then).
            const QString title = host->playItemDisplayTitle();
            const int row = host->playlistCurrentRow();
            if (row >= 0 && !title.isEmpty()) {
                QString s = QString::number(row + 1) + QStringLiteral(". ")
                            + title;
                const qint64 dur = host->durationMs();
                if (dur > 0)
                    s += QStringLiteral(" (") + fmtMs(dur)
                         + QStringLiteral(")");
                return s;
            }
            return title.isEmpty() ? host->songTitle() : title;
        }
        if (k == QStringLiteral("filename"))
            return host->songFilename();
        // Empty when there is no meaningful value (stopped / nothing
        // decoded) — the widget's authored deftext placeholder
        // ("(___)" on the Modern kbps field) shows instead.
        if (k == QStringLiteral("kbps") ||
            k == QStringLiteral("bitrate") ||
            k == QStringLiteral("songbitrate")) {
            const int b = host->bitrate();
            return b > 0 ? QString::number(b) : QString();
        }
        if (k == QStringLiteral("khz") ||
            k == QStringLiteral("samplerate") ||
            k == QStringLiteral("songsamplerate") ||
            k == QStringLiteral("frequency")) {
            const int s = host->sampleRate() / 1000;
            return s > 0 ? QString::number(s) : QString();
        }
        if (k == QStringLiteral("volume"))
            return QString::number(host->volume());
        return QString();
    };
}

bool dispatchAction(const QString &action, Host *host,
                    QWidget *embedder) {
    if (!host) return false;
    const QString a = action.toUpper();
    if (a == QLatin1String("PLAY")) {
        // Winamp parity: PLAY with nothing loaded (empty playlist and no
        // current file) opens a file to play instead of being a silent
        // no-op.  Skins whose only transport is PLAY — no EJECT button,
        // like HeadAMP — can then still start a song.
        if (host->playlistRowCount() == 0 && host->songFilename().isEmpty())
            host->pickFile(embedder);
        else
            host->play();
        return true;
    }
    if (a == QLatin1String("PAUSE"))    { host->pause(); return true; }
    if (a == QLatin1String("STOP"))     { host->stop();  return true; }
    if (a == QLatin1String("NEXT"))     { host->next();  return true; }
    if (a == QLatin1String("PREV"))     { host->prev();  return true; }
    if (a == QLatin1String("EJECT")) {
        const QUrl u = host->pickFile(embedder);
        if (!u.isEmpty()) {
            // Convention: pickFile is the embedder's hook; the
            // embedder's Host::pickFile typically also kicks
            // playback (since EJECT historically loads + plays).
            // We don't auto-play here so embedders that want a
            // "load only" semantic can override pickFile to return
            // the URL without starting playback.  See QtampHost.
        }
        return true;
    }
    if (a == QLatin1String("CLOSE"))    return host->close();
    if (a == QLatin1String("MINIMIZE")) return host->minimize();
    if (a == QLatin1String("MAXIMIZE")) return host->maximize();
    // SWITCH = winshade toggle (compact mode).  Real Winamp shrinks
    // the player to a thin strip; absent that, mark the click
    // handled so it doesn't fall through to a window drag.
    if (a == QLatin1String("SWITCH"))   return host->toggleShade();
    if (a == QLatin1String("SHADE"))    return host->toggleShade();
    // SYSMENU = open the system menu (right-click style).  Default
    // routes to Host::showSystemMenu(embedder) so embedders that
    // ship a QMenu can pop it; default returns true to consume.
    if (a == QLatin1String("SYSMENU"))  return host->showSystemMenu(embedder);
    // MENU/MENUHOTKEY_* are menubar-driven; default-consume so they
    // don't fall through.  Embedders that ship a real menubar
    // override these (e.g. via dispatchAction in their subclass).
    if (a == QLatin1String("MENU"))     return true;
    if (a.startsWith(QLatin1String("MENUHOTKEY_")))    return true;
    if (a.startsWith(QLatin1String("ML_MENUHOTKEY_"))) return true;
    if (a.startsWith(QLatin1String("PL_MENUHOTKEY_"))) return true;
    // VID_FS = toggle fullscreen video.  Default no-op-consume.
    if (a == QLatin1String("VID_FS"))   return true;
    // Playlist-editor chrome buttons → embedder's playlist menus.
    if (a == QLatin1String("PE_ADD")  || a == QLatin1String("PE_REM")  ||
        a == QLatin1String("PE_SEL")  || a == QLatin1String("PE_MISC") ||
        a == QLatin1String("PE_LIST"))
        return host->pleditCommand(a);
    return false;
}

}  // namespace qtWasabi
