/**
 * BackendLink — the pylon's gRPC client of the player protocol
 * (api/player.proto v1 over `unix:` — the framework's SidecarService,
 * or any conforming player): holds the Events stream open forever
 * (reconnect with a fixed 1s retry — the player is a local sibling),
 * keeps a merged snapshot cache, forwards commands, and fans out
 * change pushes to the GraphQL subscriptions.
 *
 * The pylon never spawns the player: both run under the launcher /
 * supervisor, and `Query.player` is simply null while the player is
 * down.
 *
 * Snapshot vocabulary: transport/playlist keep the historic doc shape
 * (`count`, `positionMs`, ...); track carries the proto's named rich
 * fields; EQ is in Maki scale (-127..127) end to end — the proto
 * killed the 0..63 wire.
 */
import {createRequire} from 'node:module'
import {dirname, join} from 'node:path'
import {fileURLToPath} from 'node:url'

const require = createRequire(import.meta.url)
// eslint-disable-next-line @typescript-eslint/no-var-requires
const grpc = require('@grpc/grpc-js')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const loader = require('@grpc/proto-loader')

export type Snapshot = {
  epoch?: string
  revision?: number
  serverNowMs?: number
  transport?: Record<string, unknown>
  track?: Record<string, unknown>
  playlist?: {
    revision?: number
    count?: number
    currentIndex?: number
    rows?: {text?: string; durationMs?: number}[]
  }
  eq?: Record<string, unknown>
}

// Section enum values (api/player.proto).
const S = {
  TRANSPORT: 1,
  TRACK: 2,
  PLAYLIST_META: 3,
  PLAYLIST_ROWS: 4,
  EQ: 5,
  CAPABILITIES: 6
} as const

const protoPath = join(
  dirname(fileURLToPath(import.meta.url)),
  '..',
  '..',
  'player.proto'
)

function makeClient(socket: string) {
  const def = loader.loadSync(protoPath, {
    keepCase: false,
    longs: Number,
    defaults: true,
    oneofs: true
  })
  const pkg: any = grpc.loadPackageDefinition(def)
  return new pkg.wasabi.player.v1.Player(
    `unix:${socket}`,
    grpc.credentials.createInsecure()
  )
}

export class BackendLink {
  readonly socket: string
  snapshot: Snapshot | null = null
  capabilities: Record<string, unknown> | null = null
  connected = false

  /** index.ts hooks these to publish typed subscription events. */
  onPlayerChange: ((section: string) => void) | null = null
  onPlaylistChange: (() => void) | null = null
  private started = false
  private stopped = false
  private client: any

  constructor(socket?: string) {
    this.socket = socket ?? process.env.QTAMP_PLAYER_SOCKET ?? ''
    this.client = makeClient(this.socket)
  }

  start() {
    if (this.started) return
    this.started = true
    void this.loop()
  }

  /** Test hook: stop reconnecting (vitest teardown). */
  stop() {
    this.stopped = true
    this.client.close()
  }

  private call<T = any>(method: string, req: unknown): Promise<T> {
    return new Promise((resolve, reject) => {
      this.client[method](req, (err: Error | null, val: T) =>
        err ? reject(err) : resolve(val)
      )
    })
  }

  private async loop() {
    for (;;) {
      if (this.stopped) return
      try {
        await this.refetchState()
        this.capabilities = await this.call('GetCapabilities', {})
        await this.consumeEvents() // returns when the stream drops
      } catch {
        // player down or unreachable; fall through to retry
      }
      this.setConnected(false)
      if (this.stopped) return
      await new Promise(r => setTimeout(r, 1000))
    }
  }

  private setConnected(up: boolean) {
    if (this.connected === up) return
    this.connected = up
    // Connectivity edges are player-visible state: push them.
    this.publishPlayer('connectivity')
  }

  private snapshotFromState(st: any): Snapshot {
    const pl = st.playlist ?? {}
    return {
      epoch: st.epoch,
      revision: Number(st.revision) || 0,
      serverNowMs: Number(st.serverNowMs) || 0,
      transport: st.transport ?? {},
      track: st.track ?? {},
      playlist: {
        revision: Number(pl.revision) || 0,
        count: Number(pl.rowCount) || 0,
        currentIndex: pl.currentIndex ?? -1,
        rows: (pl.rows ?? []).map((r: any) => ({
          text: r.text ?? '',
          durationMs: Number(r.durationMs) || 0
        }))
      },
      eq: st.eq ?? {}
    }
  }

  private async refetchState() {
    const st = await this.call('GetState', {
      sections: [S.TRANSPORT, S.TRACK, S.PLAYLIST_ROWS, S.EQ]
    })
    this.snapshot = this.snapshotFromState(st)
    this.setConnected(true)
    this.publishPlayer('state')
    this.publishPlaylist()
  }

  private consumeEvents(): Promise<void> {
    return new Promise(resolve => {
      const stream = this.client.Events({})
      this.setConnected(true)
      stream.on('data', (e: any) => this.onEvent(e))
      stream.on('end', () => resolve())
      stream.on('error', () => resolve())
    })
  }

  private onEvent(e: any) {
    if (!e || e.payload === 'ping') return
    if (!this.snapshot) {
      void this.refetchState().catch(() => {})
      return
    }
    // Same semantics as the C++ applyEvent: epoch change means a player
    // reboot — re-snapshot; stale revisions drop.  Streams are ordered
    // and reliable, so gaps only appear across reconnects (handled by
    // the refetch in loop()).
    if (e.epoch && this.snapshot.epoch && e.epoch !== this.snapshot.epoch) {
      void this.refetchState().catch(() => {})
      return
    }
    const rev = Number(e.revision) || 0
    const have = Number(this.snapshot.revision) || 0
    if (rev && rev <= have) return

    const bump = (patch: Partial<Snapshot>) => {
      this.snapshot = {
        ...this.snapshot,
        ...patch,
        revision: rev || this.snapshot?.revision,
        serverNowMs: Number(e.serverNowMs) || this.snapshot?.serverNowMs
      }
    }

    switch (e.payload) {
      case 'transport':
        bump({transport: e.transport})
        this.publishPlayer('transport')
        break
      case 'track':
        bump({track: e.track})
        this.publishPlayer('track')
        break
      case 'eq':
        bump({eq: e.eq})
        this.publishPlayer('eq')
        break
      case 'playlistRows': {
        const pl = e.playlistRows ?? {}
        bump({
          playlist: {
            revision: Number(pl.revision) || 0,
            count: Number(pl.rowCount) || 0,
            currentIndex: pl.currentIndex ?? -1,
            rows: (pl.rows ?? []).map((r: any) => ({
              text: r.text ?? '',
              durationMs: Number(r.durationMs) || 0
            }))
          }
        })
        this.publishPlaylist()
        this.publishPlayer('playlist')
        break
      }
      case 'playlistMeta': {
        const pl = e.playlistMeta ?? {}
        bump({
          playlist: {
            ...this.snapshot.playlist,
            revision: Number(pl.revision) || 0,
            count: Number(pl.rowCount) || 0,
            currentIndex: pl.currentIndex ?? -1
          }
        })
        this.publishPlaylist()
        this.publishPlayer('playlist')
        break
      }
      default:
        break
    }
  }

  /** Map the historic op vocabulary onto a proto Command. */
  private commandFor(op: string, args: Record<string, unknown>): any {
    const expect =
      args.expectPlaylistRevision !== undefined
        ? {expectPlaylistRevision: Number(args.expectPlaylistRevision)}
        : {}
    switch (op) {
      case 'play': return {play: {}}
      case 'pause': return {pause: {}}
      case 'stop': return {stop: {}}
      case 'next': return {next: {}}
      case 'prev': return {prev: {}}
      case 'seek': return {seek: {ms: Number(args.ms) || 0}}
      case 'setVolume': return {setVolume: {v: Number(args.v) || 0}}
      case 'setPan': return {setPan: {v: Number(args.v ?? 0.5)}}
      case 'setEqOn': return {setEqOn: {on: !!args.on}}
      case 'setEqAuto': return {setEqAuto: {on: !!args.on}}
      case 'setEqPreamp':
        return {setEqPreamp: {value: Number(args.value) || 0}}
      case 'setEqBand':
        return {
          setEqBand: {band: Number(args.band), value: Number(args.value) || 0}
        }
      case 'playlistPlayRow':
        return {playRow: {row: Number(args.row), ...expect}}
      case 'playlistSetCurrentRow':
        return {setCurrentRow: {row: Number(args.row), ...expect}}
      case 'playlistAddPaths':
        return {playlistAdd: {paths: (args.paths as string[]) ?? []}}
      case 'playlistRemoveRows':
        return {playlistRemove: {rows: (args.rows as number[]) ?? [], ...expect}}
      case 'playlistClear': return {playlistClear: {}}
      case 'open': return {open: {url: String(args.url ?? '')}}
      default:
        throw new Error(`unknown op ${op}`)
    }
  }

  /**
   * Forward a command; on acceptance refresh the snapshot so mutations
   * can return the post-write state. Player rejections (stale playlist
   * revision, gated path) surface as errors to the GraphQL caller.
   */
  async cmd(op: string, args: Record<string, unknown> = {}) {
    const res = await this.call('Execute', this.commandFor(op, args))
    if (!res?.ok) throw new Error(res?.error || `command ${op} failed`)
    await this.refetchState().catch(() => {})
    return res
  }

  /** Album art as one buffer (chunked on the wire); null = no art. */
  getArt(): Promise<{data: Buffer; mime: string; token: string} | null> {
    return new Promise(resolve => {
      const stream = this.client.GetArt({})
      const chunks: Buffer[] = []
      let mime = 'image/png'
      let token = ''
      stream.on('data', (c: any) => {
        if (c.mime) mime = c.mime
        if (c.token) token = c.token
        if (c.data?.length) chunks.push(Buffer.from(c.data))
      })
      stream.on('end', () =>
        resolve(chunks.length ? {data: Buffer.concat(chunks), mime, token} : null)
      )
      stream.on('error', () => resolve(null))
    })
  }

  private publishPlayer(section: string = 'state') {
    this.onPlayerChange?.(section)
  }

  private publishPlaylist() {
    this.onPlaylistChange?.()
  }
}

export const backendLink = process.env.QTAMP_PLAYER_SOCKET
  ? new BackendLink()
  : null
