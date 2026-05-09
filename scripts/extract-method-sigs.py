#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Florian Kleber and qtWasabi contributors
#
# extract-method-sigs.py — scan one or more Wasabi ScriptObject .cpp
# files for the addFunction / EXPORTED_FUNCTION style declarations the
# upstream uses to register a script method's name and nparams, and
# emit {L"name", N} lines suitable for pasting into kKnownMethods in
# wasabi-port/wasabi-port-link-stubs.cpp.
#
# Usage:
#
#   ./extract-method-sigs.py path/to/file.cpp [more.cpp ...]
#   ./extract-method-sigs.py /path/to/wasabi-src/Src/Wasabi/api/script/objects
#
# Directories are walked recursively; only *.cpp files are scanned.
#
# Output goes to stdout, one entry per line, deduplicated and sorted.
# Pipe through `sort -u` if you concatenate multiple runs.
#
# Reads files in place, never modifies anything.

import os
import re
import sys
from pathlib import Path

# Patterns Wasabi historically uses to register a callable script
# method on its ScriptObject's vtable. Capture (method_name, nparams)
# from each.
PATTERNS = [
    # mgr->addFunction(L"name", nparams, ...);
    re.compile(
        r'addFunction\s*\(\s*L"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*(-?\d+)',
    ),
    # exportFunction(L"name", nparams, ...)
    re.compile(
        r'exportFunction\s*\(\s*L"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*(-?\d+)',
    ),
    # exportedFunction(L"name", nparams, ...)
    re.compile(
        r'exportedFunction\s*\(\s*L"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*(-?\d+)',
    ),
    # registerFunction(L"name", nparams, ...)
    re.compile(
        r'registerFunction\s*\(\s*L"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*(-?\d+)',
    ),
    # registerSCM(NAME, name, type, NPARAMS) — older Wasabi
    re.compile(
        r'registerSCM\s*\([^,]+,\s*([A-Za-z_][A-Za-z0-9_]*)\s*,'
        r'\s*[A-Za-z_][A-Za-z0-9_]*\s*,\s*(-?\d+)',
    ),
    # SCM_FUNC(name, nparams) / SCM_FUNCTION(name, nparams)
    re.compile(
        r'SCM_FUNC(?:TION)?\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,\s*(-?\d+)',
    ),
]


def scan_file(path: Path, hits: dict[str, int]) -> int:
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return 0
    found = 0
    for pat in PATTERNS:
        for m in pat.finditer(text):
            name = m.group(1)
            try:
                nparams = int(m.group(2))
            except ValueError:
                continue
            # Last writer wins — first match per name typically suffices,
            # but if a method appears twice with different nparams we
            # want to know.
            if name in hits and hits[name] != nparams:
                print(
                    f"# WARN: {name} declared with nparams={hits[name]} "
                    f"and {nparams} in different places",
                    file=sys.stderr,
                )
            hits[name] = nparams
            found += 1
    return found


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.lstrip("# ").strip(), file=sys.stderr)
        return 2
    targets = []
    for a in argv[1:]:
        p = Path(a)
        if not p.exists():
            print(f"# WARN: not found: {p}", file=sys.stderr)
            continue
        if p.is_dir():
            targets.extend(sorted(p.rglob("*.cpp")))
        else:
            targets.append(p)
    if not targets:
        print("# no input files", file=sys.stderr)
        return 1

    hits: dict[str, int] = {}
    for t in targets:
        n = scan_file(t, hits)
        if n:
            print(f"# {t}: {n} match{'es' if n != 1 else ''}",
                  file=sys.stderr)

    if not hits:
        print(
            "# no matches — patterns may be wrong for this codebase, "
            "extend PATTERNS in this script",
            file=sys.stderr,
        )
        return 1

    print(f"# {len(hits)} unique method signatures extracted")
    print("// Paste under the matching ScriptObject section in")
    print("// wasabi-port/wasabi-port-link-stubs.cpp's kKnownMethods.")
    for name in sorted(hits):
        nparams = hits[name]
        # Pad name to 28 chars to match the column alignment in the
        # existing table.
        print(f'{{L"{name}",{" " * max(1, 28 - len(name))}{nparams}}},')
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
