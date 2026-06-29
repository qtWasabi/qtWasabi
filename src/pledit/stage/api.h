// qtamp trimmed api.h: draw_pe.cpp uses nothing from the real Wasabi
// service aggregator (getStringW comes from Main.h).  Empty stub avoids
// pulling URLManager/PaletteManager/the skin API tail (mostly absent in
// this drop).  If a ported TU needs a WASABI_API_* service, add it here.
#ifndef __WASABI_API_H
#define __WASABI_API_H
// draw_pe.cpp:78 calls playlistTextFeed->UpdateText(...).  Provide just
// that minimal surface (the real PlaylistTextFeed lives in feeds.h with
// the full Wasabi service tail we deliberately skip).  The shim TU
// defines a non-null instance so the call is a safe no-op.
class PlaylistTextFeed {
public:
    void UpdateText(const wchar_t *text, int length);
};
extern PlaylistTextFeed *playlistTextFeed;
#endif
