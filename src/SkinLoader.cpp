// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/Skin.h>
#include <WasabiQt/Host.h>

#include <QWidget>
#include <QString>

namespace WasabiQt {

class Skin::Private {
public:
    Host    *host = nullptr;
    QWidget *widget = nullptr;
    QString  loadedPath;
    QString  skinName;
};

Skin::Skin(Host *host, QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->host = host;
}

Skin::~Skin() = default;

bool Skin::load(const QString &path)
{
    d->loadedPath = path;
    // TODO: wire to Wasabi's SkinParser via Bootstrap once the
    // service registry is up.  For now we register the path and
    // return false — real loading happens in the next milestone.
    return false;
}

bool Skin::reload()
{
    if (d->loadedPath.isEmpty()) return false;
    return load(d->loadedPath);
}

QWidget *Skin::widget() const  { return d->widget; }
QString  Skin::skinName() const { return d->skinName; }

} // namespace WasabiQt
