// qtwasabi-pylon — THE GraphQL server of the qtWasabi framework
// (Wasabi 2).  This V0 skeleton authors the canonical schema v2.0.0
// code-first against a deterministic MOCK player; the gRPC BackendLink
// to real players lands in V3/V4.  The committed api/schema.graphql is
// generated from this file (CI drift check).
//
// Sync semantics (SDL docstrings = the contract):
//   epoch changes  => consumers drop caches and refetch Query.player
//   revision gaps  => consumers refetch Query.player
//   position is NEVER pushed on a timer — transport events fire on
//   edges; positionMs is valid at positionAtMs (server clock) and
//   heads interpolate (PositionClock).  The PING event every 5s
//   carries serverNowMs as the clock beacon.
//
// EQ scale: bands + preamp are -127..127 (Maki scale) end to end.
import {Pylon, experimentalCreatePubSub} from '@getcronit/pylon'

import {backendLink} from './backendlink'

// ── typed surface ────────────────────────────────────────────────────

export class Transport {
  playing!: boolean
  paused!: boolean
  positionMs!: number
  positionAtMs!: number
  durationMs!: number
  volume!: number
  pan!: number
  shuffle!: boolean
  repeat!: 'OFF' | 'ALL' | 'ONE'
}

export class Track {
  title!: string
  artist!: string
  album!: string
  albumArtist!: string
  genre!: string
  year!: string
  trackNo!: string
  disc!: string
  composer!: string
  publisher!: string
  streamGenre!: string
  filename!: string
  displayTitle!: string
  decoder!: string
  bitrate!: number
  sampleRate!: number
  channels!: number
  artToken!: string | null
  artUrl!: string | null
}

export class PlaylistRow {
  index!: number
  text!: string
  durationMs!: number
}

export class PlaylistMeta {
  revision!: number
  currentIndex!: number
  rowCount!: number
}

export class Eq {
  on!: boolean
  auto!: boolean
  preamp!: number
  bands!: number[]
}

export class Capabilities {
  localFiles!: boolean
  ingest!: boolean
  playlistEdit!: boolean
  library!: boolean
  mediaLibrary!: boolean
  visPcm!: boolean
  pcmStream!: boolean
  preferences!: boolean
  shuffleRepeat!: boolean
  musicRoot!: string | null
}

export class ApiInfo {
  schemaVersion!: string
  playerProtocol!: string
  serverName!: string
}

export class LibraryNode {
  label!: string
  path!: string
  hasChildren!: boolean
}

export class MlFilterValue {
  name!: string
  count!: number
}

export class MlTrack {
  artist!: string
  album!: string
  title!: string
  genre!: string
  trackNo!: number
  year!: number
  lengthMs!: number
  path!: string
}

export class MlTrackPage {
  totalCount!: number
  rows!: MlTrack[]
}

export class PrefField {
  key!: string
  label!: string
  type!: 'BOOL' | 'INT' | 'FLOAT' | 'STRING' | 'ENUM' | 'PATH'
  valueJson!: string
  min!: number | null
  max!: number | null
  step!: number | null
  enumValues!: string[]
  maxLength!: number | null
}

export class PrefSection {
  title!: string
  fields!: PrefField[]
}

export class PrefPage {
  id!: string
  title!: string
  sections!: PrefSection[]
}

export class MenuItem {
  id!: string
  label!: string
  section!: string
  enabled!: boolean
  checkable!: boolean
  checked!: boolean
  children!: MenuItem[]
}

export class UiExtensions {
  prefPages!: PrefPage[]
  menuItems!: MenuItem[]
}

export class PlayerEvent {
  kind!:
    | 'STATE'
    | 'TRANSPORT'
    | 'TRACK'
    | 'EQ'
    | 'PLAYLIST_META'
    | 'UI_EXTENSIONS'
    | 'CAPABILITIES'
    | 'PING'
  epoch!: string
  revision!: number
  serverNowMs!: number
  transport!: Transport | null
  track!: Track | null
  eq!: Eq | null
  playlist!: PlaylistMeta | null
  uiExtensions!: UiExtensions | null
  capabilities!: Capabilities | null
}

export class Playlist extends PlaylistMeta {
  rows!: PlaylistRow[]
}

export class PlaylistEvent {
  epoch!: string
  revision!: number
  serverNowMs!: number
  playlist!: Playlist
}

export class SpectrumFrame {
  serverNowMs!: number
  // base64 at this edge; binary bytes on the player protocol.
  spectrum!: string
  peaks!: string | null
  osc!: string
  vuLeft!: number
  vuRight!: number
}

export class PcmFrame {
  serverNowMs!: number
  sampleRate!: number
  channels!: number
  // base64 s16le interleaved; LOCAL connections only — the pylon
  // refuses this subscription on non-local transports.
  samples!: string
}

export class Player {
  epoch!: string
  revision!: number
  serverNowMs!: number
  transport!: Transport
  track!: Track | null
  playlist!: Playlist
  eq!: Eq
}

export class CommandResult {
  ok!: boolean
  error!: string | null
  revision!: number
  player!: Player
}

// ── mock player state (deterministic; replaced by BackendLink in V3+) ─

const state = {
  epoch: 'mock-epoch-1',
  revision: 1,
  transport: {
    playing: false,
    paused: false,
    positionMs: 0,
    positionAtMs: 1000,
    durationMs: 0,
    volume: 70,
    pan: 0.5,
    shuffle: false,
    repeat: 'OFF' as const
  },
  track: null as Track | null,
  playlist: {revision: 0, currentIndex: -1, rowCount: 0, rows: [] as PlaylistRow[]},
  eq: {on: false, auto: false, preamp: 0, bands: Array(10).fill(0)}
}

const serverNowMs = () => 1000 + state.revision

const currentPlayer = (): Player => ({
  epoch: state.epoch,
  revision: state.revision,
  serverNowMs: serverNowMs(),
  transport: state.transport,
  track: state.track,
  playlist: state.playlist,
  eq: state.eq
})

const pubSub = experimentalCreatePubSub<{
  playerEvents: [event: PlayerEvent]
  playlistEvents: [event: PlaylistEvent]
  spectrumFrames: [frame: SpectrumFrame]
  pcmFrames: [frame: PcmFrame]
}>()

const emit = (kind: PlayerEvent['kind']) => {
  state.revision += 1
  const ev: PlayerEvent = {
    kind,
    epoch: state.epoch,
    revision: state.revision,
    serverNowMs: serverNowMs(),
    transport: kind === 'TRANSPORT' || kind === 'STATE' ? state.transport : null,
    track: kind === 'TRACK' || kind === 'STATE' ? state.track : null,
    eq: kind === 'EQ' || kind === 'STATE' ? state.eq : null,
    playlist:
      kind === 'PLAYLIST_META' || kind === 'STATE'
        ? {
            revision: state.playlist.revision,
            currentIndex: state.playlist.currentIndex,
            rowCount: state.playlist.rowCount
          }
        : null,
    uiExtensions: null,
    capabilities: null
  }
  pubSub.publish('playerEvents', ev)
}

// PING every 5s — the PositionClock beacon.
setInterval(() => {
  pubSub.publish('playerEvents', {
    kind: 'PING',
    epoch: state.epoch,
    revision: state.revision,
    serverNowMs: serverNowMs(),
    transport: null,
    track: null,
    eq: null,
    playlist: null,
    uiExtensions: null,
    capabilities: null
  })
}, 5000)

const result = (ok: boolean, error: string | null = null): CommandResult => ({
  ok,
  error,
  revision: state.revision,
  player: currentPlayer()
})

const guardPlaylist = (expect?: number | null): string | null =>
  expect != null && expect !== state.playlist.revision
    ? `playlistRevision mismatch: expected ${expect}, have ${state.playlist.revision}`
    : null

// ── channel source (real player behind qtamp --backend) ──────────────
// Active when QTAMP_BACKEND_URL is set; the mock above serves tests.
// EQ scale + rich-metadata mapping: src/channelmap.ts (unit-tested).

const channelPlayer = (): Player => {
  const s = backendLink?.snapshot
  if (!s) throw new Error('player backend unavailable')
  const t: any = s.transport ?? {}
  const tr: any = s.track ?? {}
  const pl = s.playlist ?? {}
  const eq: any = s.eq ?? {}
  const rows = (pl.rows ?? []).map((r, i) => ({
    index: i,
    text: r.text ?? '',
    durationMs: Number(r.durationMs) || 0
  }))
  return {
    epoch: s.epoch ?? 'unknown',
    revision: Number(s.revision) || 0,
    serverNowMs: Number(s.serverNowMs) || 0,
    transport: {
      playing: !!t.playing,
      paused: !!t.paused,
      positionMs: Number(t.positionMs) || 0,
      positionAtMs: Number(t.positionAtMs) || 0,
      durationMs: Number(t.durationMs) || 0,
      volume: Number(t.volume) || 0,
      pan: typeof t.pan === 'number' ? t.pan : 0.5,
      shuffle: !!t.shuffle,
      repeat: String(t.repeat ?? 'REPEAT_OFF').replace('REPEAT_', '') as
        | 'OFF'
        | 'ALL'
        | 'ONE'
    },
    track: tr.title || tr.filename
      ? {
          title: tr.title ?? '',
          artist: tr.artist ?? '',
          album: tr.album ?? '',
          albumArtist: tr.albumArtist ?? '',
          genre: tr.genre ?? '',
          year: tr.year ?? '',
          trackNo: tr.trackNo ?? '',
          disc: tr.disc ?? '',
          composer: tr.composer ?? '',
          publisher: tr.publisher ?? '',
          streamGenre: tr.streamGenre ?? '',
          filename: tr.filename ?? '',
          displayTitle: tr.displayTitle ?? tr.title ?? '',
          decoder: tr.decoder ?? '',
          bitrate: Number(tr.bitrate) || 0,
          sampleRate: Number(tr.sampleRate) || 0,
          channels: Number(tr.channels) || 0,
          artToken: tr.artToken ? String(tr.artToken) : null,
          artUrl: tr.filename ? '/art/current' : null
        }
      : null,
    playlist: {
      revision: Number(pl.revision) || 0,
      currentIndex: pl.currentIndex ?? -1,
      rowCount: pl.count ?? rows.length,
      rows
    },
    eq: {
      on: !!eq.on,
      auto: !!eq.auto,
      // Maki scale end to end since the gRPC player protocol.
      preamp: typeof eq.preamp === 'number' ? eq.preamp : 0,
      bands: (Array.isArray(eq.bands) ? eq.bands : Array(10).fill(0)).map(
        (b: unknown) => (typeof b === 'number' ? b : 0)
      )
    }
  }
}

const player = (): Player => (backendLink ? channelPlayer() : currentPlayer())

const channelResult = async (
  op: string,
  args: Record<string, unknown> = {}
): Promise<CommandResult> => {
  try {
    await backendLink!.cmd(op, args)
    const p = channelPlayer()
    return {ok: true, error: null, revision: p.revision, player: p}
  } catch (e) {
    let p: Player
    try {
      p = channelPlayer()
    } catch {
      p = currentPlayer()
    }
    return {
      ok: false,
      error: e instanceof Error ? e.message : String(e),
      revision: p.revision,
      player: p
    }
  }
}

// Bridge channel changes into the typed subscriptions.
if (backendLink) {
  backendLink.onPlayerChange = (section: string) => {
    let p: Player
    try {
      p = channelPlayer()
    } catch {
      return
    }
    const kind: PlayerEvent['kind'] =
      section === 'transport'
        ? 'TRANSPORT'
        : section === 'track'
          ? 'TRACK'
          : section === 'eq'
            ? 'EQ'
            : section === 'playlist'
              ? 'PLAYLIST_META'
              : 'STATE'
    pubSub.publish('playerEvents', {
      kind,
      epoch: p.epoch,
      revision: p.revision,
      serverNowMs: p.serverNowMs,
      transport: kind === 'TRANSPORT' || kind === 'STATE' ? p.transport : null,
      track: kind === 'TRACK' || kind === 'STATE' ? p.track : null,
      eq: kind === 'EQ' || kind === 'STATE' ? p.eq : null,
      playlist:
        kind === 'PLAYLIST_META' || kind === 'STATE'
          ? {
              revision: p.playlist.revision,
              currentIndex: p.playlist.currentIndex,
              rowCount: p.playlist.rowCount
            }
          : null,
      uiExtensions: null,
      capabilities: null
    })
  }
  backendLink.onPlaylistChange = () => {
    let p: Player
    try {
      p = channelPlayer()
    } catch {
      return
    }
    pubSub.publish('playlistEvents', {
      epoch: p.epoch,
      revision: p.revision,
      serverNowMs: p.serverNowMs,
      playlist: p.playlist
    })
  }
  backendLink.start()
}

// ── the app ──────────────────────────────────────────────────────────

export default new Pylon({
  graphql: {
    Query: {
      /**
       * The full player snapshot — the resync path.  Refetch on epoch
       * change or revision gap.
       */
      player,
      capabilities: (): Capabilities => {
        const c: any = backendLink?.capabilities
        if (c) {
          return {
            localFiles: !!c.localFiles,
            ingest: !!c.ingest,
            playlistEdit: !!c.playlistEdit,
            library: !!c.library,
            mediaLibrary: !!c.mediaLibrary,
            visPcm: !!c.visPcm,
            pcmStream: !!c.pcmStream,
            preferences: !!c.preferences,
            shuffleRepeat: !!c.shuffleRepeat,
            musicRoot: c.musicRoot || null
          }
        }
        return {
          localFiles: false,
          ingest: false,
          playlistEdit: true,
          library: false,
          mediaLibrary: false,
          visPcm: false,
          pcmStream: false,
          preferences: false,
          shuffleRepeat: false,
          musicRoot: null
        }
      },
      apiInfo: (): ApiInfo => ({
        schemaVersion: '2.0.0',
        playerProtocol: '1.0.0',
        serverName: 'qtwasabi-pylon (mock player)'
      }),
      library: (parent: string = ''): LibraryNode[] => [],
      mlFilterValues: (
        field: string,
        countField?: string
      ): MlFilterValue[] => [],
      mlTracks: (offset: number = 0, limit: number = 1000): MlTrackPage => ({
        totalCount: 0,
        rows: []
      }),
      uiExtensions: (): UiExtensions => ({prefPages: [], menuItems: []})
    },
    Mutation: {
      play: async (): Promise<CommandResult> => {
        if (backendLink) return channelResult('play')
        state.transport.playing = true
        state.transport.paused = false
        emit('TRANSPORT')
        return result(true)
      },
      pause: async (): Promise<CommandResult> => {
        if (backendLink) return channelResult('pause')
        state.transport.paused = true
        emit('TRANSPORT')
        return result(true)
      },
      stop: async (): Promise<CommandResult> => {
        if (backendLink) return channelResult('stop')
        state.transport.playing = false
        state.transport.paused = false
        state.transport.positionMs = 0
        emit('TRANSPORT')
        return result(true)
      },
      next: async (): Promise<CommandResult> =>
        backendLink ? channelResult('next') : result(true),
      prev: async (): Promise<CommandResult> =>
        backendLink ? channelResult('prev') : result(true),
      seek: async (ms: number): Promise<CommandResult> => {
        if (backendLink) return channelResult('seek', {ms})
        state.transport.positionMs = ms
        state.transport.positionAtMs = serverNowMs()
        emit('TRANSPORT')
        return result(true)
      },
      setVolume: async (v: number): Promise<CommandResult> => {
        if (backendLink) return channelResult('setVolume', {v})
        state.transport.volume = Math.max(0, Math.min(100, v))
        emit('TRANSPORT')
        return result(true)
      },
      setPan: async (v: number): Promise<CommandResult> => {
        if (backendLink) return channelResult('setPan', {v})
        state.transport.pan = Math.max(0, Math.min(1, v))
        emit('TRANSPORT')
        return result(true)
      },
      setShuffle: (on: boolean): CommandResult =>
        result(false, 'shuffle not supported by this player'),
      setRepeat: (mode: string): CommandResult =>
        result(false, 'repeat not supported by this player'),
      setEqOn: async (on: boolean): Promise<CommandResult> => {
        if (backendLink) return channelResult('setEqOn', {on})
        state.eq.on = on
        emit('EQ')
        return result(true)
      },
      setEqAuto: async (on: boolean): Promise<CommandResult> => {
        if (backendLink) return channelResult('setEqAuto', {on})
        state.eq.auto = on
        emit('EQ')
        return result(true)
      },
      /** Preamp, Maki scale -127..127. */
      setEqPreamp: async (value: number): Promise<CommandResult> => {
        if (backendLink)
          return channelResult('setEqPreamp', {value})
        state.eq.preamp = Math.max(-127, Math.min(127, value))
        emit('EQ')
        return result(true)
      },
      /** Band 0..9, Maki scale -127..127. */
      setEqBand: async (
        band: number,
        value: number
      ): Promise<CommandResult> => {
        if (backendLink)
          return channelResult('setEqBand', {band, value})
        if (band < 0 || band > 9) return result(false, 'band out of range')
        state.eq.bands[band] = Math.max(-127, Math.min(127, value))
        emit('EQ')
        return result(true)
      },
      playRow: async (
        row: number,
        expectPlaylistRevision?: number
      ): Promise<CommandResult> => {
        if (backendLink)
          return channelResult('playlistPlayRow', {row, expectPlaylistRevision})
        const err = guardPlaylist(expectPlaylistRevision)
        if (err) return result(false, err)
        if (row < 0 || row >= state.playlist.rowCount)
          return result(false, 'row out of range')
        state.playlist.currentIndex = row
        state.transport.playing = true
        emit('PLAYLIST_META')
        return result(true)
      },
      setCurrentRow: async (
        row: number,
        expectPlaylistRevision?: number
      ): Promise<CommandResult> => {
        if (backendLink)
          return channelResult('playlistSetCurrentRow', {
            row,
            expectPlaylistRevision
          })
        const err = guardPlaylist(expectPlaylistRevision)
        if (err) return result(false, err)
        state.playlist.currentIndex = row
        emit('PLAYLIST_META')
        return result(true)
      },
      playlistAdd: async (paths: string[]): Promise<CommandResult> => {
        if (backendLink) return channelResult('playlistAddPaths', {paths})
        for (const p of paths) {
          state.playlist.rows.push({
            index: state.playlist.rows.length,
            text: p,
            durationMs: 0
          })
        }
        state.playlist.rowCount = state.playlist.rows.length
        state.playlist.revision += 1
        emit('PLAYLIST_META')
        pubSub.publish('playlistEvents', {
          epoch: state.epoch,
          revision: state.revision,
          serverNowMs: serverNowMs(),
          playlist: state.playlist
        })
        return result(true)
      },
      playlistRemove: async (
        rows: number[],
        expectPlaylistRevision?: number
      ): Promise<CommandResult> => {
        if (backendLink)
          return channelResult('playlistRemoveRows', {
            rows,
            expectPlaylistRevision
          })
        const err = guardPlaylist(expectPlaylistRevision)
        if (err) return result(false, err)
        const drop = new Set(rows)
        state.playlist.rows = state.playlist.rows
          .filter(r => !drop.has(r.index))
          .map((r, i) => ({...r, index: i}))
        state.playlist.rowCount = state.playlist.rows.length
        state.playlist.revision += 1
        emit('PLAYLIST_META')
        pubSub.publish('playlistEvents', {
          epoch: state.epoch,
          revision: state.revision,
          serverNowMs: serverNowMs(),
          playlist: state.playlist
        })
        return result(true)
      },
      playlistClear: async (): Promise<CommandResult> => {
        if (backendLink) return channelResult('playlistClear')
        state.playlist.rows = []
        state.playlist.rowCount = 0
        state.playlist.currentIndex = -1
        state.playlist.revision += 1
        emit('PLAYLIST_META')
        pubSub.publish('playlistEvents', {
          epoch: state.epoch,
          revision: state.revision,
          serverNowMs: serverNowMs(),
          playlist: state.playlist
        })
        return result(true)
      },
      pleditCommand: (verb: string): CommandResult =>
        result(false, 'pledit menus not supported by this player'),
      open: async (url: string): Promise<CommandResult> =>
        backendLink
          ? channelResult('open', {url})
          : result(false, 'open not supported by this player (no musicRoot)'),
      mlPlayTracks: (
        paths: string[],
        startRow: number,
        enqueueOnly: boolean = false
      ): CommandResult => result(false, 'media library not supported'),
      showPreferences: (): CommandResult =>
        result(false, 'preferences not supported by this player'),
      setPlayerPref: (key: string, valueJson: string): CommandResult =>
        result(false, 'no player preferences declared'),
      invokeMenuItem: (
        id: string,
        context?: {
          playlistRows?: number[]
          expectPlaylistRevision?: number
          libraryPath?: string
        }
      ): CommandResult => result(false, 'no player menu items declared')
    },
    Subscription: {
      /**
       * Sectional pushes WITHOUT playlist rows.  First value after
       * subscribing is the next change; do an initial Query.player for
       * the snapshot.  kind=PING every 5s carries serverNowMs.
       */
      playerEvents: () => pubSub.subscribe('playerEvents'),
      /** Full row sets, pushed on playlist change. */
      playlistEvents: () => pubSub.subscribe('playlistEvents'),
      /** ≤20fps, produced only while subscribed and playing. */
      spectrumFrames: () => pubSub.subscribe('spectrumFrames'),
      /** LOCAL connections only; refused on TCP by the serve layer. */
      pcmFrames: () => pubSub.subscribe('pcmFrames')
    }
  }
})
