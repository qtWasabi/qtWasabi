#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Florian Kleber and qtWasabi contributors
#
# merge-method-sigs.py — merge new {L"name", nparams} entries into the
# kKnownMethods table in wasabi-port/wasabi-port-link-stubs.cpp.
#
# Pairs with extract-method-sigs.py. Typical workflow:
#
#   ./scripts/extract-method-sigs.py /path/to/wasabi-src/Src/Wasabi/api \
#       > /tmp/sigs.txt
#   ./scripts/merge-method-sigs.py /tmp/sigs.txt
#
# The merge is idempotent. Existing entries are preserved verbatim;
# conflicting nparams between an existing entry and a new one keep
# the existing value and emit a warning to stderr. Pure additions are
# appended to the table in alphabetical order, just before the
# sentinel.
#
# Pass --dry-run to print the merged table to stdout without touching
# the file.

import argparse
import re
import sys
from pathlib import Path

DEFAULT_TARGET = Path(__file__).resolve().parent.parent / \
    "wasabi-port" / "wasabi-port-link-stubs.cpp"

# Where the table lives in the file. We anchor on the kKnownMethods[]
# definition and the {nullptr, 0} sentinel that closes it.
TABLE_START_RE = re.compile(
    r"^(static\s+const\s+MethodSig\s+kKnownMethods\s*\[\]\s*=\s*\{)\s*$",
    re.MULTILINE,
)
SENTINEL_RE = re.compile(
    r"^\s*\{\s*nullptr\s*,\s*0\s*\}\s*,\s*$",
    re.MULTILINE,
)
ENTRY_RE = re.compile(
    r'\{\s*L"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*(-?\d+)\s*\}\s*,',
)


def parse_input(path: Path) -> dict[str, int]:
    """Parse new entries from extract-method-sigs.py output (or any
    file that has {L"name", nparams}, lines)."""
    out: dict[str, int] = {}
    text = path.read_text(encoding="utf-8")
    for m in ENTRY_RE.finditer(text):
        name, nparams = m.group(1), int(m.group(2))
        if name in out and out[name] != nparams:
            print(
                f"# WARN input has {name} with multiple nparams "
                f"({out[name]} vs {nparams}), keeping first",
                file=sys.stderr,
            )
            continue
        out[name] = nparams
    return out


def split_target(text: str) -> tuple[str, str, str]:
    """Return (head, body, tail) where body is the entries between the
    `{` opening kKnownMethods and the sentinel line."""
    m_start = TABLE_START_RE.search(text)
    if not m_start:
        sys.exit("error: kKnownMethods[] opening line not found in target")
    body_start = m_start.end()
    m_sentinel = SENTINEL_RE.search(text, body_start)
    if not m_sentinel:
        sys.exit("error: {nullptr, 0} sentinel not found in target")
    head = text[: body_start]
    body = text[body_start: m_sentinel.start()]
    tail = text[m_sentinel.start():]
    return head, body, tail


def merge(target: Path, new_sigs: dict[str, int],
          dry_run: bool) -> int:
    text = target.read_text(encoding="utf-8")
    head, body, tail = split_target(text)

    # Existing entries (preserve order + verbatim text by default).
    existing: dict[str, int] = {}
    for m in ENTRY_RE.finditer(body):
        name, nparams = m.group(1), int(m.group(2))
        existing[name] = nparams

    # Compute additions: new names not already in the table.
    additions: list[tuple[str, int]] = []
    skipped = 0
    conflicts: list[tuple[str, int, int]] = []
    for name, nparams in new_sigs.items():
        if name in existing:
            if existing[name] != nparams:
                conflicts.append((name, existing[name], nparams))
            else:
                skipped += 1
            continue
        additions.append((name, nparams))

    additions.sort()

    # Format additions as a fresh block appended just before the sentinel.
    if additions:
        max_name = max(len(n) for n, _ in additions)
        col = max(28, max_name + 2)
        added_block = ["\n    // M14k: signatures merged from upstream "
                       "ScriptObject getExportedFunctions tables.\n"]
        for name, nparams in additions:
            pad = " " * (col - len(name))
            added_block.append(f'    {{L"{name}",{pad}{nparams}}},\n')
        appended = "".join(added_block)
        # Body keeps existing text intact. Append the new block at the
        # end of body (right before the sentinel).
        new_body = body.rstrip("\n") + "\n" + appended
    else:
        new_body = body

    out = head + new_body + tail

    # Reporting.
    print(f"# existing entries: {len(existing)}", file=sys.stderr)
    print(f"# new entries to add: {len(additions)}", file=sys.stderr)
    print(f"# already-present (skipped): {skipped}", file=sys.stderr)
    if conflicts:
        print(f"# WARNING: {len(conflicts)} nparams conflicts kept existing:",
              file=sys.stderr)
        for name, kept, ignored in conflicts:
            print(f"#   {name}: existing={kept} ignored={ignored}",
                  file=sys.stderr)

    if dry_run:
        sys.stdout.write(out)
    else:
        backup = target.with_suffix(target.suffix + ".bak")
        backup.write_text(text, encoding="utf-8")
        target.write_text(out, encoding="utf-8")
        print(f"# wrote {target} (backup at {backup})", file=sys.stderr)
    return 0 if not conflicts else 0  # conflicts are warnings, not errors


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__.strip())
    p.add_argument(
        "input",
        type=Path,
        help="file with {L\"name\", nparams}, lines, e.g. the output of "
             "extract-method-sigs.py",
    )
    p.add_argument(
        "--target",
        type=Path,
        default=DEFAULT_TARGET,
        help=f"file to merge into (default: {DEFAULT_TARGET})",
    )
    p.add_argument(
        "--dry-run",
        action="store_true",
        help="print the merged file to stdout, don't write",
    )
    args = p.parse_args()

    if not args.input.is_file():
        sys.exit(f"error: input file not found: {args.input}")
    if not args.target.is_file():
        sys.exit(f"error: target file not found: {args.target}")

    sigs = parse_input(args.input)
    if not sigs:
        sys.exit(
            f"error: no {{L\"name\", N}}, entries parsed from {args.input}"
        )

    return merge(args.target, sigs, args.dry_run)


if __name__ == "__main__":
    sys.exit(main())
