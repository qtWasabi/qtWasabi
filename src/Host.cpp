// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/Host.h>

#include <QChar>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QWidget>

namespace WasabiQt {

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
            k == QStringLiteral("songinfo")) {
            const QString t = host->songTitle();
            return t.isEmpty()
                ? QStringLiteral("(no song loaded)")
                : t;
        }
        if (k == QStringLiteral("filename"))
            return host->songFilename();
        if (k == QStringLiteral("kbps") ||
            k == QStringLiteral("bitrate") ||
            k == QStringLiteral("songbitrate"))
            return QString::number(host->bitrate());
        if (k == QStringLiteral("khz") ||
            k == QStringLiteral("samplerate") ||
            k == QStringLiteral("songsamplerate"))
            return QString::number(host->sampleRate() / 1000);
        if (k == QStringLiteral("volume"))
            return QString::number(host->volume());
        return QString();
    };
}

bool dispatchAction(const QString &action, Host *host,
                    QWidget *embedder) {
    if (!host) return false;
    const QString a = action.toUpper();
    if (a == QLatin1String("PLAY"))     { host->play();  return true; }
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
    return false;
}

}  // namespace WasabiQt
