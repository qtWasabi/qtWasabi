#pragma once
//
// HeaderWindow — wasabi-compat WindowObject for the standalone
// Win32 Header control (HDM_*).  gen_ml's owner-drawn ListView
// creates a synthetic Header HWND so it can drive column sort
// indicators / drag-resize hooks via SendMessage.
//
// Ships the minimal surface: column count tracking,
// HDM_INSERTITEMW / DELETEITEM bookkeeping, and HDM_GETITEMRECT
// returning a sensible width.  Real visual sync between this
// Header and the ListView's column band is a separate composition
// step.
//

#include <handle-registry.h>
#include <winuser.h>
#include <commctrl.h>

#include <QString>
#include <QList>

namespace qtWasabi {
namespace ml {

struct HdrColumn {
    QString text;
    int     width = 80;
    int     fmt   = HDF_LEFT | HDF_STRING;
};

class HeaderWindow : public qtWasabi::wasabi_compat::WindowObject {
public:
    HeaderWindow() = default;
    LRESULT wndProc(UINT msg, WPARAM wp, LPARAM lp) override;

private:
    QList<HdrColumn> m_columns;
};

HWND createHeader(HWND parent);

}  // namespace ml
}  // namespace qtWasabi
