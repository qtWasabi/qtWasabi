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

// Static head assets (PYLON_STATIC_DIR): serves the wasm remote head
// (index.html + qtamp.js/.wasm + qtloader.js) from the same process —
// the cdp harness and small deployments need no extra web server.
const staticDir = (): Plugin => ({
  name: 'static-dir',
  strategy: 'last',
  setup: app => {
    const dir = process.env.PYLON_STATIC_DIR
    if (!dir) return
    const MIME: Record<string, string> = {
      '.html': 'text/html',
      '.js': 'application/javascript',
      '.wasm': 'application/wasm',
      '.svg': 'image/svg+xml',
      '.png': 'image/png'
    }
    const serveFile = async (name: string) => {
      const {readFile} = await import('node:fs/promises')
      const {join, extname, normalize} = await import('node:path')
      const safe = normalize(name).replace(/^([/\.])+/, '')
      const data = await readFile(join(dir, safe === '' ? 'index.html' : safe))
      return {data, mime: MIME[extname(safe || 'index.html')] ?? 'application/octet-stream'}
    }
    app.get('/', async c => {
      try {
        const {data, mime} = await serveFile('index.html')
        return c.body(data, 200, {'content-type': mime, 'cache-control': 'no-cache'})
      } catch {
        return c.text('not found', 404)
      }
    })
    app.get('/:file{.+\.(html|js|wasm|svg|png)}', async c => {
      try {
        const {data, mime} = await serveFile(c.req.param('file'))
        return c.body(data, 200, {'content-type': mime, 'cache-control': 'no-cache'})
      } catch {
        return c.text('not found', 404)
      }
    })
  }
})

// Album-art sidecar: pass through to the player backend with ETag
// semantics (ETag = artToken per the contract; legacy channel interim).
const artRoute = (): Plugin => ({
  name: 'art',
  setup: app => {
    const base = process.env.QTAMP_BACKEND_URL
    if (!base) return
    app.get('/art/current', async c => {
      const headers: Record<string, string> = {}
      const inm = c.req.header('if-none-match')
      if (inm) headers['if-none-match'] = inm
      try {
        const upstream = await fetch(`${base}/art/current`, {headers})
        return upstream as unknown as Response
      } catch {
        return c.text('player backend unavailable', 502)
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
  plugins: [playerAssets(), artRoute(), staticDir(), servePylon()]
} satisfies PylonConfig
