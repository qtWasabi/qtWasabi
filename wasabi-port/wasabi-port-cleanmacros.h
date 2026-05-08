// Pop the min/max macros that bfc/platform/linux.h drops on the
// global namespace.  Include AFTER any Wasabi header in TUs that
// also pull <algorithm>, <format>, or any other STL header that
// uses std::min / std::max.
#pragma once
#ifdef min
#  undef min
#endif
#ifdef max
#  undef max
#endif
