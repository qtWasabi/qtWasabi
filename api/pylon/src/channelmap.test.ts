import {describe, expect, it} from 'vitest'

import {eq63ToMaki, eqMakiTo63, richFieldsFromChannelMeta} from './channelmap'

describe('EQ scale interim (channel 0..63 slider ↔ Maki -127..127 gain)', () => {
  it('runs in opposite directions: slider top (0) is full boost', () => {
    expect(eq63ToMaki(0)).toBe(127)
    expect(eq63ToMaki(63)).toBe(-127)
    expect(eq63ToMaki(31)).toBe(0)
  })

  it('matches the reference player slider store exactly', () => {
    // QtampHost: slider = round(31 - maki*31/127)
    expect(eqMakiTo63(127)).toBe(0)
    expect(eqMakiTo63(0)).toBe(31)
    expect(eqMakiTo63(-127)).toBe(62) // asymmetric center, clamped ≥0
  })

  it('round-trips every slider step within quantization (±1)', () => {
    for (let s = 0; s <= 63; s++) {
      expect(Math.abs(eqMakiTo63(eq63ToMaki(s)) - s)).toBeLessThanOrEqual(1)
    }
  })

  it('clamps out-of-range input', () => {
    expect(eq63ToMaki(-5)).toBe(127)
    expect(eq63ToMaki(99)).toBe(-127)
    expect(eqMakiTo63(300)).toBe(0)
    expect(eqMakiTo63(-300)).toBe(63)
  })
})

describe('rich track fields from channel meta', () => {
  it('maps canonical lower-case keys onto schema fields', () => {
    expect(
      richFieldsFromChannelMeta({
        albumartist: 'VA',
        genre: 'Jungle',
        year: '1996',
        track: '7',
        disc: '2',
        composer: 'C',
        publisher: 'P'
      })
    ).toEqual({
      albumArtist: 'VA',
      genre: 'Jungle',
      year: '1996',
      trackNo: '7',
      disc: '2',
      composer: 'C',
      publisher: 'P',
      streamGenre: ''
    })
  })

  it('reads absent meta as empty fields', () => {
    const empty = richFieldsFromChannelMeta(undefined)
    expect(Object.values(empty).every(v => v === '')).toBe(true)
  })
})
