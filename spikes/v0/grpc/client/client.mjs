// V0 spike (c) client: @grpc/grpc-js over a unix socket against the
// Qt/grpc++ spike server — unary GetState + server-streamed Events.
import grpc from '@grpc/grpc-js'
import loader from '@grpc/proto-loader'

const sock = process.argv[2] ?? process.env.SPIKE_GRPC_SOCKET
if (!sock) {
  console.error('usage: node client.mjs <socket path>')
  process.exit(2)
}

const def = loader.loadSync(new URL('../spike.proto', import.meta.url).pathname, {
  keepCase: false,
  longs: Number,
  defaults: true
})
const pkg = grpc.loadPackageDefinition(def)
const client = new pkg.wasabi.spike.SpikePlayer(
  `unix:${sock}`,
  grpc.credentials.createInsecure()
)

let failures = 0
const check = (ok, label) => {
  console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${label}`)
  if (!ok) failures++
}

client.GetState({}, (err, state) => {
  check(!err, `unary GetState (${err ? err.message : 'ok'})`)
  if (!err) {
    check(state.epoch === 'spike-epoch-1', 'unary: epoch matches')
    check(state.transport && state.transport.volume === 70,
      'unary: nested transport arrives typed')
    console.log('       state:', JSON.stringify(state))
  }

  const stream = client.Events({})
  const frames = []
  const timer = setTimeout(() => stream.cancel(), 4000)
  stream.on('data', s => {
    frames.push(s)
    if (frames.length >= 3) {
      clearTimeout(timer)
      stream.cancel()
    }
  })
  stream.on('error', () => {})
  stream.on('end', () => {
    check(frames.length >= 3, `stream: ${frames.length} frames received`)
    const revs = frames.map(f => Number(f.revision))
    check(revs.every((r, i) => i === 0 || r > revs[i - 1]),
      'stream: revisions strictly increasing (Qt-signal bridge works)')
    console.log('       frames:', JSON.stringify(revs))
    console.log(failures === 0 ? 'SPIKE PASS' : `SPIKE FAIL (${failures})`)
    process.exit(failures === 0 ? 0 : 1)
  })
})
