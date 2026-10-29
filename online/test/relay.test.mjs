// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//
// Exercises a running CTW Online (`npx wrangler dev`, or a deployed one): CTW_ONLINE_URL=http://127.0.0.1:8787
import { test } from 'node:test'
import assert from 'node:assert/strict'

const base = process.env.CTW_ONLINE_URL
const ws = (path) => base.replace(/^http/, 'ws') + path

function open(url) {
  return new Promise((resolve, reject) => {
    const socket = new WebSocket(url)
    socket.binaryType = 'arraybuffer'
    const queue = [], waiting = []
    socket.addEventListener('message', (e) => {
      const data = new Uint8Array(e.data)
      if (waiting.length) waiting.shift()(data)
      else queue.push(data)
    })
    socket.next = () => (queue.length ? Promise.resolve(queue.shift()) : new Promise((r) => waiting.push(r)))
    socket.addEventListener('open', () => resolve(socket))
    socket.addEventListener('error', () => reject(new Error('could not connect to ' + url)))
  })
}

test('a public game is listed, relayed and removed', { skip: !base }, async () => {
  const created = await (await fetch(base + '/v1/servers', {
    method: 'POST', body: JSON.stringify({ name: 'Relay test\u0007', players: 1, max: 4, version: '1' }),
  })).json()
  assert.match(created.id, /^[A-Z2-9]{6}$/)
  let list = await (await fetch(base + '/v1/servers')).json()
  assert.ok(list.some((s) => s.id === created.id && s.name === 'Relay test'), 'listed, without control characters')

  const host = await open(ws(`/v1/rooms/${created.id}?token=${created.token}`))
  const a = await open(ws(`/v1/rooms/${created.id.toLowerCase()}`))
  const b = await open(ws(`/v1/rooms/${created.id}`))
  a.send(new Uint8Array([7, 8, 9]))
  assert.deepEqual([...await host.next()], [1, 0, 7, 8, 9], 'a player\'s datagram reaches the host tagged with its number')
  b.send(new Uint8Array([5]))
  assert.deepEqual([...await host.next()], [2, 0, 5])
  host.send(new Uint8Array([2, 0, 42]))
  assert.deepEqual([...await b.next()], [42], 'the host can answer one player')
  host.send(new Uint8Array([0xff, 0xff, 1, 2]))
  assert.deepEqual([...await a.next()], [1, 2], 'and everyone')
  assert.deepEqual([...await b.next()], [1, 2])
  a.close()
  assert.deepEqual([...await host.next()], [1, 0], 'the host hears when a player goes')

  assert.equal((await fetch(base + `/v1/servers/${created.id}`, {
    method: 'POST', body: JSON.stringify({ token: 'wrong', players: 2 }) })).status, 404, 'only the host keeps it listed')
  assert.equal((await fetch(base + `/v1/servers/${created.id}`, {
    method: 'POST', body: JSON.stringify({ token: created.token, players: 2, name: 'Relay test' }) })).status, 200)
  await assert.rejects(open(ws('/v1/rooms/NOPE00')), 'no relay for a game that does not exist')

  const closed = new Promise((r) => b.addEventListener('close', r))
  host.close()
  await closed
  await fetch(base + `/v1/servers/${created.id}?token=${created.token}`, { method: 'DELETE' })
  list = await (await fetch(base + '/v1/servers')).json()
  assert.ok(!list.some((s) => s.id === created.id), 'a game that stopped is no longer listed')
})

test('the configured room limit includes its host and cannot be bypassed in the join URL', { skip: !base, timeout: 15_000 }, async () => {
  const created = await (await fetch(base + '/v1/servers', {
    method: 'POST', body: JSON.stringify({ name: 'Two-player room', players: 1, max: 2 }),
  })).json()
  let host, player
  try {
    host = await open(ws(`/v1/rooms/${created.id}?token=${created.token}`))
    player = await open(ws(`/v1/rooms/${created.id}`))
    await assert.rejects(open(ws(`/v1/rooms/${created.id}?role=host&max=32`)), 'two means host plus one player')
    assert.equal((await fetch(base + `/v1/servers/${created.id}`, {
      method: 'POST', body: JSON.stringify({ token: created.token, players: 32, name: 'Two-player room' }),
    })).status, 200)
    const list = await (await fetch(base + '/v1/servers')).json()
    const room = list.find((s) => s.id === created.id)
    assert.equal(room.max, 2, 'a heartbeat preserves the registered capacity')
    assert.equal(room.players, 2, 'a heartbeat cannot advertise more players than the room holds')
  } finally {
    player?.close()
    host?.close()
    await fetch(base + `/v1/servers/${created.id}?token=${created.token}`, { method: 'DELETE' })
  }
})

test('rooms allow 32 players with bounded defaults and refuse the next connection', { skip: !base, timeout: 30_000 }, async () => {
  const create = async (max) => (await fetch(base + '/v1/servers', {
    method: 'POST', body: JSON.stringify({ name: 'Capacity test', players: 1, max }),
  })).json()
  const large = await create(999), standard = await create(undefined), small = await create(1)
  let host
  const players = []
  try {
    const list = await (await fetch(base + '/v1/servers')).json()
    assert.equal(list.find((s) => s.id === large.id).max, 32)
    assert.equal(list.find((s) => s.id === standard.id).max, 16)
    assert.equal(list.find((s) => s.id === small.id).max, 2)
    host = await open(ws(`/v1/rooms/${large.id}?token=${large.token}`))
    for (let i = 1; i < 32; ++i) {
      const player = await open(ws(`/v1/rooms/${large.id}`))
      players.push(player)
      player.send(new Uint8Array([i]))
      assert.deepEqual([...await host.next()], [i, 0, i], 'every admitted player gets a distinct relay id')
    }
    await assert.rejects(open(ws(`/v1/rooms/${large.id}`)), '32 is a hard limit including the host')
  } finally {
    for (const player of players) player.close()
    host?.close()
    for (const room of [large, standard, small])
      await fetch(base + `/v1/servers/${room.id}?token=${room.token}`, { method: 'DELETE' })
  }
})

test('one address cannot keep more than a few games listed', { skip: !base }, async () => {
  const made = []
  try {
    let refused
    for (let i = 0; i < 8 && !refused; ++i) {
      const response = await fetch(base + '/v1/servers', { method: 'POST', body: JSON.stringify({ name: 'Cap test', max: 2 }) })
      if (response.status === 429) refused = await response.json()
      else made.push(await response.json())
    }
    assert.ok(refused, 'a sixth game from one address is refused')
    assert.equal(made.length, 5)
  } finally {
    for (const room of made) await fetch(base + `/v1/servers/${room.id}?token=${room.token}`, { method: 'DELETE' })
  }
})
