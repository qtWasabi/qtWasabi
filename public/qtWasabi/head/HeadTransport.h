// makeTransport — the head's transport factory (V5f-shared).
//
// Resolves a --connect style URL to the right GraphQL transport:
// graphql+unix:///path (or unix://) → GraphQLLocalTransport;
// everything else → GraphQLHttpTransport with the bearer token the
// pylon gate expects, sourced from QTAMP_BEARER_TOKEN (set-but-empty
// = explicitly unauthenticated) or the stored backend entry whose
// canonicalized URL matches.  GraphQL is the only head data path.
#pragma once

#include <QString>

namespace qtWasabi::remote {
class RemoteTransport;
}

namespace qtWasabi::head {

// `connectUrl` is rewritten in place to the effective http(s) base
// (the graphql+ prefix stripped; unix sockets get a placeholder).
remote::RemoteTransport *makeTransport(QString &connectUrl,
                                       const QString &settingsFile);

}  // namespace qtWasabi::head
