// Stub overlay: upstream has #error port me, but the VM layer
// only forwards Bitmap* pointers (never calls into them).
#pragma once
#define _BITMAP_H 1
#define _BITMAPI_H 1
#define __BITMAP_H 1

class Bitmap;
class BitmapI;
class AutoBitmap;
class IfcBitmap;
