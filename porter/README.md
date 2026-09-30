# CTW-Native Porter

The exe people download to play: it turns their own GTA: Chinatown Wars APK (Android 4.4.243) into the PC game.
Drop the APK on it, press **Port it**, press **Play**. The game itself (`GTACTW.exe`, the mod menu and the example
mods) is inside the Porter; the game files come from the APK. Nothing is downloaded.

Built like Kryoto Desktop and Kryoto Forge: Tauri 2, React and Kryoto's design tokens, type and ASCII art.

## Run it

```text
pnpm install
pnpm dev          # the window alone in a browser, with a stand-in for every step (http://localhost:1422)
pnpm app:dev      # the app
pnpm app:build    # the exe: target/release/ctw-native-porter.exe
```

`app:build` packs the game from `port/build-release` (what `scripts/export_pc.py` builds; set `CTW_GAME_BUILD` to use
another folder). Without a game build the Porter still builds, and says it has no game inside.

## Where things are

| | |
| --- | --- |
| `core/` | The work: checking the APK, reading its tables, extracting, installing. A Rust port of `scripts/setup_game.py`, `apk_manifest.py`, `extract_tables.py` and `export_pc.py` |
| `core/tests/parity.sh` | Checks that the Rust table extraction gives the same bytes as the Python one |
| `src-tauri/` | The window, the commands, and `build.rs`, which packs the game in |
| `src/` | The screen. `src/ui` is copied from Kryoto Desktop |
| `public/art/` | The banner and the CTW-Native logo |
| `public/fonts/` | Teletext, Kryoto's typeface (see THIRD_PARTY.md) |

## Tests

```text
cargo test -p ctw-porter-core
cargo clippy --workspace --all-targets -- -D warnings
core/tests/parity.sh          # needs g++-aarch64-linux-gnu and requirements-setup.txt
pnpm build                    # typecheck and build the screen
```

## Releases

The version is in `package.json`, `src-tauri/Cargo.toml` and `src-tauri/tauri.conf.json`. To release:

```text
pnpm version:set 0.2.0      # sets all three
```

then add the `## 0.2.0` entry to [CHANGELOG.md](CHANGELOG.md) and push to main. `.github/workflows/release.yml` sees
the new version, builds the game and the Porter on Windows and publishes `CTW-Native-Porter.exe` as release
`v0.2.0` with the changelog entry as its notes. Pushing without a version change releases nothing.

There are no automatic updates: the window shows its version, and a new one is downloaded from Releases.
