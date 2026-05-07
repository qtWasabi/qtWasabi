// Stub for upstream <tataki/canvas/Canvas.h>.  Upstream's
// `Src/tataki/canvas/canvas.h` is lowercase and only includes win/
// or mac/ canvas headers — there is no Linux variant.  vcpu.cpp /
// scriptmgr.cpp transitively include this through api/wnd/api_window.h
// but never actually call any Canvas method, so a forward declaration
// is enough to satisfy the parser.
#pragma once

class Canvas;
class BltCanvas;
class PaintCanvas;
class IfcCanvas;
