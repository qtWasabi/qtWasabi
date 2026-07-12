// Proto-level conformance probe: drives a player-protocol server
// (api/player.proto v1) over grpc-js and asserts the contract —
// version, capabilities, sectioned state, section-masked command
// echoes, cold-start play, exact seek, the playlist revision guard,
// unsupported-op errors, ordered events, UNIMPLEMENTED optional
// streams.  Written against fake-sidecar's FakeHost (deterministic
// idle: stopped, volume 70, EQ flat).
//
// usage: node probe.mjs <socketPath> <player.proto> <musicRoot>
import grpc from '@grpc/grpc-js'
import loader from '@grpc/proto-loader'

const [sock, proto, music] = process.argv.slice(2)
const def = loader.loadSync(proto, {
  keepCase: false,
  longs: Number,
  defaults: true,
  oneofs: true
})
const pkg = grpc.loadPackageDefinition(def)
const c = new pkg.wasabi.player.v1.Player(
  `unix:${sock}`,
  grpc.credentials.createInsecure()
)

let fail = 0
const check = (ok, l) => {
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${l}`)
  if (!ok) fail++
}
const call = (m, req) => new Promise(r => c[m](req, (e, v) => r([e, v])))

const [ev, v] = await call('GetVersion', {})
check(!ev && v.protocol === '1.0.0', `GetVersion (${v?.protocol}, ${v?.player})`)

const [ec, caps] = await call('GetCapabilities', {})
check(!ec && caps.playlistEdit === false && caps.musicRoot === music,
  'capabilities (no playlist-edit hooks, music root)')

const [es, st] = await call('GetState', {})
check(!es && st.epoch.length > 10, `state epoch (${st?.epoch?.slice(0, 8)})`)
check(st.transport.volume === 70 && !st.transport.playing, 'idle: stopped, volume 70')
check(st.eq.preamp === 0 && st.eq.bands.length === 10 &&
      st.eq.bands.every(b => b === 0), 'idle: EQ flat in Maki scale')
check(st.playlist.rowCount === 0 && !st.playlist.rows.length,
  'default sections: playlist META only, empty')

const stream = c.Events({})
const events = []
stream.on('data', e => events.push(e))
stream.on('error', () => {})
await new Promise(r => setTimeout(r, 300))

const [ea, add] = await call('Execute',
  {playlistAdd: {paths: [`${music}/fake.mp3`]}, echo: [3]})
check(!ea && add.ok && add.state.playlist.rowCount === 1 &&
      !add.state.playlist.rows.length,
  'playlistAdd: echo=PLAYLIST_META carries count, NO rows')
check(!add.state.transport, 'echo mask: unrequested sections absent')

const [eb, bad] = await call('Execute',
  {playlistAdd: {paths: ['/etc/passwd']}})
check(!eb && bad.ok === false && bad.error.includes('music root'),
  'music-root confinement rejects')

const [ep, play] = await call('Execute', {play: {}, echo: [1]})
check(!ep && play.ok && play.state.transport.playing === true,
  'cold-start play lands on the current row')

const [esk, seek] = await call('Execute', {seek: {ms: 60000}, echo: [1]})
check(!esk && seek.ok && seek.state.transport.positionMs === 60000,
  'seek is exact (deterministic host)')

const [eg, guard] = await call('Execute',
  {playRow: {row: 0, expectPlaylistRevision: 999}})
check(!eg && guard.ok === false && guard.error.includes('ismatch'),
  'playlist revision guard rejects')

const [eu, unsup] = await call('Execute', {setEqOn: {on: true}})
check(!eu && unsup.ok === false && unsup.error === 'unsupported',
  'missing hook surfaces as unsupported, not a crash')

await new Promise(r => setTimeout(r, 600))
stream.cancel()
const kinds = new Set(events.map(e => e.payload))
check(kinds.has('transport') && kinds.has('playlistRows'),
  `events pushed on edges (${[...kinds].join(',')})`)
const revs = events.filter(e => e.payload !== 'ping').map(e => e.revision)
check(revs.every((r, i) => i === 0 || r >= revs[i - 1]),
  'event revisions are monotonic')
const rowsEv = events.filter(e => e.payload === 'playlistRows').pop()
check(rowsEv && rowsEv.playlistRows.rows.length === 1 &&
      rowsEv.playlistRows.rows[0].durationMs === 175000,
  'playlist_rows event carries the canned row')

const spectrum = c.SpectrumFrames({})
const spectrumStatus = await new Promise(r => {
  spectrum.on('error', e => r(e.code))
  spectrum.on('end', () => r(null))
})
check(spectrumStatus === grpc.status.UNIMPLEMENTED,
  'SpectrumFrames is UNIMPLEMENTED (V7), not a hang')

console.log(fail ? 'conformance: FAILURES' : 'conformance: all checks passed')
process.exit(fail ? 1 : 0)
