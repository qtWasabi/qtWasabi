#pragma once
//
// TextPainter — paint a Wasabi <text> widget using a bitmap font.
//
// `<text>` honours: `font`, `fontsize`, `x`, `y`, `w`, `h`,
//                    `align` (left/center/right), `display`,
//                    `default`, `forceuppercase`, `xoffset`, `yoffset`.
//
// `display=` names a runtime-bound value the host supplies (time,
// Bitrate, Frequency, song title, etc.).  When the host hasn't
// set a value, we render `default=` instead.
//

#include <QtCore/qglobal.h>
#include <QHash>
#include <QString>

class QPainter;
class QSize;

namespace WasabiQt {

class BitmapRegistry;
class FontRegistry;

namespace TextPainter {

// Resolve a `display=` key.  Returning an empty string means
// "fall back to the `default` attribute".
using DisplayResolver =
    std::function<QString(const QString &displayKey)>;

// Paint the <text> defined by `attrs` onto `p` using `fontReg` for
// glyph metrics + `bmpReg` for the underlying char-table bitmaps.
// `containerSize` is the parent's pixel size (for relat*).
// `resolver`, if set, is called for any non-empty `display=` to
// fetch the live string.
bool paintText(QPainter *p,
               FontRegistry &fontReg, BitmapRegistry &bmpReg,
               const QHash<QString, QString> &attrs,
               const QSize &containerSize,
               const DisplayResolver &resolver = {});

}  // namespace TextPainter
}  // namespace WasabiQt
