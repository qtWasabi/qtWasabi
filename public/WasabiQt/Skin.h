// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once

//
// WasabiQt::Skin — load a .wal skin and render it inside a QWidget.
//
// Embedder responsibility:
//   1. Implement WasabiQt::Host (playback/mixer/config bridge)
//   2. WasabiQt::Skin skin(host);
//   3. skin.load("/path/to/skin.wal")  — or an unpacked directory
//   4. Hand skin.widget() to your QMainWindow / QStackedWidget / etc.
//

#include <QString>
#include <QObject>
#include <memory>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace WasabiQt {

class Host;

class Skin : public QObject {
    Q_OBJECT
public:
    explicit Skin(Host *host, QObject *parent = nullptr);
    ~Skin() override;

    // Load a skin from a `.wal` archive (unpacked or zipped) or a
    // directory containing skin.xml at its root.  Returns true on
    // successful XML parse + Maki script load.
    bool load(const QString &path);

    // Reload the currently loaded skin.  No-op if none is loaded.
    bool reload();

    // The QWidget that hosts the skin's "main" container, normal
    // layout.  Embed in your QMainWindow.  Lifetime: same as Skin.
    QWidget *widget() const;

    // Currently loaded skin's display name (from skin.xml <skininfo>).
    QString skinName() const;

signals:
    // Maki script raised an event the host might care about (e.g.
    // a button widget firing an action keyword).  Forwards to
    // Host::onSkinEvent for embedders that prefer the virtual.
    void skinEvent(const QString &name);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace WasabiQt
