// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// Build the current game before embedding it. CI can supply its own freshly built CTW_GAME_BUILD.
import { existsSync, readFileSync } from 'node:fs'
import { join, resolve } from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath, pathToFileURL } from 'node:url'

const root = fileURLToPath(new URL('../../', import.meta.url))

function runCommand(command, args) {
  console.log('>', command, ...args)
  const result = spawnSync(command, args, { cwd: root, stdio: 'inherit' })
  if (result.error) throw result.error
  if (result.status !== 0) throw new Error(`${command} failed (${result.status ?? result.signal})`)
}

export function prepareGame({ gameBuild = process.env.CTW_GAME_BUILD, run = runCommand } = {}) {
  const build = gameBuild ? resolve(gameBuild) : join(root, 'port', 'build-release')
  if (!gameBuild) {
    const version = JSON.parse(readFileSync(join(root, 'porter', 'package.json'), 'utf8')).version
    run('cmake', ['-S', root, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
      '-DCTW_RELEASE=ON', '-DCTW_BUILD_MODKIT=ON', '-DBUILD_TESTING=OFF', `-DCTW_VERSION=${version}`])
    // Always run the incremental build, even if an older GTACTW.exe is already present.
    run('cmake', ['--build', build])
  }
  for (const name of ['GTACTW.exe', 'mods/ModMenu.dll', 'mods/CheatExample/cheat_example.dll']) {
    if (!existsSync(join(build, name))) throw new Error(`Missing ${name} in ${build}. Build the game and mod kit first.`)
  }
  console.log(`Embedding the game from ${build}`)
  return build
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  try {
    prepareGame()
  } catch (error) {
    console.error(error.message)
    process.exitCode = 1
  }
}
