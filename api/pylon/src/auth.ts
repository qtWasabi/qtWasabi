// Bearer-token gate for TCP serving (socket conventions, okf/api):
// the unix socket is filesystem trust and never passes through here;
// loopback TCP is the local dev/test path and stays open; any
// NON-loopback TCP request must carry the configured bearer token —
// and is refused entirely when none is configured.
//
// Pure decision function so it unit-tests without sockets.
export interface AuthInput {
  // req.socket.remoteAddress of the node request ('' for unix sockets).
  remoteAddress: string | undefined
  // The Authorization header, verbatim (undefined when absent).
  authorization: string | undefined
  // The configured token (PYLON_BEARER_TOKEN), undefined when unset.
  token: string | undefined
}

const LOOPBACK = new Set(['127.0.0.1', '::1', '::ffff:127.0.0.1'])

export const isLoopback = (addr: string | undefined): boolean =>
  // Unix-socket requests report no remote address — same trust bucket.
  !addr || LOOPBACK.has(addr)

export const authDecision = (input: AuthInput): 'ok' | 'unauthorized' => {
  if (isLoopback(input.remoteAddress)) return 'ok'
  if (!input.token) return 'unauthorized'
  return input.authorization === `Bearer ${input.token}`
    ? 'ok'
    : 'unauthorized'
}
