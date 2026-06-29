// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#pragma once
//
// decode-service.h — AGAVE_API_DECODE minimal stub.
//
// The Wasabi AGAVE_API_DECODE service exposes input-plugin-driven
// metadata extraction (artist, album, duration, bitrate, cover art).
// We delegate to the host's existing audio decoder when one is set;
// otherwise queries return empty / zero.  ml_local uses this heavily
// for folder-scan indexing; the no-data default is enough for init
// to succeed.
//

#include "service-registry.h"

#include <QString>

namespace qtWasabi {
namespace wasabi_compat {
namespace svc {

class DecodeService : public ServiceObject {
public:
    GUID         guid()        const override { return DECODE_GUID; }
    const char  *typeName()    const override { return "decode"; }
    const char  *displayName() const override { return "qtWasabi Decode (passthrough)"; }

    // Passthrough stub: exposes the surface so caller code links,
    // but returns an empty string.  A host-backed metadata lookup
    // would replace this body.
    QString getMetadata(const QString & /*filePath*/,
                          const QString & /*field*/) {
        return QString();
    }

    static DecodeService &instance() {
        static DecodeService s;
        return s;
    }
};

}  // namespace svc
}  // namespace wasabi_compat
}  // namespace qtWasabi
