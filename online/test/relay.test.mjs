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
