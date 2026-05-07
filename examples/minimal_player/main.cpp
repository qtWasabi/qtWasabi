// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

//
// Minimal WasabiQT embedder.  Demonstrates the WasabiQt::Host
// interface — every method here is a no-op or returns dummy data.
// A real player (Audacious, WACUP, custom Qt media player) would
// route these to its actual playback engine.
//

#include <QApplication>
#include <QMainWindow>
#include <WasabiQt/Host.h>
#include <WasabiQt/Skin.h>

class StubHost : public WasabiQt::Host {
public:
    int     playbackState() const override { return 0; }
    qint64  position()      const override { return 0; }
    qint64  duration()      const override { return 0; }
    int     bitrate()       const override { return 0; }
    int     sampleRate()    const override { return 0; }
    int     channels()      const override { return 0; }
    QString songTitle()     const override { return {}; }
    QString songArtist()    const override { return {}; }
    QString songAlbum()     const override { return {}; }
    QString currentFile()   const override { return {}; }
    int     volume()        const override { return 200; }
    int     balance()       const override { return 0; }
    bool    eqEnabled()     const override { return false; }
    bool    eqAutoEnabled() const override { return false; }
    int     eqBand(int)     const override { return 32; }
    int     eqPreamp()      const override { return 32; }
    const float *spectrum(int *n)  const override { if (n) *n = 0; return nullptr; }
    const float *waveform(int *n)  const override { if (n) *n = 0; return nullptr; }
    bool    shuffleOn()      const override { return false; }
    bool    repeatOn()       const override { return false; }
    bool    repeatTrack()    const override { return false; }
    bool    isPlaylistVisible()     const override { return false; }
    bool    isMediaLibraryVisible() const override { return false; }
    void    play()  override {}
    void    pause() override {}
    void    stop()  override {}
    void    next()  override {}
    void    prev()  override {}
    void    setPosition(qint64) override {}
    void    setVolume(int)      override {}
    void    setBalance(int)     override {}
    void    toggleEq()          override {}
    void    toggleEqAuto()      override {}
    void    setEqBand(int, int) override {}
    void    setEqPreamp(int)    override {}
    void    setShuffle(bool)    override {}
    void    cycleRepeat()       override {}
    void    showPlaylist(bool)        override {}
    void    showMediaLibrary(bool)    override {}
    void    openFileDialog()          override {}
    QVariant getConfig(const QString&, const QString&) const override { return {}; }
    void     setConfig(const QString&, const QString&, const QVariant&) override {}
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    StubHost host;
    WasabiQt::Skin skin(&host);

    if (argc > 1) {
        if (!skin.load(QString::fromLocal8Bit(argv[1]))) {
            qWarning("Failed to load skin: %s", argv[1]);
            // (Once the bootstrap lands, return non-zero on real failure.
            //  For now we keep going so the window pops up.)
        }
    } else {
        qWarning("Usage: %s /path/to/skin.wal", argv[0]);
    }

    QMainWindow win;
    win.setWindowTitle("WasabiQT minimal player");
    if (auto *w = skin.widget())
        win.setCentralWidget(w);
    win.resize(354, 280);
    win.show();

    return app.exec();
}
