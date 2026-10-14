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

`app:build` first configures and incrementally rebuilds the current game and mod kit in `port/build-release`,
then packs them into the Porter. This needs CMake, Ninja and MinGW on your build machine. Set `CTW_GAME_BUILD`
to use a game and mod kit you have already built (as CI does); an incomplete payload stops the build.
Development builds can run without a game payload; Windows release builds require one.

The PC game code is embedded at build time. At runtime, the Porter installs that embedded game and extracts
the assets and lookup tables from your APK. Changing the game source therefore needs a new Porter build;
an existing downloaded Porter keeps the game from its release. Already exported game folders keep their
own copy too. Use a newer Porter and export to a new folder to get the newer game.

## Optional mods and 3D people

With mods enabled, the Porter shows a premade-mod checklist. Enhancements is selected by default; the cheat
and appearance examples are optional. Mods can combine code and textures in the same folder.

Enhancements offers an optional extra APK import for third-person 3D people. Choose your own **Liberty City
Stories for Android 2.4.379** APK; the screen states this version and validates it automatically. You can skip
and keep the camera/lighting features with CTW sprites. Imported appearances include Toni as the player,
a cop and twelve civilians; see [character support and limitations](../ctw-modkit/THIRD_PERSON_ASSETS.md).

The importer copies only selected models, required textures and pedestrian/bike animations into
`mods/Enhancements/lcs/`. It never runs donor code or packages a donor APK/library. Building the Porter excludes
local imports and user settings. Do not share an exported game folder containing game resources.

The standalone Python exporter supports the same selection:

```text
python scripts/export_pc.py --apk your-ctw.apk --out your-game --mods Enhancements --lcs-apk your-lcs.apk
python porter/core/tests/lcs_parity.py              # synthetic importer byte parity
python porter/core/tests/lcs_parity.py your-lcs.apk # optional private-copy parity check
```

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
