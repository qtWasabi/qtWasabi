// HeadTransport implementation (moved from the reference embedder).
#include <qtWasabi/head/HeadTransport.h>

#include <QUrl>

#include <qtWasabi/remote/GraphQLTransport.h>
#include <qtWasabi/head/HeadPreferences.h>

namespace qtWasabi::head {

remote::RemoteTransport *makeTransport(QString &connectUrl,
                                       const QString &settingsFile) {
    if (connectUrl.startsWith(QLatin1String("graphql+unix://"))) {
        const QString path =
            connectUrl.mid(int(qstrlen("graphql+unix://")));
        connectUrl = QStringLiteral("http://qtwasabi.local");  // placeholder
        return new remote::GraphQLLocalTransport(path);
    }
    if (connectUrl.startsWith(QLatin1String("unix://"))) {
        const QString path = connectUrl.mid(int(qstrlen("unix://")));
        connectUrl = QStringLiteral("http://qtwasabi.local");  // placeholder
        return new remote::GraphQLLocalTransport(path);
    }
    // GraphQL is the only head data path; plain http(s):// means the
    // pylon's GraphQL endpoint (the graphql+ prefix stays accepted).
    const QString original = connectUrl;
    if (connectUrl.startsWith(QLatin1String("graphql+")))
        connectUrl = connectUrl.mid(int(qstrlen("graphql+")));
    auto *t = new remote::GraphQLHttpTransport();
    // Bearer token for a token-gated remote pylon: QTAMP_BEARER_TOKEN
    // wins; otherwise the token stored with a matching backend entry
    // (Preferences > Connection).  The unix-socket path never needs
    // one (filesystem trust).  Note: the wasm GET-subscribe leg cannot
    // send headers (browser EventSource) — wasm+token is an open gap
    // until the pylon grows a query-param fallback.
    QString token;
    if (qEnvironmentVariableIsSet("QTAMP_BEARER_TOKEN")) {
        // Set-but-empty is an explicit "no token" override.
        token = qEnvironmentVariable("QTAMP_BEARER_TOKEN");
    } else {
        // Canonicalize both sides (graphql+ prefix, trailing slash) —
        // an exact-string miss would silently connect unauthenticated.
        auto canon = [](QString u) {
            if (u.startsWith(QLatin1String("graphql+")))
                u = u.mid(int(qstrlen("graphql+")));
            while (u.endsWith(QLatin1Char('/'))) u.chop(1);
            return u;
        };
        const QString want = canon(original);
        const auto entries =
            HeadPreferences::loadBackends(settingsFile);
        for (const auto &e : entries) {
            if (canon(e.url) == want) {
                token = e.token;
                break;
            }
        }
    }
    if (!token.isEmpty()) {
        t->setExtraHeaders({{QByteArrayLiteral("Authorization"),
                             QByteArrayLiteral("Bearer ") +
                                 token.toUtf8()}});
    }
    return t;
}

}  // namespace qtWasabi::head
