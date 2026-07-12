// Pure channel→schema mapping helpers (unit-tested; no I/O).
//
// EQ interim: the legacy channel carries the classic Winamp EQ SLIDER
// scale 0..63 (0 = top = +12 dB boost, 63 = bottom = -12 dB cut,
// 31 flat); the schema mandates the Maki GAIN scale -127..127
// (+127 = boost). The two run in OPPOSITE directions — the mapping
// inverts, matching the reference player's slider store exactly.
// Converted here (quantized) until the gRPC player protocol (V4)
// carries the Maki scale natively.
export const eq63ToMaki = (v: number) =>
  Math.max(-127, Math.min(127, Math.round(((31 - v) * 127) / 31)))
export const eqMakiTo63 = (v: number) =>
  Math.max(0, Math.min(63, Math.round(31 - (v * 31) / 127)))

// Rich track metadata: the channel's additive `track.meta` object keyed
// by canonical lower-case field names. Absent keys read as ''.
export interface RichTrackFields {
  albumArtist: string
  genre: string
  year: string
  trackNo: string
  disc: string
  composer: string
  publisher: string
  streamGenre: string
}

export const richFieldsFromChannelMeta = (
  meta: Record<string, string> | undefined
): RichTrackFields => {
  const m = meta ?? {}
  return {
    albumArtist: m['albumartist'] ?? '',
    genre: m['genre'] ?? '',
    year: m['year'] ?? '',
    trackNo: m['track'] ?? '',
    disc: m['disc'] ?? '',
    composer: m['composer'] ?? '',
    publisher: m['publisher'] ?? '',
    streamGenre: m['streamgenre'] ?? ''
  }
}
