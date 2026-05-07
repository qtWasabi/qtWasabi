// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

//
// titlebar_test — first regression target.  Loads the WACUP /
// Winamp Modern player skin, takes a 354×164 native render of the
// "main"/"normal" layout, and verifies the streak silver positions
// + WACUP text glyph extent match the canonical reference render.
//
// Reference values are pixel-counted from the WACUP author's own
// promotional render; those are the spec for "correct".
//
// Currently a stub — actual skin loading lands once src/Bootstrap
// is wired up.
//

#include <QTest>
#include <QtCore/QObject>

class TitlebarTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void leftStreakSilverWidth();
    void rightStreakSilverWidth();
    void textGlyphCenter();
    void cleanupTestCase();
};

void TitlebarTest::initTestCase()
{
    // TODO: instantiate Host stub + Skin, load player.wal, render to
    // QImage at 354×164, store as m_render.
    QSKIP("Skin loading not yet bootstrapped — stub test passes for now");
}

void TitlebarTest::leftStreakSilverWidth()
{
    // Reference: silver pixels at y=9 from x=24 to x=149 (125 wide).
    QSKIP("Skin loading not yet bootstrapped");
}

void TitlebarTest::rightStreakSilverWidth()
{
    // Reference: silver pixels at y=9 from x=205 to x=304 (100 wide).
    QSKIP("Skin loading not yet bootstrapped");
}

void TitlebarTest::textGlyphCenter()
{
    // Reference: WACUP glyphs span x=158..195 (37 wide), centred on
    // layout midline 176.5 (layout center 177).
    QSKIP("Skin loading not yet bootstrapped");
}

void TitlebarTest::cleanupTestCase()
{
}

QTEST_MAIN(TitlebarTest)
#include "titlebar_test.moc"
