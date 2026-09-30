// The Porter's version lives in three files: package.json (shown in the window), src-tauri/Cargo.toml (the exe's
// file version) and src-tauri/tauri.conf.json (what the release workflow tags). They must agree.
// `node scripts/check-version.mjs 0.2.0` sets all three instead.
import { readFileSync, writeFileSync } from 'node:fs'

const files = {
  package: new URL('../package.json', import.meta.url),
  conf: new URL('../src-tauri/tauri.conf.json', import.meta.url),
  cargo: new URL('../src-tauri/Cargo.toml', import.meta.url),
}
const cargoVersion = /^version\s*=\s*"([^"]+)"/m
const next = process.argv[2]

if (next) {
  if (!/^\d+\.\d+\.\d+$/.test(next)) {
    console.error(`Not a version: ${next} (use MAJOR.MINOR.PATCH)`)
    process.exit(1)
  }
  for (const key of ['package', 'conf']) {
    const text = readFileSync(files[key], 'utf8')
    writeFileSync(files[key], text.replace(/"version":\s*"[^"]+"/, `"version": "${next}"`))
  }
  writeFileSync(files.cargo, readFileSync(files.cargo, 'utf8').replace(cargoVersion, `version = "${next}"`))
  console.log(`Porter version set to ${next}. Add its CHANGELOG.md entry, then push to main to release it.`)
}

const pkg = JSON.parse(readFileSync(files.package, 'utf8')).version
const conf = JSON.parse(readFileSync(files.conf, 'utf8')).version
const cargo = readFileSync(files.cargo, 'utf8').match(cargoVersion)?.[1]
if (pkg !== conf || conf !== cargo) {
  console.error(`::error::Versions differ: package.json ${pkg}, tauri.conf.json ${conf}, Cargo.toml ${cargo}. Bump all three.`)
  process.exit(1)
}
const changelog = readFileSync(new URL('../CHANGELOG.md', import.meta.url), 'utf8')
if (!new RegExp(`^## \\[?${conf.replaceAll('.', '\\.')}\\]?`, 'm').test(changelog)) {
  console.error(`::error::CHANGELOG.md has no entry for ${conf}.`)
  process.exit(1)
}
console.log(`Porter ${conf}: versions agree.`)
