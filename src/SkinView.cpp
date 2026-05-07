// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include <WasabiQt/SkinView.h>
#include <WasabiQt/SkinXml.h>
#include <WasabiQt/TreePainter.h>

#include <QPainter>
#include <QPaintEvent>

namespace WasabiQt {

SkinView::SkinView(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

SkinView::~SkinView() = default;

bool SkinView::load(const SkinXml::Document &doc,
                    const QString &containerId,
                    const QString &layoutId,
                    QString *errMsg) {
    if (!Layout::expandLayout(doc, containerId, layoutId, m_tree, errMsg))
        return false;
    m_registry.loadFromDocument(doc);
    m_fonts.loadFromDocument(doc);

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
    TreePainter::paintTree(&p, m_tree, m_registry, m_fonts, size(),
                            m_resolver);
}

}  // namespace WasabiQt
