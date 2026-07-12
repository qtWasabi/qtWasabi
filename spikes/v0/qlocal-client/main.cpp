// V0 spike (b): a QLocalSocket HTTP/1.1 client against the pylon on a
// unix socket — one unary POST /graphql and one UNBOUNDED SSE
// subscription stream.  Node emits streams with Transfer-Encoding:
// chunked (verified), so this client carries a minimal incremental
// chunked decoder; unary responses use Content-Length.  This is the
// blueprint for LocalSocketGraphQLTransport.
#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QLocalSocket>

#include <cstdio>

namespace {

struct HttpResponse {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;          // unary only
    bool headersDone = false;
    bool chunked = false;
    qint64 contentLength = -1;
};

// Incremental chunked-transfer decoder: feed raw bytes, emits payload
// bytes.  Enough for our own servers (no trailers, no extensions).
class ChunkedDecoder {
public:
    // Returns decoded payload bytes extracted from `raw`.
    QByteArray feed(const QByteArray &raw) {
        m_buf += raw;
        QByteArray out;
        for (;;) {
            if (m_remaining == 0) {
                const int idx = m_buf.indexOf("\r\n");
                if (idx < 0) return out;                  // need more
                bool ok = false;
                const qint64 size =
                    m_buf.left(idx).toLongLong(&ok, 16);
                if (!ok) { m_error = true; return out; }
                m_buf.remove(0, idx + 2);
                if (size == 0) { m_done = true; return out; }
                m_remaining = size;
            }
            const qint64 take =
                qMin<qint64>(m_remaining, m_buf.size());
            out += m_buf.left(take);
            m_buf.remove(0, take);
            m_remaining -= take;
            if (m_remaining == 0) {
                if (m_buf.size() < 2) return out;         // await CRLF
                m_buf.remove(0, 2);
            } else {
                return out;                               // need more
            }
        }
    }
    bool done() const { return m_done; }
    bool error() const { return m_error; }

private:
    QByteArray m_buf;
    qint64 m_remaining = 0;
    bool m_done = false;
    bool m_error = false;
};

QByteArray buildRequest(const QByteArray &body, bool sse) {
    QByteArray req;
    req += "POST /graphql HTTP/1.1\r\n";
    req += "Host: qtwasabi.local\r\n";
    req += "Content-Type: application/json\r\n";
    if (sse) req += "Accept: text/event-stream\r\n";
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    req += "Connection: close\r\n\r\n";
    req += body;
    return req;
}

bool parseHeaders(QByteArray &buf, HttpResponse &resp) {
    const int idx = buf.indexOf("\r\n\r\n");
    if (idx < 0) return false;
    const QList<QByteArray> lines = buf.left(idx).split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> status = lines[0].simplified().split(' ');
        if (status.size() >= 2) resp.status = status[1].toInt();
    }
    for (int i = 1; i < lines.size(); ++i) {
        const int c = lines[i].indexOf(':');
        if (c < 0) continue;
        resp.headers.insert(lines[i].left(c).trimmed().toLower(),
                            lines[i].mid(c + 1).trimmed());
    }
    resp.chunked = resp.headers.value("transfer-encoding")
                       .toLower().contains("chunked");
    resp.contentLength =
        resp.headers.contains("content-length")
            ? resp.headers.value("content-length").toLongLong()
            : -1;
    resp.headersDone = true;
    buf.remove(0, idx + 4);
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QString sockPath = argc > 1
        ? QString::fromLocal8Bit(argv[1])
        : qEnvironmentVariable("PYLON_SOCKET");
    if (sockPath.isEmpty()) {
        fprintf(stderr, "usage: qlocal-spike <socket path>\n");
        return 2;
    }

    int failures = 0;
    const auto check = [&failures](bool ok, const char *label) {
        printf("  %-4s %s\n", ok ? "ok" : "FAIL", label);
        if (!ok) ++failures;
    };

    // ── 1) unary query ─────────────────────────────────────────────
    {
        QLocalSocket sock;
        sock.connectToServer(sockPath);
        if (!sock.waitForConnected(3000)) {
            fprintf(stderr, "connect failed: %s\n",
                    sock.errorString().toLocal8Bit().constData());
            return 1;
        }
        sock.write(buildRequest(
            R"({"query":"{player{kind revision transport{volume}}}"})",
            false));
        HttpResponse resp;
        QByteArray buf;
        while (sock.waitForReadyRead(3000)) {
            buf += sock.readAll();
            if (!resp.headersDone && !parseHeaders(buf, resp)) continue;
            if (resp.chunked) {
                static ChunkedDecoder dec;
                resp.body += dec.feed(buf);
                buf.clear();
                if (dec.done()) break;
            } else {
                resp.body += buf;
                buf.clear();
                if (resp.contentLength >= 0 &&
                    resp.body.size() >= resp.contentLength)
                    break;
            }
            if (sock.state() != QLocalSocket::ConnectedState) break;
        }
        check(resp.status == 200, "unary: HTTP 200");
        check(resp.body.contains("\"volume\":70"),
              "unary: body carries player.transport.volume");
        printf("       body: %s\n", resp.body.constData());
    }

    // ── 2) unbounded SSE subscription (chunked) ────────────────────
    {
        QLocalSocket sock;
        sock.connectToServer(sockPath);
        if (!sock.waitForConnected(3000)) return 1;
        sock.write(buildRequest(
            R"({"query":"subscription { playerEvents { kind revision } }"})",
            true));
        HttpResponse resp;
        ChunkedDecoder dec;
        QByteArray buf, payload;
        int events = 0;
        const qint64 deadline =
            QDateTime::currentMSecsSinceEpoch() + 5000;
        while (QDateTime::currentMSecsSinceEpoch() < deadline &&
               events < 3) {
            if (!sock.waitForReadyRead(1000)) continue;
            buf += sock.readAll();
            if (!resp.headersDone && !parseHeaders(buf, resp)) continue;
            payload += resp.chunked ? dec.feed(buf) : buf;
            buf.clear();
            // Count SSE `event: next` frames as they stream in.
            events = payload.count("event: next");
        }
        check(resp.status == 200, "sse: HTTP 200");
        check(resp.chunked, "sse: Transfer-Encoding chunked (as verified)");
        check(events >= 3, "sse: >=3 typed events streamed incrementally");
        check(payload.contains("\"playerEvents\""),
              "sse: payload carries typed playerEvents selection");
        const int first = payload.indexOf("data: ");
        if (first >= 0) {
            const int eol = payload.indexOf('\n', first);
            printf("       first frame: %s\n",
                   payload.mid(first, eol - first).constData());
        }
    }

    printf(failures == 0 ? "SPIKE PASS\n" : "SPIKE FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
