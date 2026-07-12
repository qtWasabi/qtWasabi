// HeadShell — the reference head's toplevel.  A real QML Window: on
// Wayfire/wlroots a C++-constructed QQuickWindow never gets a valid
// wl_surface commit; the QML engine's window lifecycle maps correctly.
import QtQuick
import QtQuick.Window

Window {
    flags: Qt.Window | Qt.FramelessWindowHint
    color: 'transparent'
    width: 354; height: 280
    visible: true
}
