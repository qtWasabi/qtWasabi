#include <WasabiQt/Maki.h>

#include <QtEndian>
#include <QStringDecoder>
#include <QUuid>
#include <cstring>

namespace wasabiq::maki {

// ── Reader helpers ─────────────────────────────────────────────────
// All multi-byte values in .maki blobs are little-endian and packed
// without alignment padding.  We bounce through a dedicated reader so
// short reads land in errMsg instead of crashing.

namespace {
struct Reader {
    const QByteArray &buf;
    int pos = 0;

    bool need(int n) const { return pos + n <= buf.size(); }

    bool readU8(quint8 &out) {
        if (!need(1)) return false;
        out = static_cast<quint8>(buf[pos++]);
        return true;
    }
    bool readU16(quint16 &out) {
        if (!need(2)) return false;
        out = qFromLittleEndian<quint16>(buf.constData() + pos);
        pos += 2;
        return true;
    }
    bool readU32(quint32 &out) {
        if (!need(4)) return false;
        out = qFromLittleEndian<quint32>(buf.constData() + pos);
        pos += 4;
        return true;
    }
    bool readI32(qint32 &out) {
        quint32 u; if (!readU32(u)) return false;
        out = static_cast<qint32>(u); return true;
    }
    bool readBytes(int n, QByteArray &out) {
        if (!need(n)) return false;
        out = buf.mid(pos, n); pos += n; return true;
    }
    bool readString8(QString &out) {
        // 16-bit length prefix, raw UTF-8 bytes (matches scripts/foo.maki
        // function names — DLF table strings).
        quint16 len; if (!readU16(len)) return false;
        QByteArray b; if (!readBytes(len, b)) return false;
        out = QString::fromUtf8(b);
        return true;
    }
    bool readGuid(QUuid &out) {
        if (!need(16)) return false;
        // The .maki blob stores GUIDs as Microsoft's wire layout:
        // Data1 (LE u32), Data2 (LE u16), Data3 (LE u16), Data4 (8 BE bytes).
        quint32 d1; quint16 d2, d3;
        readU32(d1); readU16(d2); readU16(d3);
        QByteArray d4; readBytes(8, d4);
        out = QUuid(d1, d2, d3,
                    static_cast<quint8>(d4[0]), static_cast<quint8>(d4[1]),
                    static_cast<quint8>(d4[2]), static_cast<quint8>(d4[3]),
                    static_cast<quint8>(d4[4]), static_cast<quint8>(d4[5]),
                    static_cast<quint8>(d4[6]), static_cast<quint8>(d4[7]));
        return true;
    }
};
} // namespace

bool loadScript(const QByteArray &blob, Script &out, QString *errMsg)
{
    auto fail = [&](const QString &msg) {
        if (errMsg) *errMsg = msg;
        return false;
    };

    if (blob.size() < 8) return fail("blob too small for header");

    // Header — "FG\x03\x04" magic + 1 version byte + 3 zero bytes.
    if (blob[0] != 'F' || blob[1] != 'G' || blob[2] != 0x03 || blob[3] != 0x04)
        return fail("not a maki file (missing FG\\x03\\x04 magic)");

    quint8 verByte = static_cast<quint8>(blob[4]);
    HeaderVersion version;
    bool translateObjects = false;
    switch (verByte) {
        case 0x14: version = HeaderVersion::Unknown;  // deprecated v1 — reject
                   return fail("deprecated maki binary (v1)");
        case 0x15: version = HeaderVersion::V3; translateObjects = true; break;
        case 0x16: version = HeaderVersion::V3; break;
        case 0x17: version = HeaderVersion::V4; break;
        default:
            if (verByte > 0x17) return fail("future maki version — please upgrade");
            return fail("unrecognised maki version byte");
    }
    out.version = version;
    Q_UNUSED(translateObjects);

    Reader r{blob, 8};

    // ── Type table (skipped in v15/translateObjects mode) ─────────
    if (!translateObjects) {
        quint32 nGuids;
        if (!r.readU32(nGuids)) return fail("type table count truncated");
        out.typeTable.reserve(static_cast<int>(nGuids));
        for (quint32 i = 0; i < nGuids; ++i) {
            QUuid g;
            if (!r.readGuid(g)) return fail("type table entry truncated");
            out.typeTable.append(g);
        }
    }

    // ── DLF table ─────────────────────────────────────────────────
    quint32 nDlf;
    if (!r.readU32(nDlf)) return fail("DLF count truncated");
    out.dlfTable.reserve(static_cast<int>(nDlf));
    for (quint32 i = 0; i < nDlf; ++i) {
        DLFEntry e;
        qint32 basetype;
        if (!r.readI32(basetype)) return fail("DLF basetype truncated");
        e.rawBasetype = basetype;
        e.basetype = basetype;
        if (!r.readString8(e.functionName))
            return fail("DLF function name truncated");
        out.dlfTable.append(e);
    }

    // ── Variable table ────────────────────────────────────────────
    quint32 nVars;
    if (!r.readU32(nVars)) return fail("variable count truncated");
    out.variables.reserve(static_cast<int>(nVars));
    // Maki primitive types — match vcputypes.h SCRIPT_* constants.
    constexpr int kScriptVoid    = 0;
    constexpr int kScriptInt     = 2;
    constexpr int kScriptFloat   = 3;
    constexpr int kScriptDouble  = 4;
    constexpr int kScriptBoolean = 5;
    constexpr int kScriptString  = 6;
    for (quint32 i = 0; i < nVars; ++i) {
        // scriptVar on disk is `pragma pack(1)`: 4-byte type + 8-byte
        // data union (sized for the `double ddata` member).  After
        // the union come 'transcient' u8 and (V4 only) 'isstatic' u8.
        //
        // The 8-byte data union contains the variable's INITIAL
        // VALUE.  For Int / Float / Double constants this is what
        // versionCheck() etc. compares against — discarding it makes
        // every numeric constant zero, which breaks branches.
        quint32 vtype;
        if (!r.readU32(vtype)) return fail("var type truncated");
        QByteArray dataSlot;
        if (!r.readBytes(8, dataSlot)) return fail("var data truncated");
        quint8 transcient = 0, isStatic = 0;
        if (!r.readU8(transcient)) return fail("var transcient truncated");
        if (version == HeaderVersion::V4) {
            if (!r.readU8(isStatic)) return fail("var isstatic truncated");
        }
        ScriptVar v;
        v.type       = static_cast<int>(vtype);
        v.transcient = (transcient == 0);   // upstream inverts this
        v.isStatic   = (isStatic != 0);

        // Decode the initial value from the 8-byte data slot.  In
        // pragma-pack(1) the union is laid out so the primitive's
        // bytes are in the LOW end (little-endian on x86, which is
        // what Maki targets).
        const char *db = dataSlot.constData();
        switch (vtype) {
            case kScriptInt:
            case kScriptBoolean: {
                qint32 iv = qFromLittleEndian<qint32>(db);
                v.value = QVariant::fromValue<qint64>(iv);
                break;
            }
            case kScriptFloat: {
                quint32 raw = qFromLittleEndian<quint32>(db);
                float fv;
                std::memcpy(&fv, &raw, sizeof(fv));
                v.value = double(fv);
                break;
            }
            case kScriptDouble: {
                // Upstream loader does `e.data.ddata = e.data.fdata`
                // — i.e. the binary stores a 4-byte FLOAT in the low
                // part of the union, and the runtime promotes it to
                // double.  The upper 4 bytes of the slot are unused.
                quint32 raw = qFromLittleEndian<quint32>(db);
                float fv;
                std::memcpy(&fv, &raw, sizeof(fv));
                v.value = double(fv);
                break;
            }
            default:
                // Object / string / void: data slot is either a
                // null pointer or to-be-resolved.  Strings get
                // populated from the string table below.
                break;
        }
        out.variables.append(v);
    }

    // ── String table ──────────────────────────────────────────────
    // The string table assigns an initial value to a *string-typed*
    // variable identified by attach_id.  On-disk layout per entry:
    //   u32 attach_id, u16 stringLen, stringLen bytes UTF-8.
    quint32 nStrings;
    if (!r.readU32(nStrings)) return fail("string count truncated");
    for (quint32 i = 0; i < nStrings; ++i) {
        qint32 attachId;
        if (!r.readI32(attachId)) return fail("string attach id truncated");
        QString s;
        if (!r.readString8(s)) return fail("string table entry truncated");
        if (attachId >= 0 && attachId < out.variables.size())
            out.variables[attachId].value = s;
    }

    // ── Event table ───────────────────────────────────────────────
    quint32 nEvents;
    if (!r.readU32(nEvents)) return fail("event count truncated");
    out.events.reserve(static_cast<int>(nEvents));
    for (quint32 i = 0; i < nEvents; ++i) {
        EventHook h;
        qint32 v, d, off;
        if (!r.readI32(v)) return fail("event variable truncated");
        if (!r.readI32(d)) return fail("event DLF truncated");
        if (!r.readI32(off)) return fail("event offset truncated");
        h.variableId = v;
        h.dlfId      = d;
        h.codeOffset = off;
        out.events.append(h);
    }

    // ── Code segment ──────────────────────────────────────────────
    quint32 codeSize;
    if (!r.readU32(codeSize)) return fail("code size truncated");
    if (!r.readBytes(static_cast<int>(codeSize), out.code))
        return fail("code segment truncated");

    return true;
}

} // namespace wasabiq::maki
