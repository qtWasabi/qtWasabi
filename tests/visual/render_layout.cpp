// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// render_layout — offscreen renderer for visual regression.
//
// Usage:
//
//   QT_QPA_PLATFORM=offscreen render_layout <skin.xml> <out.png>
//                              [--theme NAME]
//                              [--container ID]   (default: main)
//                              [--layout    ID]   (default: normal)
//                              [--w PX --h PX]    (default: 354 280)
//                              [--display key=value]*
//
// The whole point of this binary: capture a deterministic PNG of a
// skin's layout WITHOUT opening a window, so visual regressions can
// be diff'd against committed `tests/visual/expected/*.png` golden
// images in CI — no display server required.

#include <WasabiQt/SkinXml.h>
#include <WasabiQt/Layout.h>
#include <WasabiQt/BitmapRegistry.h>
#include <WasabiQt/FontRegistry.h>
#include <WasabiQt/GammasetRegistry.h>
#include <WasabiQt/TreePainter.h>

#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <cstdio>

using namespace WasabiQt;

static void usage() {
    std::fprintf(stderr,
        "Usage: render_layout <skin.xml> <out.png> [options]\n"
        "  --theme NAME        gammaset to apply (Default | Blues | …)\n"
        "  --container ID      container id (default: main)\n"
        "  --layout ID         layout id (default: normal)\n"
        "  --w PX --h PX       canvas size (default: 354x280)\n"
        "  --display key=val   bind one <text display=key/> resolver entry\n");
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);

    QString skinPath, outPath;
    QString theme = QStringLiteral("Default");
    QString containerId = QStringLiteral("main");
    QString layoutId    = QStringLiteral("normal");
    int width = 354, height = 280;
    QHash<QString, QString> displayBindings;

    QStringList pos;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--theme"     && i + 1 < argc) theme       = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--container" && i + 1 < argc) containerId = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--layout"    && i + 1 < argc) layoutId    = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--w"  && i + 1 < argc) width  = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == "--h"  && i + 1 < argc) height = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == "--display" && i + 1 < argc) {
            const QString kv = QString::fromLocal8Bit(argv[++i]);
            const int eq = kv.indexOf(QChar('='));
            if (eq > 0) displayBindings.insert(kv.left(eq), kv.mid(eq + 1));
        }
        else pos.append(a);
    }
    if (pos.size() < 2) { usage(); return 2; }
    skinPath = pos[0];
    outPath  = pos[1];

    SkinXml::Document doc;
    QString err;
    if (!SkinXml::parse(skinPath, doc, &err)) {
        std::fprintf(stderr, "parse: %s\n", qPrintable(err));
        return 1;
    }

    Layout::ResolvedWidget tree;
    if (!Layout::expandLayout(doc, containerId, layoutId, tree, &err)) {
        std::fprintf(stderr, "expand: %s\n", qPrintable(err));
        return 1;
    }

    BitmapRegistry bmp;     bmp.loadFromDocument(doc);
    FontRegistry   fonts;   fonts.loadFromDocument(doc);
    GammasetRegistry gs;    gs.loadFromDocument(doc);
    gs.setActiveGammaset(theme);
    bmp.setGammasetRegistry(&gs);

    QImage out(QSize(width, height), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        TreePainter::DisplayResolver resolver = [&](const QString &k) {
            return displayBindings.value(k);
        };
        TreePainter::paintTree(&p, tree, bmp, fonts,
                                QSize(width, height), resolver);
    }
    if (!out.save(outPath)) {
        std::fprintf(stderr, "save: %s\n", qPrintable(outPath));
        return 1;
    }
    std::fprintf(stdout, "wrote %s (%dx%d, theme=%s)\n",
                 qPrintable(outPath), width, height, qPrintable(theme));
    return 0;
}
