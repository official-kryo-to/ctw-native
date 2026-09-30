# Contributing

Small, understood changes are welcome. For a substantial feature, open an issue first to agree on its scope.

## AI assistance

AI use is allowed. Use it as a tool, not as a slave or a substitute for your own judgment.
You must understand what it produces, check its assumptions, and be able to explain and maintain the result.

Vibe coding is not allowed. Pull requests containing AI slop will be denied. That includes code the author cannot
explain, invented behavior presented as verified, unnecessary abstractions, filler documentation, and unchecked
generated changes. Disclose meaningful AI assistance in the PR and describe how you verified it.

## Working on the port

- Confirm behavior from evidence. Separate observations from assumptions in your PR.
- Write maintainable C++ that fits the surrounding code. Keep changes focused.
- Do not submit decompiler output, original binaries, extracted tables, game assets or recordings of them.
- Use your own legally obtained Android 4.4.243 APK for local testing. Do not share game download links or bypass tools.
- Add tests where they catch a real regression. State what you tested and any remaining uncertainty.

The repository does not claim clean-room provenance. Keep private analysis outside the public source tree.
Third-party code retains its own notices; see [THIRD_PARTY.md](THIRD_PARTY.md).

## Layout

| Path | Purpose |
| --- | --- |
| `port/src/` | Shared rendering, asset readers, audio and world code |
| `port/game/` | Player, vehicles, traffic, cameras and plugin host |
| `port/showcase/` | Separate asset and world viewer |
| `ctw-modkit/` | Mod menu, public interfaces and examples |
| `scripts/` | Local APK setup and public-file checks |
| `tests/` | Tests using synthetic data |

`input/`, `port/data/`, `analysis/`, `tools/`, `dist/` and `portlogs/` are private and ignored.
Do not force-add them. Preserve the owner's local progress logs.

## Verification

```powershell
python -m unittest discover -s tests -v
python scripts/check_public_files.py
cmake --build port/build
cmake -S ctw-modkit -B ctw-modkit/build -G Ninja
cmake --build ctw-modkit/build
```

Building does not need game data. Running the game does. CMake fetches SDL2 on first configuration.
The game currently uses MinGW-specific arithmetic; use the Windows toolchain documented in README.md.

For visual changes, test the game and showcase. Save screenshots locally with:

```powershell
.\port\build\GTACTW.exe --data port/data --shot port/build/check.bmp --frames 60
```

Describe the result in the PR. Do not attach game assets or private analysis.
Keep `port/game/ctw_plugin.h` and `ctw-modkit/include/ctw_plugin.h` identical when changing the plugin interface.
Extend public API tables at the end and check version and size before accessing new entries.

## Pull requests

Explain the problem, resulting behavior and evidence behind the change. Include exact test commands and results.
A PR must build, pass the public-file check and remain small enough to review. Reviews assess behavior and
maintainability, not whether the code was typed by a person or suggested by a tool.
