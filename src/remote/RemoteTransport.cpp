#include <qtWasabi/remote/RemoteTransport.h>

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <qtWasabi/remote/SseReader.h>

#ifdef Q_OS_WASM
#include <emscripten.h>

#include <QHash>

#include <cstdlib>

// Browser EventSource glue.  Qt-WASM's QNetworkReply buffers a GET until
// it finishes, so the streaming-SSE path silently never delivers — the
// browser's native EventSource is the reliable primitive (and it also
// owns reconnection).  The glue is self-contained: strings cross the
// boundary through our own KEEPALIVE alloc/free wrappers and manual
// UTF-8 on HEAPU8, so no Emscripten runtime-method exports are needed
// (EXPORTED_RUNTIME_METHODS is last-wins on the link line and Qt sets
// its own list).
namespace qtWasabi::remote {
namespace {
QHash<int, RemoteTransport *> &wasmStreams() {
    static QHash<int, RemoteTransport *> reg;
    return reg;
}
}  // namespace
}  // namespace qtWasabi::remote

extern "C" {
EMSCRIPTEN_KEEPALIVE void *qtwasabi_es_alloc(int n) { return std::malloc(n); }
EMSCRIPTEN_KEEPALIVE void qtwasabi_es_free(void *p) { std::free(p); }
EMSCRIPTEN_KEEPALIVE void qtwasabi_es_event(int id, const char *event,
                                         const char *data) {
    if (auto *t = qtWasabi::remote::wasmStreams().value(id))
        t->wasmDeliverEvent(QByteArray(event), QByteArray(data));
}
EMSCRIPTEN_KEEPALIVE void qtwasabi_es_state(int id, int up) {
    if (auto *t = qtWasabi::remote::wasmStreams().value(id)) t->wasmDeliverState(up != 0);
}
}

// clang-format off
EM_JS(void, qtwasabi_es_open, (int id, const char *urlPtr), {
    let end = urlPtr;
    while (HEAPU8[end]) end++;
    const url = new TextDecoder().decode(HEAPU8.subarray(urlPtr, end));
    if (!Module.qtwasabiES) Module.qtwasabiES = {};
    if (Module.qtwasabiES[id]) Module.qtwasabiES[id].close();
    const es = new EventSource(url);
    Module.qtwasabiES[id] = es;
    const push = (name, payload) => {
        const enc = new TextEncoder();
        const ev = enc.encode(name);
        const data = enc.encode(payload || '');
        const ptr = _qtwasabi_es_alloc(ev.length + data.length + 2);
        HEAPU8.set(ev, ptr);
        HEAPU8[ptr + ev.length] = 0;
        const dPtr = ptr + ev.length + 1;
        HEAPU8.set(data, dPtr);
        HEAPU8[dPtr + data.length] = 0;
        _qtwasabi_es_event(id, ptr, dPtr);
        _qtwasabi_es_free(ptr);
    };
    // SSE named events bypass onmessage — subscribe to every event the
    // protocol defines (docs/PROTOCOL.md).
    for (const n of ['state', 'transport', 'track', 'playlist', 'eq',
                     'ping', 'next', 'complete'])
        es.addEventListener(n, (e) => push(n, e.data));
    es.onopen = () => _qtwasabi_es_state(id, 1);
    // EventSource reconnects on its own; just report the drop so the
    // host can resync when onopen fires again.
    es.onerror = () => _qtwasabi_es_state(id, 0);
});
EM_JS(void, qtwasabi_es_close, (int id), {
    if (Module.qtwasabiES && Module.qtwasabiES[id]) {
        Module.qtwasabiES[id].close();
        delete Module.qtwasabiES[id];
    }
});
// clang-format on

namespace qtWasabi::remote {
void wasmEsOpen(RemoteTransport *t, const QUrl &url) {
    if (!t->m_wasmStreamId) {
        static int nextId = 0;
        t->m_wasmStreamId = ++nextId;
        wasmStreams().insert(t->m_wasmStreamId, t);
    }
    qtwasabi_es_open(t->m_wasmStreamId, url.toString().toUtf8().constData());
}
void wasmEsClose(RemoteTransport *t) {
    if (t->m_wasmStreamId) qtwasabi_es_close(t->m_wasmStreamId);
}
}  // namespace qtWasabi::remote
#endif  // Q_OS_WASM

