# CTW Online

The public game list and relay behind the Multiplayer mod's **Online** tab. With it, players host and join games
over the internet without opening ports: a host lists its game (publicly, or privately with a six-letter code) and
every player's messages go through the relay to the host and back.

It is one Cloudflare Worker with two Durable Objects (`Lobby` for the list, one `Room` per game for the relay),
so there is no server to run or update. Cloudflare's free plan is enough to start with; see the limits below.

The mod uses the project's own service, `https://ctw-online.kryotooooo.workers.dev`, unless told otherwise. Anyone
can run their own (a private list for a group, or more room than the free plan gives): deploy it as below and point
the players' mods at it. Games on different services do not see each other.

## Deploy your own

You need a free Cloudflare account and Node.js 18 or newer. Nothing in the project is tied to the project's own
account: there are no secrets, account IDs or routes to edit. The first deploy asks you to pick a `workers.dev`
subdomain if your account has none.

```text
cd online
npm install
npx wrangler login          # once, in a browser
npx wrangler deploy
```

Wrangler prints the address, for example `https://ctw-online.<your-account>.workers.dev`. Every player then adds
a line to their `mods/Multiplayer/multiplayer.ini` (the file appears after the game has run once with the mod):

```text
online=https://ctw-online.<your-account>.workers.dev
```

or starts the game with the environment variable `CTW_ONLINE_URL` set to it. A build of the mod for everyone can
change `ONLINE_DEFAULT_URL` in `ctw-modkit/examples/multiplayer/multiplayer.c` instead. `online=` with nothing after
it turns CTW Online off (Nearby and Address still work).

## Try it locally

```text
cd online
npm install
npx wrangler dev --port 8787              # the service on this computer
CTW_ONLINE_URL=http://127.0.0.1:8787 npm test
```

A game uses a local service with `CTW_ONLINE_URL=http://127.0.0.1:8787` set before it starts.

## How it works

| Request | What it does |
| --- | --- |
| `GET /v1/servers` | the public games heard from in the last minute |
| `POST /v1/servers` | a host lists a game: `{name, players, max, version, public}` gives `{id, token}` |
| `POST /v1/servers/<id>` | the host's heartbeat (every 20 seconds): `{token, players, name}` |
| `DELETE /v1/servers/<id>?token=` | the host stops |
| `GET /v1/rooms/<id>?token=` | the host's WebSocket to the relay |
| `GET /v1/rooms/<id>` | a player's WebSocket |

Relay frames are binary and carry the game's own messages unchanged: a player's message reaches the host as
`[u16 player][message]`; the host sends `[u16 player][message]` to one player or `[0xFFFF][message]` to all. An
empty message from the relay tells the host a player has gone.

Rooms default to 16 players and allow a host-selected limit from 2 to 32, including the host. The registration's
`max` is the room's capacity for its lifetime; heartbeats preserve it. The relay refuses connections when that
capacity is reached, and the mod also enforces it across direct, LAN and relay players together.

## Abuse limits

The service has no accounts, so it limits by the client's address (`CF-Connecting-IP`). The constants are at the
top of `src/index.js`; raise them if many players share one address.

| Limit | Value | When exceeded |
| --- | --- | --- |
| Games one address keeps listed | 5 | listing is refused with 429 |
| New games one address lists | 20 a minute | 429 |
| Lookups of unknown game codes | 30 a minute | 429, which stops code guessing |
| Frames a relay player sends | 200 a second | the excess is dropped |
| Frame size | 2048 bytes | dropped |
| Room size | 2 to 32 including the host | refused |

Counters are kept in memory and start again if Cloudflare restarts the list object, so they are a courtesy and not a
guarantee. A host reports its own player count, so the list is not proof of who is playing. A private game is
reachable by anyone who knows its six-letter code.

Many games run at once: each one is its own relay room, and a service has one list. They all share the Cloudflare
account's quota (below), so on the project's service one heavy user can use up what everyone else has. Run your
own if that matters.

## Limits

Every message a player sends through the relay counts towards the account's Durable Object requests (WebSocket
messages count as one twentieth of a request each). A two-player game sends about 30 messages a second, so the
free plan carries roughly a dozen hours of two-player play a day across all players, or fewer hours with more
players. Cloudflare's paid Workers plan ($5 a month) raises that far beyond what a fan mod needs. Games on the same
network (the **Nearby** tab) and direct joins by address never use the service.
