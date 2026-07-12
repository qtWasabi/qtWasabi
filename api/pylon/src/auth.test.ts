import {describe, expect, it} from 'vitest'

import {authDecision} from './auth'

const TOKEN = 'sekrit'

describe('bearer gate (TCP serving)', () => {
  it('lets loopback through without a token', () => {
    for (const addr of ['127.0.0.1', '::1', '::ffff:127.0.0.1']) {
      expect(
        authDecision({remoteAddress: addr, authorization: undefined, token: undefined})
      ).toBe('ok')
    }
  })

  it('treats missing remote address (unix socket) as local trust', () => {
    expect(
      authDecision({remoteAddress: undefined, authorization: undefined, token: TOKEN})
    ).toBe('ok')
  })

  it('refuses non-loopback entirely when no token is configured', () => {
    expect(
      authDecision({
        remoteAddress: '192.168.1.50',
        authorization: 'Bearer anything',
        token: undefined
      })
    ).toBe('unauthorized')
  })

  it('requires the exact bearer token on non-loopback', () => {
    const base = {remoteAddress: '203.0.113.9', token: TOKEN}
    expect(authDecision({...base, authorization: undefined})).toBe('unauthorized')
    expect(authDecision({...base, authorization: 'Bearer wrong'})).toBe('unauthorized')
    expect(authDecision({...base, authorization: TOKEN})).toBe('unauthorized')
    expect(authDecision({...base, authorization: `Bearer ${TOKEN}`})).toBe('ok')
  })
})
