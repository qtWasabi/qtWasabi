// Stub for nu/AutoWide.h.  Upstream is Win32-only (uses MultiByteToWideChar).
// Linux equivalent: convert via mbstowcs.
//
// vcpu.cpp uses AutoWide(s) as a temporary that decays to wchar_t* in
// WCSDUP() — `WCSDUP(AutoWide(functionName))`.  The destructor frees
// the buffer, so it must out-live the WCSDUP call (implicit in the
// expression).
#ifndef AUTOWIDEH
#define AUTOWIDEH

#include <cstdlib>
#include <cstring>
#include <cwchar>

class AutoWide {
public:
    explicit AutoWide(const char *src) {
        if (!src) { wide = nullptr; return; }
        size_t n = std::mbstowcs(nullptr, src, 0);
        if (n == (size_t)-1) { wide = nullptr; return; }
        wide = static_cast<wchar_t *>(std::malloc((n + 1) * sizeof(wchar_t)));
        if (wide) std::mbstowcs(wide, src, n + 1);
    }
    ~AutoWide() { std::free(wide); }
    operator const wchar_t *() const { return wide; }
    operator       wchar_t *()       { return wide; }
private:
    wchar_t *wide = nullptr;
    AutoWide(const AutoWide &) = delete;
    AutoWide &operator=(const AutoWide &) = delete;
};

#endif
