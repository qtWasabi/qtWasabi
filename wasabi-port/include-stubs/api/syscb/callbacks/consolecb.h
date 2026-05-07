// Stub: vcpu.cpp's only Console::outputString call is inside a
// /* */ comment.  Upstream's consolecb.h is mostly empty too, but
// pulls in the syscbi.h chain we don't want.
#pragma once
#define _CONSOLECB_H 1

namespace ConsoleCallback {
  enum { DEBUGMESSAGE = 10 };
}

class Console {
public:
    static void outputString(int severity, const char *str);
};
