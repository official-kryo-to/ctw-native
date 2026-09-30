# Chinatown Wars PC

A native C++ gameplay prototype of GTA: Chinatown Wars for Windows, built with SDL2 and OpenGL.
It runs from the game files of your own Android **4.4.243** copy; none are included in this repository.
This is an independent project, not affiliated with Rockstar Games or Take-Two. See [STATUS.md](STATUS.md)
for what works.

## Export a standalone PC game

Put your APK in `input/` and run:

```text
python -m pip install -r requirements-setup.txt
python scripts/export_pc.py
```

This builds the game in release mode (first time only) and writes a clean folder:

```text
dist/GTA-Chinatown-Wars-PC/
  GTACTW.exe      the game
  data/           the game files from your APK, as plain files
  mods/           empty: put mods here
```

The folder can be copied anywhere. Options: `--apk PATH`, `--out DIR`, `--rebuild`, and `--with-modkit` to also put
the mod menu (`ModMenu.dll`, F4) and the example mods into `mods/`. Modders: see the
[modding guide](ctw-modkit/README.md); `GTACTW.exe --export-textures DIR` writes every texture as a PNG.

## Build from source

Windows x64 with Python 3.10+, Git, CMake 3.20+, Ninja and a MinGW-w64 toolchain (GCC or LLVM-MinGW) on
your PATH. MSVC is not supported because of the game's fixed-point arithmetic.

```text
python scripts/setup_game.py                    # extracts your APK into port/data
cmake -S . -B port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build
port/build/GTACTW.exe --data port/data
```

The build also produces `ctw_showcase.exe` (an asset and world viewer) and the [mod kit](ctw-modkit/README.md)
(`ModMenu.dll` and the example mods) in `port/build/mods/`. Press **F4** in the game for the mod menu.
`-DCTW_BUILD_MODKIT=OFF` builds the game and showcase only.
Building needs no game files; running does. The first configure downloads SDL2.

## Tests

```text
ctest --test-dir port/build --output-on-failure
python -m unittest discover -s tests -v
```

## License

Project code is licensed under **GPL-3.0-only**, see [LICENSE](LICENSE); `GTACTW.exe --license` prints the notice.
Third-party components keep their own licenses, see [THIRD_PARTY.md](THIRD_PARTY.md). The license does not cover
the original game's code, assets or trademarks.

[Contributing](CONTRIBUTING.md) | [Mod kit](ctw-modkit/README.md) | [Discord](https://discord.gg/ZXTvWB7gnb)
