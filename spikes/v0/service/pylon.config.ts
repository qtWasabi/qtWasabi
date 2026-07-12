import {mkdirSync, rmSync, writeFileSync} from 'node:fs'
import {createServer} from 'node:http'
import {dirname, join} from 'node:path'

import {type Plugin, type PylonConfig} from '@getcronit/pylon'
import {getRequestListener, serve} from '@hono/node-server'

// Static-asset sanity: one extra route on the same app (stands in for
// the wasm head's /player/ assets — no react battery needed).
const playerAssets = (): Plugin => ({
  name: 'player-assets',
  setup: app => {
    const dir = join(process.env.SPIKE_TMP ?? '/tmp', 'wasabi2-spike-assets')
    mkdirSync(dir, {recursive: true})
    writeFileSync(join(dir, 'player.txt'), 'wasm head asset placeholder\n')
    app.get('/player/:file', async c => {
      const {readFile} = await import('node:fs/promises')
      try {
        const data = await readFile(join(dir, c.req.param('file')))
        return c.body(data, 200, {'content-type': 'text/plain'})
      } catch {
        return c.text('not found', 404)
      }
    })
  }
})

// Serve on TCP (PORT) and/or a unix socket (PYLON_SOCKET) — hono's
// serve() exposes no socket path, so the socket listener uses the
// request-listener adapter on a plain node http server.
const servePylon = (): Plugin => ({
  name: 'serve',
  strategy: 'last',
  setup: app => {
    const sock = process.env.PYLON_SOCKET
    if (sock) {
      mkdirSync(dirname(sock), {recursive: true, mode: 0o700})
      try {
        rmSync(sock)
      } catch {}
      const server = createServer(getRequestListener(app.fetch))
      server.listen(sock, () =>
        console.log(`spike pylon on unix socket ${sock}`)
      )
    }
    const raw = process.env.PORT
    const envPort = raw && raw.trim() !== '' ? Number(raw) : NaN
    if (Number.isFinite(envPort)) {
      serve({fetch: app.fetch, port: envPort}, info =>
        console.log(`spike pylon on http://localhost:${info.port}`)
      )
    }
  }
})

export default {
  graphiql: false,
  landingPage: false,
  plugins: [playerAssets(), servePylon()]
} satisfies PylonConfig
