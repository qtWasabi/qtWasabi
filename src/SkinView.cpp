// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinView.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/SkinRuntime.h>
#include <WasabiQt/TreePainter.h>

#include <QMetaObject>
#include <QPainter>
#include <QPaintEvent>
#include <QPointer>

namespace WasabiQt {

SkinView::SkinView(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);

    // M14c: hook the runtime's repaint callback so script mutations
    // (setXmlParam from Maki) trigger a repaint. QPointer keeps the
    // closure safe even if the view dies before the callback fires,
    // and QueuedConnection routes the update to the GUI thread no
    // matter where dispatch ran.
    QPointer<SkinView> self(this);
    registerSkinRepaintCallback([self]() {
        if (auto *v = self.data()) {
            QMetaObject::invokeMethod(v, [v]() { v->update(); },
                                      Qt::QueuedConnection);
        }
    });
}

SkinView::~SkinView() {
    // M14c: clear the runtime callback so a freshly mutated widget
    // attr after this view dies doesn't reach into a dead lambda.
    registerSkinRepaintCallback({});
}

void SkinView::setActiveGammaset(const QString &name) {
    m_gammasets.setActiveGammaset(name);
    m_registry.setGammasetRegistry(&m_gammasets);  // clears tint cache
    update();
}

bool SkinView::load(const SkinXml::Document &doc,
                    const QString &containerId,
                    const QString &layoutId,
                    QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);
    m_gammasets.loadFromDocument(doc);
    m_registry.setGammasetRegistry(&m_gammasets);

    // Native size: prefer explicit w/h, fall back to minimum_w/h.
    auto attrInt = [&](const QString &k, int def = 0) {
        auto it = m_tree.attrs.constFind(k);
        if (it == m_tree.attrs.constEnd()) return def;
        bool ok = false;
        int v = it.value().toInt(&ok);
        return ok ? v : def;
    };
    int w = attrInt(QStringLiteral("w"));
    int h = attrInt(QStringLiteral("h"));
    if (w <= 0) w = attrInt(QStringLiteral("minimum_w"), 354);
    if (h <= 0) h = attrInt(QStringLiteral("minimum_h"), 280);
    m_nativeSize = QSize(w, h);
    resize(m_nativeSize);
    update();
    return true;
}

void SkinView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Qt::transparent);
    if (m_host) {
        // Host route — pulls live display strings + slider
        // positions straight from the embedder's Host impl.  When
        // the embedder also set a display resolver explicitly, the
        // Host route still uses qtWasabi's default mapping; the
        // explicit resolver only matters in the no-host case.
        TreePainter::paintTree(&p, m_tree, m_registry, m_fonts,
                                size(), m_host);
    } else {
        TreePainter::paintTree(&p, m_tree, m_registry, m_fonts,
                                size(), m_resolver);
    }
}

}  // namespace WasabiQt
