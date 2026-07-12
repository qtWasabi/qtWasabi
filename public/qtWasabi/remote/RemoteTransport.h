// RemoteTransport — how RemoteHost reaches its backend, abstracted so
// unit tests can inject a scripted double and the WASM build can swap
// the event stream for a browser-native EventSource.
//
// The wire protocol is docs/PROTOCOL.md: JSON POSTs (commands), JSON
// GETs (state), raw GETs (album art) and one long-lived SSE stream.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QUrl>

#include <functional>

#include "SseReader.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace qtWasabi::remote {

class RemoteTransport : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    using JsonCallback = std::function<void(bool ok, QJsonObject reply)>;
    using BytesCallback = std::function<void(bool ok, QByteArray body)>;

    virtual void postJson(const QUrl &url, const QJsonObject &body,
                          JsonCallback cb) = 0;
    virtual void getJson(const QUrl &url, JsonCallback cb) = 0;
    virtual void getBytes(const QUrl &url, BytesCallback cb) = 0;
    // Open (or re-open) the long-lived SSE stream; events arrive on
    // eventReceived until closeEventStream() or a network drop, which
    // the implementation reports via streamStateChanged(false) and then
    // retries with backoff on its own.
    virtual void openEventStream(const QUrl &url) = 0;
    virtual void closeEventStream() = 0;

#ifdef Q_OS_WASM
    // Entry points for the browser EventSource glue (EM_JS blocks in
    // remotetransport.cpp).  Virtual so protocol transports can
    // intercept raw frames (GraphQL: `next` frames need translation).
    virtual void wasmDeliverEvent(const QByteArray &event,
                                  const QByteArray &data) {
        emit eventReceived(event, data);
    }
    void wasmDeliverState(bool up) { emit streamStateChanged(up); }
    int m_wasmStreamId = 0;
#endif

signals:
    void eventReceived(const QByteArray &event, const QByteArray &data);
    void streamStateChanged(bool up);
};

#ifdef Q_OS_WASM
// Open/close a browser-native EventSource for `t` (any RemoteTransport);
// frames arrive via t->wasmDeliverEvent / wasmDeliverState.
void wasmEsOpen(RemoteTransport *t, const QUrl &url);
void wasmEsClose(RemoteTransport *t);
#endif

// Test double: scripted replies, manual event injection, an op log.
class InjectedTransport : public RemoteTransport {
    Q_OBJECT
public:
    using RemoteTransport::RemoteTransport;

    // Reply served for the next getJson (the /state snapshot).
    QJsonObject stateReply;
    // Every postJson body lands here, in order.
    QList<QJsonObject> posted;
    // Reply for postJson calls (defaults to ok:true).
    QJsonObject postReply{{QStringLiteral("ok"), true}};

    void postJson(const QUrl &, const QJsonObject &body,
                  JsonCallback cb) override {
        posted.append(body);
        if (cb) cb(true, postReply);
    }
    void getJson(const QUrl &, JsonCallback cb) override {
        ++stateFetches;
        if (cb) cb(true, stateReply);
    }
    void getBytes(const QUrl &, BytesCallback cb) override {
        if (cb) cb(false, {});
    }
    void openEventStream(const QUrl &) override {
        streamOpens++;
        emit streamStateChanged(true);
    }
    void closeEventStream() override { emit streamStateChanged(false); }

    // Push an event as if it arrived on the stream.
    void inject(const QByteArray &event, const QByteArray &data) {
        emit eventReceived(event, data);
    }

    int stateFetches = 0;
    int streamOpens = 0;
};

}  // namespace qtWasabi::remote
