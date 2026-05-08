// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// wq_view — minimal skin viewer.  Opens a Wasabi/Modern skin's
// container/layout in a frameless QWidget so you can see what
// WasabiQT actually renders for a real skin.
//
// Usage:
//
//   wq_view <skin.xml> [container] [layout]
//
//   container defaults to "main", layout to "normal".
//
// Drag from the title strip to move; close on Escape.

#include <WasabiQt/SkinView.h>
#include <WasabiQt/SkinXml.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStringList>

class ViewerWindow : public WasabiQt::SkinView {
public:
    using SkinView::SkinView;
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            m_dragOrigin = e->globalPosition().toPoint() -
                           frameGeometry().topLeft();
            m_dragging   = true;
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (m_dragging && (e->buttons() & Qt::LeftButton))
            move(e->globalPosition().toPoint() - m_dragOrigin);
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_dragging = false; }
    void keyPressEvent(QKeyEvent *e) override {
        if (e->key() == Qt::Key_Escape) close();
        else QWidget::keyPressEvent(e);
    }
private:
    QPoint m_dragOrigin;
    bool   m_dragging = false;
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("wq_view"));

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral(
        "WasabiQT skin viewer — opens a container's layout"));
    cli.addHelpOption();
    cli.addPositionalArgument(QStringLiteral("skin.xml"),
                              QStringLiteral("path to the skin's skin.xml"));
    cli.addPositionalArgument(QStringLiteral("container"),
                              QStringLiteral("container id (default: main)"),
                              QStringLiteral("[container]"));
    cli.addPositionalArgument(QStringLiteral("layout"),
                              QStringLiteral("layout id (default: normal)"),
                              QStringLiteral("[layout]"));
    cli.process(app);

    const QStringList args = cli.positionalArguments();
    if (args.isEmpty()) {
        qCritical("Usage: wq_view <skin.xml> [container=main] [layout=normal]");
        return 2;
    }
    const QString skinPath = args.value(0);
    const QString containerId = args.value(1, QStringLiteral("main"));
    const QString layoutId    = args.value(2, QStringLiteral("normal"));

    WasabiQt::SkinXml::Document doc;
    QString err;
    if (!WasabiQt::SkinXml::parse(skinPath, doc, &err)) {
        qCritical("Skin parse failed: %s", qPrintable(err));
        return 1;
    }

    ViewerWindow w;
    w.setWindowFlags(Qt::FramelessWindowHint);
    w.setAttribute(Qt::WA_TranslucentBackground);
    if (!w.load(doc, containerId, layoutId, &err)) {
        qCritical("Layout load failed: %s", qPrintable(err));
        return 1;
    }
    w.setWindowTitle(QStringLiteral("WasabiQT — %1 / %2 / %3")
                         .arg(doc.skinName.isEmpty()
                                  ? QStringLiteral("(unnamed)")
                                  : doc.skinName,
                              containerId, layoutId));
    w.show();
    return app.exec();
}
