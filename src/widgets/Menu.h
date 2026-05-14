#pragma once
//
// <Menu> — Wasabi menu-bar item.  Provides hit-test bounds + a state
// machine that toggles the visibility of three sibling widgets
// (referenced by `normal=` / `hover=` / `down=` attributes) based on
// mouse interaction:
//
//   normal.visible = !isSpawned
//   down.visible   =  isSpawned
//   hover.visible  = !isSpawned && inArea
//
// Mirrors `XuiMenu::updateObjects()` in real Wasabi's
// Src/Wasabi/api/skin/widgets/xuimenu.cpp.  Menu paints nothing
// itself — its visual state IS the visibility of the three sibling
// widgets, which are typically composed of layers + bitmaps.
//
// The widget claims clicks via `isInteractive()` so an unmapped
// `<Menu>` with no `id` attr still dispatches.  Spawning the actual
// popup menu (QMenu integration) is future work; the click-state
// visibility swap is enough to make menu buttons visibly respond
// to mouse interaction.
//

#include <WasabiQt/Widget.h>

namespace WasabiQt {

class MenuWidget : public Widget {
public:
    // Menu is inherently interactive even without an id — its
    // identity comes from the menu=/normal=/hover=/down= attrs.
    bool isInteractive() const override { return true; }

    void onLeftButtonDown(QPoint p, PaintCtx &ctx) override;
    void onLeftButtonUp  (QPoint p, PaintCtx &ctx) override;
    void onMouseMove     (QPoint p, PaintCtx &ctx) override;
    void onMouseLeave    (PaintCtx &ctx) override;

private:
    // Re-evaluate normal/hover/down sibling visibility from the
    // current m_isSpawned + m_inArea flags.  Looks up the three
    // siblings by id every call — the registry is constant-time so
    // this is cheap, and it avoids stale pointers if groups get
    // remapped at runtime.
    void updateObjects();

    bool m_isSpawned = false;
    bool m_inArea = false;
};

}  // namespace WasabiQt
