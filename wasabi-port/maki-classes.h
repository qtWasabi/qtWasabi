// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// The Maki class registry — the typed layer around the vendored
// interpreter.  Compiled .maki binaries declare every class they use
// by GUID and every imported method by (class, name); real Wasabi
// resolves methods by walking the declared class's controller chain.
// This registry restores that model: classes with their GUIDs and
// ancestor links, plus per-class method tables scoped exactly like
// ObjectTable::addrefDLF's ancestor walk.
//
// The GUID constants are interop identifiers from the published
// Wasabi API surface (the same values every compiled skin embeds);
// they carry no expressive content.
//
// A method row's `ptr` may be null: the name then resolves through
// the flat binding table (behaviour-identical migration path), and a
// miss there leaves the method honestly unbound with THIS row's arity
// (so the operand stack stays aligned even for methods we have not
// implemented yet).

#pragma once

#include <bfc/nsguid.h>

namespace qtWasabi::Maki {

struct ClassMethod {
    const wchar_t *name;
    int            nparams;
    void          *ptr;      // null ⇒ resolve via the flat table
};

struct MakiClass {
    const wchar_t     *name;
    GUID               guid;
    int                parent;       // index into the class table, -1 = root
    const ClassMethod *methods;      // may be null (no scoped rows yet)
    int                methodCount;
};

// The registry, parents-first.  Index + CLASS_ID_BASE = global classid.
const MakiClass *makiClassTable(int *count);

int makiClassIndexFromGuid(const GUID &g);       // -1 = unknown
int makiClassIndexFromName(const wchar_t *name); // case-insensitive, -1

// Scoped resolution: walk classIdx → ancestors for a case-insensitive
// name match.  On a hit, fills nparams/ptr per the migration contract
// (explicit row ptr > flat-table ptr > unbound with row arity) and
// returns true.  Miss (name not on the class chain) returns false —
// the caller decides whether to fall back to the flat table.
bool makiResolveScoped(int classIdx, const wchar_t *name,
                       int *nparams, void **ptr);

}  // namespace qtWasabi::Maki
