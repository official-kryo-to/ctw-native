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
| `ctw-modkit/` | Public mod interfaces, the mod menu (built into the game) and example mods |
| `scripts/` | APK setup and the standalone export |
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

## Plugin and mod interfaces

`port/game/ctw_plugin.h` and `ctw-modkit/include/ctw_plugin.h` must stay identical. Interfaces only grow: add new
entries at the end of `CtwHostApi` / `CtwApi`, and check `version` and `size` before using them.

## Pull requests

Explain the problem, the resulting behavior and the evidence. Include the commands you ran and their results.
Reviews look at behavior and maintainability, not at whether a person or a tool typed the code.
