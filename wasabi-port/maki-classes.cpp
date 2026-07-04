// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber

#include "maki-classes.h"

namespace qtWasabi::Maki {

// Flat binding registry (maki-bindings.cpp) — the migration fallback.
void *makiFlatLookup(const wchar_t *name, int *nparams);

namespace {

int ciCmp(const wchar_t *a, const wchar_t *b) {
    while (*a && *b) {
        wchar_t la = *a, lb = *b;
        if (la >= L'A' && la <= L'Z') la = wchar_t(la - L'A' + L'a');
        if (lb >= L'A' && lb <= L'Z') lb = wchar_t(lb - L'A' + L'a');
        if (la != lb) return int(la) - int(lb);
        ++a; ++b;
    }
    return int(*a) - int(*b);
}

}  // namespace

// Per-class split bodies (maki-bindings.cpp).  Declared opaquely —
// the table stores addresses only; callDLF casts to the real Maki
// method ABI at the call site, exactly as it does for the flat rows.
extern "C" {
void wq_sliderGetPosition();
void wq_sliderSetPosition();
void wq_frameGetPosition();
void wq_frameSetPosition();
}

#define FN(f) reinterpret_cast<void *>(&f)

namespace {

// ── Per-class method tables ─────────────────────────────────────────
// Only rows whose SCOPING changes behaviour today are listed: the
// cross-class collisions (Slider/Frame position, the Timer surface)
// and the classes whose flat rows are unambiguous but class-owned
// (Application, Config family, Text).  Every unlisted name falls back
// to the flat table with identical behaviour; the fallback is traced
// (WASABIQT_TRACE_SCOPED_MISS) so migration proceeds evidence-first.

const ClassMethod kTimer[] = {
    { L"setDelay",   1, nullptr },   // flat: wq_setDelay
    { L"getDelay",   0, nullptr },   // unbound; correct arity
    { L"start",      0, nullptr },   // flat: wq_timerStart
    { L"stop",       0, nullptr },   // flat: wq_stop (timer semantics)
    { L"isRunning",  0, nullptr },
    { L"isStarted",  0, nullptr },
    { L"getSkipped", 0, nullptr },
    { L"onTimer",    0, nullptr },
};

const ClassMethod kSlider[] = {
    { L"getPosition",   0, FN(wq_sliderGetPosition) },
    { L"setPosition",   1, FN(wq_sliderSetPosition) },
    { L"onSetPosition", 1, nullptr },
    { L"onPostedPosition", 1, nullptr },
    { L"lock",          0, nullptr },
    { L"unlock",        0, nullptr },
};

const ClassMethod kFrame[] = {
    { L"getPosition", 0, FN(wq_frameGetPosition) },
    { L"setPosition", 1, FN(wq_frameSetPosition) },
};

const ClassMethod kApplication[] = {
    { L"GetApplicationName",      0, nullptr },
    { L"GetVersionString",        0, nullptr },
    { L"GetVersionNumberString",  0, nullptr },
    { L"GetBuildNumber",          0, nullptr },
};

const ClassMethod kText[] = {
    { L"setText",       1, nullptr },
    { L"getText",       0, nullptr },
    { L"getTextWidth",  0, nullptr },
    { L"onTextChanged", 1, nullptr },
};

const ClassMethod kGroup[] = {
    { L"getObject",     1, nullptr },
    { L"getNumObjects", 0, nullptr },
    { L"enumObject",    1, nullptr },
};

const ClassMethod kLayout[] = {
    { L"getContainer",   0, nullptr },
    { L"getScale",       0, nullptr },
    { L"setScale",       1, nullptr },
    { L"onTargetReached", 0, nullptr },
};

const ClassMethod kContainer[] = {
    { L"getLayout",     1, nullptr },
    { L"getNumLayouts", 0, nullptr },
    { L"enumLayout",    1, nullptr },
};

const ClassMethod kButton[] = {
    { L"leftClick",   0, nullptr },
    { L"rightClick",  0, nullptr },
    { L"onActivate",  1, nullptr },
    { L"onLeftClick", 0, nullptr },
    { L"onRightClick", 0, nullptr },
};

const ClassMethod kToggleButton[] = {
    { L"setActivated",         1, nullptr },
    { L"getActivated",         0, nullptr },
    { L"setActivatedNoCallback", 1, nullptr },
    { L"onToggle",             1, nullptr },
};

const ClassMethod kConfig[] = {
    { L"newItem",       2, nullptr },
    { L"getItem",       1, nullptr },
    { L"getItemByGuid", 1, nullptr },
};

const ClassMethod kConfigItem[] = {
    { L"newAttribute", 2, nullptr },
    { L"getAttribute", 1, nullptr },
    { L"getGuid",      0, nullptr },
};

const ClassMethod kConfigAttribute[] = {
    { L"setData",       1, nullptr },
    { L"getData",       0, nullptr },
    { L"onDataChanged", 0, nullptr },
    { L"getAttributeName", 0, nullptr },
    { L"getParentItem", 0, nullptr },
};

const ClassMethod kWinampConfig[] = {
    { L"getGroup", 1, nullptr },
};

const ClassMethod kWinampConfigGroup[] = {
    { L"getInt",    1, nullptr },
    { L"getBool",   1, nullptr },
    { L"getString", 1, nullptr },
};

// ── Class registry ──────────────────────────────────────────────────
// Parents-first; index + CLASS_ID_BASE = the global classid handed to
// the interpreter.  The GUIDs are the interop identifiers compiled
// .maki binaries embed for these classes.
#define G(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { a, b, c, { d0, d1, d2, d3, d4, d5, d6, d7 } }
#define M(t) t, int(sizeof(t) / sizeof(t[0]))

const MakiClass kClasses[] = {
/* 0*/ { L"Object",         G(0x51654971,0x0d87,0x4a51,0x91,0xe3,0xa6,0xb5,0x32,0x35,0xf3,0xe7), -1, nullptr, 0 },
/* 1*/ { L"SystemObject",   G(0xd6f50f64,0x93fa,0x49b7,0x93,0xf1,0xba,0x66,0xef,0xae,0x3e,0x98),  0, nullptr, 0 },
/* 2*/ { L"GuiObject",      G(0x4ee3e199,0xc636,0x4bec,0x97,0xcd,0x78,0xbc,0x9c,0x86,0x28,0xb0),  0, nullptr, 0 },
/* 3*/ { L"Group",          G(0x45be95e5,0x2072,0x4191,0x93,0x5c,0xbb,0x5f,0xf9,0xf1,0x17,0xfd),  2, M(kGroup) },
/* 4*/ { L"Layout",         G(0x60906d4e,0x537e,0x482e,0xb0,0x04,0xcc,0x94,0x61,0x88,0x56,0x72),  3, M(kLayout) },
/* 5*/ { L"Container",      G(0xe90dc47b,0x840d,0x4ae7,0xb0,0x2c,0x04,0x0b,0xd2,0x75,0xf7,0xfc),  0, M(kContainer) },
/* 6*/ { L"Button",         G(0x698eddcd,0x8f1e,0x4fec,0x9b,0x12,0xf9,0x44,0xf9,0x09,0xff,0x45),  2, M(kButton) },
/* 7*/ { L"ToggleButton",   G(0xb4dccfff,0x81fe,0x4bcc,0x96,0x1b,0x72,0x0f,0xd5,0xbe,0x0f,0xff),  6, M(kToggleButton) },
/* 8*/ { L"Text",           G(0xefaa8672,0x310e,0x41fa,0xb7,0xdc,0x85,0xa9,0x52,0x5b,0xcb,0x4b),  2, M(kText) },
/* 9*/ { L"Edit",           G(0x64e4bbfa,0x81f4,0x49d9,0xb0,0xc0,0xa8,0x5b,0x2e,0xc3,0xbc,0xfd),  2, M(kText) },
/*10*/ { L"Slider",         G(0x62b65e3f,0x375e,0x408d,0x8d,0xea,0x76,0x81,0x4a,0xb9,0x1b,0x77),  2, M(kSlider) },
/*11*/ { L"Layer",          G(0x5ab9fa15,0x9a7d,0x4557,0xab,0xc8,0x65,0x57,0xa6,0xc6,0x7c,0xa9),  2, nullptr, 0 },
/*12*/ { L"AnimatedLayer",  G(0x6b64cd27,0x5a26,0x4c4b,0x8c,0x59,0xe6,0xa7,0x0c,0xf6,0x49,0x3a), 11, nullptr, 0 },
/*13*/ { L"Timer",          G(0x5d0c5bb6,0x7de1,0x4b1f,0xa7,0x0f,0x8d,0x16,0x59,0x94,0x19,0x41),  0, M(kTimer) },
/*14*/ { L"PopupMenu",      G(0xf4787af4,0xb2bb,0x4ef7,0x9c,0xfb,0xe7,0x4b,0xa9,0xbe,0xa8,0x8d),  0, nullptr, 0 },
/*15*/ { L"Region",         G(0x3a370c02,0x3cbf,0x439f,0x84,0xf1,0x86,0x88,0x5b,0xcf,0x1e,0x36),  0, nullptr, 0 },
/*16*/ { L"Map",            G(0x38603665,0x461b,0x42a7,0xaa,0x75,0xd8,0x3f,0x66,0x67,0xbf,0x73),  0, nullptr, 0 },
/*17*/ { L"List",           G(0xb2023ab5,0x434d,0x4ba1,0xbe,0xae,0x59,0x63,0x75,0x03,0xf3,0xc6),  0, nullptr, 0 },
/*18*/ { L"BitList",        G(0x87c65778,0xe743,0x49fe,0x85,0xf9,0x09,0xcc,0x53,0x2a,0xfd,0x56),  0, nullptr, 0 },
/*19*/ { L"Wac",            G(0x00c074a0,0xfea2,0x49a0,0xbe,0x8d,0xfa,0xbb,0xdb,0x16,0x16,0x40),  0, nullptr, 0 },
/*20*/ { L"Application",    G(0xb8e867b0,0x2715,0x4da7,0xa5,0xba,0x53,0xdb,0xa1,0xfc,0xfe,0xac),  0, M(kApplication) },
/*21*/ { L"Color",          G(0x95ddb221,0x00e3,0x4e2b,0x8e,0xa5,0x83,0x35,0x48,0xc1,0x3c,0x10),  0, nullptr, 0 },
/*22*/ { L"ColorMgr",       G(0xaee235ff,0xebd1,0x498f,0x96,0xaf,0xd7,0xe0,0xda,0xd4,0x54,0x1a),  0, nullptr, 0 },
/*23*/ { L"GammaSet",       G(0x0d024db9,0x9574,0x42d0,0xb8,0xc7,0x26,0xb5,0x53,0xf1,0xf9,0x87),  0, nullptr, 0 },
/*24*/ { L"GammaGroup",     G(0xb81f004d,0xacba,0x453d,0xa0,0x6b,0x30,0x19,0x2a,0x1d,0xa1,0x7d),  0, nullptr, 0 },
/*25*/ { L"File",           G(0x836f8b2e,0xe0d1,0x4db4,0x93,0x7f,0x0d,0x0a,0x04,0xc8,0xdc,0xd1),  0, nullptr, 0 },
/*26*/ { L"XmlDoc",         G(0x417ffb69,0x987f,0x4be8,0x8d,0x87,0xd9,0x96,0x5e,0xee,0xc8,0x68), 25, nullptr, 0 },
/*27*/ { L"Private",        G(0x78bd6ed9,0x0dbc,0x4fa5,0xb5,0xcd,0x59,0x77,0xe3,0xa9,0x12,0xf8),  0, nullptr, 0 },
/*28*/ { L"Config",         G(0x593dba22,0xd077,0x4976,0xb9,0x52,0xf4,0x71,0x36,0x55,0x40,0x0b),  0, M(kConfig) },
/*29*/ { L"ConfigItem",     G(0xd4030282,0x3aab,0x4d87,0x87,0x8d,0x12,0x32,0x6f,0xad,0xfc,0xd5),  0, M(kConfigItem) },
/*30*/ { L"ConfigAttribute",G(0x24dec283,0xb76e,0x4a36,0x8c,0xcc,0x9e,0x24,0xc4,0x6b,0x6c,0x73),  0, M(kConfigAttribute) },
/*31*/ { L"WinampConfig",   G(0xb2ad3f2b,0x31ed,0x4e31,0xbc,0x6d,0xe9,0x95,0x1c,0xd5,0x55,0xbb),  0, M(kWinampConfig) },
/*32*/ { L"WinampConfigGroup", G(0xfc17844e,0xc72b,0x4518,0xa0,0x68,0xa8,0xf9,0x30,0xa5,0xba,0x80), 0, M(kWinampConfigGroup) },
/*33*/ { L"PlEdit",         G(0x345beebc,0x0229,0x4921,0x90,0xbe,0x6c,0xb6,0xa4,0x9a,0x79,0xd9),  0, nullptr, 0 },
/*34*/ { L"Browser",        G(0xa8c2200d,0x51eb,0x4b2a,0xba,0x7f,0x5d,0x4b,0xc6,0x5d,0x4c,0x71),  2, nullptr, 0 },
/*35*/ { L"WindowHolder",   G(0x403abcc0,0x6f22,0x4bd6,0x8b,0xa4,0x10,0xc8,0x29,0x93,0x25,0x47),  2, nullptr, 0 },
/*36*/ { L"ObjectEmbedder", G(0x1819d795,0x7a6f,0x4f2a,0x8a,0x4d,0x7d,0xb3,0xee,0xa9,0x09,0x11),  2, nullptr, 0 },
/*37*/ { L"DropDownList",   G(0x36d59b71,0x03fd,0x4af8,0x97,0x95,0x05,0x02,0xb7,0xdb,0x26,0x7a), 36, nullptr, 0 },
/*38*/ { L"CheckBox",       G(0xc7ed3199,0x5319,0x4798,0x98,0x63,0x60,0xb1,0x5a,0x29,0x8c,0xaa),  2, M(kText) },
/*39*/ { L"Frame",          G(0xe2bbc14d,0x84f6,0x4173,0xbd,0xb3,0xb2,0xeb,0x2f,0x66,0x55,0x50),  2, M(kFrame) },
/*40*/ { L"TabSheet",       G(0xa8f61649,0xaa6c,0x46d1,0x94,0x15,0x0a,0xe4,0x91,0x99,0x9e,0x25),  2, nullptr, 0 },
/*41*/ { L"GuiList",        G(0x6129fec1,0xdab7,0x4d51,0x91,0x65,0x01,0xca,0x0c,0x1b,0x70,0xdb),  2, nullptr, 0 },
/*42*/ { L"GuiTree",        G(0xd59514f7,0xed36,0x45e8,0x98,0x0f,0x3f,0x4e,0xa0,0x52,0x2c,0xd9),  2, nullptr, 0 },
/*43*/ { L"TreeItem",       G(0x9b3b4b82,0x667a,0x420e,0x8f,0xfc,0x79,0x41,0x15,0x80,0x9c,0x02),  0, nullptr, 0 },
/*44*/ { L"MenuButton",     G(0x1d8631c8,0x80d0,0x4792,0x9f,0x98,0xbd,0x5d,0x36,0xb4,0x91,0x36),  6, nullptr, 0 },
/*45*/ { L"Vis",            G(0xce4f97be,0x77b0,0x4e19,0x99,0x56,0xd4,0x98,0x33,0xc9,0x6c,0x27),  2, nullptr, 0 },
/*46*/ { L"Status",         G(0x0f08c940,0xaf39,0x4b23,0x80,0xf3,0xb8,0xc4,0x8f,0x7e,0xbb,0x59),  2, nullptr, 0 },
/*47*/ { L"Title",          G(0x7dfd3244,0x3751,0x4e7c,0xbf,0x40,0x82,0xae,0x5f,0x3a,0xdc,0x33),  2, nullptr, 0 },
/*48*/ { L"ComponentBucket",G(0x97aa3e4d,0xf4d0,0x4fa8,0x81,0x7b,0x0a,0xf2,0x2a,0x45,0x49,0x83),  2, nullptr, 0 },
/*49*/ { L"MouseRedir",     G(0x9b2e341b,0x6c98,0x40fa,0x8b,0x85,0x0c,0x1b,0x6e,0xe8,0x94,0x05),  2, nullptr, 0 },
/*50*/ { L"GroupList",      G(0x01e28ce1,0xb059,0x11d5,0x97,0x9f,0xe4,0xde,0x6f,0x51,0x76,0x0a),  2, nullptr, 0 },
/*51*/ { L"QueryList",      G(0xcdcb785d,0x81f2,0x4253,0x8f,0x05,0x61,0xb8,0x72,0x28,0x3c,0xfa),  2, nullptr, 0 },
/*52*/ { L"LayoutStatus",   G(0x7fd5f210,0xacc4,0x48df,0xa6,0xa0,0x54,0x51,0x57,0x6c,0xdc,0x76),  2, nullptr, 0 },
/*53*/ { L"GuiList2",       G(0x0b099223,0x4eb3,0x4780,0x99,0x37,0x6d,0x21,0x03,0x72,0xf2,0xcf),  2, nullptr, 0 },
};

#undef M
#undef G

constexpr int kClassCount = int(sizeof(kClasses) / sizeof(kClasses[0]));

}  // namespace

const MakiClass *makiClassTable(int *count) {
    if (count) *count = kClassCount;
    return kClasses;
}

int makiClassIndexFromGuid(const GUID &g) {
    for (int i = 0; i < kClassCount; ++i) {
        const GUID &c = kClasses[i].guid;
        if (c.Data1 == g.Data1 && c.Data2 == g.Data2 && c.Data3 == g.Data3 &&
            c.Data4[0] == g.Data4[0] && c.Data4[1] == g.Data4[1] &&
            c.Data4[2] == g.Data4[2] && c.Data4[3] == g.Data4[3] &&
            c.Data4[4] == g.Data4[4] && c.Data4[5] == g.Data4[5] &&
            c.Data4[6] == g.Data4[6] && c.Data4[7] == g.Data4[7])
            return i;
    }
    return -1;
}

int makiClassIndexFromName(const wchar_t *name) {
    if (!name) return -1;
    for (int i = 0; i < kClassCount; ++i)
        if (ciCmp(kClasses[i].name, name) == 0) return i;
    // std.mi-facing aliases: the engine class names differ from the
    // script-facing ones for two classes.
    if (ciCmp(name, L"System") == 0)
        return makiClassIndexFromName(L"SystemObject");
    if (ciCmp(name, L"Popup") == 0)
        return makiClassIndexFromName(L"PopupMenu");
    return -1;
}

bool makiResolveScoped(int classIdx, const wchar_t *name,
                       int *nparams, void **ptr) {
    if (!name) return false;
    for (int idx = classIdx; idx >= 0 && idx < kClassCount;
         idx = kClasses[idx].parent) {
        const MakiClass &c = kClasses[idx];
        for (int m = 0; m < c.methodCount; ++m) {
            if (ciCmp(c.methods[m].name, name) != 0) continue;
            // Migration contract: explicit row pointer wins (the
            // per-class splits); else the flat table supplies pointer
            // AND arity (behaviour-identical); else honestly unbound
            // with this row's arity so the operand stack stays
            // aligned.
            if (c.methods[m].ptr) {
                *ptr     = c.methods[m].ptr;
                *nparams = c.methods[m].nparams;
                return true;
            }
            int fnp = 0;
            if (void *fp = makiFlatLookup(name, &fnp)) {
                *ptr     = fp;
                *nparams = fnp;
                return true;
            }
            *ptr     = nullptr;
            *nparams = c.methods[m].nparams;
            return true;
        }
    }
    return false;
}

}  // namespace qtWasabi::Maki
