// V0 spike service: does Pylon v3 stream OBJECT-TYPED subscription
// payloads (the v2 limitation forced JSON-string scalars)?  Authoring
// follows the v3 nodejs-subscriptions example: experimentalCreatePubSub
// + subscribe() resolvers (bare async generators blow the introspector,
// see spike README).
import {Pylon, experimentalCreatePubSub} from '@getcronit/pylon'

class Transport {
  playing!: boolean
  positionMs!: number
  volume!: number
}

class PlayerEvent {
  kind!: string
  epoch!: string
  revision!: number
  serverNowMs!: number
  transport!: Transport | null
}

const makeEvent = (
  kind: string,
  revision: number,
  transport: Transport | null
): PlayerEvent => ({
  kind,
  epoch: 'spike-epoch-1',
  revision,
  serverNowMs: 1000 + revision,
  transport
})

const makeTransport = (
  playing: boolean,
  positionMs: number,
  volume: number
): Transport => ({playing, positionMs, volume})

const pubSub = experimentalCreatePubSub<{
  playerEvents: [event: PlayerEvent]
  stringEvents: [payload: string]
}>()

// Emit a small scripted sequence so a subscriber sees typed frames.
let revision = 1
const tick = () => {
  revision += 1
  pubSub.publish(
    'playerEvents',
    makeEvent('TRANSPORT', revision, makeTransport(revision % 2 === 0, revision * 500, 70))
  )
  pubSub.publish('stringEvents', JSON.stringify({tick: revision}))
}
setInterval(tick, 300)

export default new Pylon({
  graphql: {
    Query: {
      player: (): PlayerEvent =>
        makeEvent('STATE', 1, makeTransport(false, 0, 70)),
      hello: () => 'world'
    },
    Mutation: {
      play: (): PlayerEvent => {
        const ev = makeEvent('TRANSPORT', ++revision, makeTransport(true, 0, 70))
        pubSub.publish('playerEvents', ev)
        return ev
      }
    },
    Subscription: {
      playerEvents: () => pubSub.subscribe('playerEvents'),
      stringEvents: () => pubSub.subscribe('stringEvents')
    }
  }
})
