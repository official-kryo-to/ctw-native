# Contributing

Small, understood changes are welcome. For a larger feature, open an issue first so the scope can be agreed.

## Working on the port

- Confirm behavior from evidence and say what is an observation and what is an assumption.
- Write maintainable C++ that fits the surrounding code. Keep changes focused.
- Do not submit original binaries, decompiler output, extracted tables, game assets or recordings of them.
- Use your own legally obtained Android 4.4.243 APK for local testing.
- AI assistance is fine. Understand, verify and be able to maintain what you submit, and mention meaningful use in the PR.
- Add a test when it catches a real regression.

## Layout

| Path | Purpose |
| --- | --- |
| `port/src/` | Shared rendering, asset readers, audio and world code (the engine) |
| `port/game/` | Player, vehicles, traffic, cameras and the plugin host |
| `port/showcase/` | Separate asset and world viewer |
| `ctw-modkit/` | Public mod interfaces, the mod menu and example mods |
| `scripts/` | APK setup and the standalone export |
| `porter/` | The Porter: the downloadable exe that makes the game from an APK |
| `tests/` | Tests using synthetic data |

`input/`, `port/data/`, `analysis/`, `tools/`, `dist/` and `portlogs/` are local and ignored. Do not force-add them.

## Checking a change

```text
cmake -S . -B port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build
ctest --test-dir port/build --output-on-failure
python -m unittest discover -s tests -v
```

For visual changes, test the game and the showcase and save screenshots locally:

```text
port/build/GTACTW.exe --data port/data --shot port/build/check.bmp --frames 60
port/build/ctw_showcase.exe --data port/data --world -1000 -1590 80 --pitch -55 --shot port/build/showcase.bmp
```

## Progress

The [progress map](https://official-kryo-to.github.io/ctw-native/progress.html) counts the original routines the
port has ported, weighted by code size. A routine counts once the source names it as `Class::Method`, so name every
routine you port that way in its comment, for example `// cMuzzleFlashes::Add`. Name a routine only when its
behaviour is ported, not when you merely call or mention it.

`progress/reference.json` lists the original routines: class names, code sizes and a short hash of each
`Class::Method` name, with no code and no readable method names. It only changes when the classification in
`scripts/progress.py` changes; rebuild it from your own APK with `python scripts/progress.py --index`.

```text
python scripts/progress.py port/build/site            # badges, progress.svg, progress.html
python scripts/progress_history.py port/build/site    # history.svg and history.html
```

On every push to main, the Progress workflow publishes both to GitHub Pages. Each pull request gets a comment
with its change in the numbers.

## The Porter

`porter/core` is the Rust port of `scripts/setup_game.py`, `apk_manifest.py`, `extract_tables.py` and
`export_pc.py`. A change to one side needs the same change on the other; `porter/core/tests/parity.sh` checks that
both read the same tables from a synthetic game binary. See [porter/README.md](porter/README.md).

## Plugin and mod interfaces

`port/game/ctw_plugin.h` and `ctw-modkit/include/ctw_plugin.h` must stay identical. Interfaces only grow: add new
entries at the end of `CtwHostApi` / `CtwApi`, and check `version` and `size` before using them.

## Pull requests

Explain the problem, the resulting behavior and the evidence. Include the commands you ran and their results.
Reviews look at behavior and maintainability, not at whether a person or a tool typed the code.
