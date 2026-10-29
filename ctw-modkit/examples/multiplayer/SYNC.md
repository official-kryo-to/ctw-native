# Shared-world synchronization

The host owns city entities. Player cash, health, armour and inventories belong to each individual player.

| Feature | Current behavior |
| --- | --- |
| Cars and seats | Stable host IDs, driver motion, passenger seats, damage/destruction and vehicle presentation are replicated. |
| Drag/skid lines | The producer sends its accepted contact points and strip IDs. A bounded ordered channel retries missing batches; observers replay geometry instead of deriving a different trail from wheel-slip flags. |
| NPC drops and pickups | The host creates money and weapon drops. Everyone receives the same pickup IDs. Collection consumes the pickup globally, and a retryable, deduplicated grant rewards only its collector. |
| NPC weapons | Snapshots carry the active weapon and firing state. Host NPC bullet/melee damage to remote player puppets is sent separately from PvP damage. |
| Time and weather | Joiners can use the host's time and weather. |
| What players see | Each state carries the sender's view. The host's on-screen tests (traffic going on rails out of sight, where cars and people appear, what is removed) count every player's view, so nothing behaves as unseen in front of a joiner. |
| Other players' cars on the host | Carried forward from the newest state (not played back 0.12 s late), so the host's cars are hit where the joiner hits them. Clients still show host cars 0.12 s late, between snapshots. |

Protocol v5 and the v16 game/mod API are required on every peer. Shared capacity is bounded: 32 players, 256 visible
skid strips and 256 active pickups. Pickup delivery remembers up to 4,096 pickup identities per session; unknown
identities are declined when that budget is exhausted rather than risking repeated rewards.

## Remaining gaps found in the audit

- Police pursuit and arrest decisions still use the host's player and wanted level. Sharing NPC rendering and
  damage does not provide independent wanted/pursuit behavior for each player.
- Dynamic street furniture and other props are not replicated entities. Explosion-driven prop physics runs on
  the host, while peers can still simulate local impacts.
- Projectile trajectories are simulated locally; shared player firing and explosion events do not replicate
  each projectile's identity and flight.
- Fire propagation, apart from the client suppression of damage from cosmetic burnout flames, is not a shared
  authoritative fire system.
- Mission progress and scripted gameplay remain local.

Regression coverage lives in `tests/multiplayer_world_test.cpp`, `tests/multiplayer_session_test.c`,
`tests/multiplayer_skid_session_test.c`, `tests/multiplayer_pickup_session_test.c` and `tests/npc_weapons_test.cpp`.
