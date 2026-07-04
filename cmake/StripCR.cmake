# StripCR.cmake: normalize CRLF line endings to LF in the file passed as
# -DIN=<path>. The archive.org Wasabi source ships Windows CRLF; the
# vcpu patch context is LF, and GNU patch refuses hunks whose line
# endings differ even with --ignore-whitespace (BSD patch on macOS is
# lenient, which is why only Linux fresh builds hit this).
file(READ "${IN}" _c)
string(REPLACE "\r\n" "\n" _c "${_c}")
file(WRITE "${IN}" "${_c}")
