import assert from 'node:assert/strict'
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { tmpdir } from 'node:os'
import { test } from 'node:test'
import { prepareGame } from './build-game.mjs'

test('an explicit game build must include the exe and compiled mods', t => {
  const build = mkdtempSync(join(tmpdir(), 'ctw-payload-'))
  t.after(() => rmSync(build, { recursive: true, force: true }))
  const run = () => assert.fail('An explicit CI build must not be rebuilt')
  assert.throws(() => prepareGame({ gameBuild: build, run }), /Missing GTACTW.exe/)
  writeFileSync(join(build, 'GTACTW.exe'), 'game')
  assert.throws(() => prepareGame({ gameBuild: build, run }), /Missing mods\/ModMenu.dll/)
  mkdirSync(join(build, 'mods', 'CheatExample'), { recursive: true })
  writeFileSync(join(build, 'mods', 'ModMenu.dll'), 'menu')
  assert.throws(() => prepareGame({ gameBuild: build, run }), /Missing mods\/CheatExample\/cheat_example.dll/)
  writeFileSync(join(build, 'mods', 'CheatExample', 'cheat_example.dll'), 'example')
  assert.equal(prepareGame({ gameBuild: build, run }), build)
})

test('the default build runs CMake before accepting any existing payload', () => {
  const calls = []
  assert.throws(() => prepareGame({ gameBuild: '', run: (command, args) => {
    calls.push([command, args])
    if (args[0] === '--build') throw new Error('Build failed')
  } }), /Build failed/)
  assert.equal(calls.length, 2)
  assert.equal(calls[0][0], 'cmake')
  assert.ok(calls[0][1].includes('-DCTW_BUILD_MODKIT=ON'))
  assert.equal(calls[1][1][0], '--build')
})

test('a configure failure stops the build', () => {
  let calls = 0
  assert.throws(() => prepareGame({ gameBuild: '', run: () => {
    calls++
    throw new Error('Configure failed')
  } }), /Configure failed/)
  assert.equal(calls, 1)
})
