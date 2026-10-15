<p align="center"><img src="porter/public/art/logo.webp" width="220" alt="CTW-Native, the PC port"></p>

# CTW-Native

GTA: Chinatown Wars as a native Windows game, rebuilt in C++ with SDL2 and OpenGL. It runs from the game files of
your own Android **4.4.243** copy; none are included here. A fan project, not affiliated with Rockstar Games or
Take-Two. [STATUS.md](STATUS.md) says what works.

## Status

[![original code ported](https://official-kryo-to.github.io/ctw-native/badge-overall.svg)](https://official-kryo-to.github.io/ctw-native/progress.html) [![routines](https://official-kryo-to.github.io/ctw-native/badge-routines.svg)](https://official-kryo-to.github.io/ctw-native/progress.html)

[![progress map](https://official-kryo-to.github.io/ctw-native/progress.svg)](https://official-kryo-to.github.io/ctw-native/progress.html)

[Progress history](https://official-kryo-to.github.io/ctw-native/history.html)

<sub>Share of the original Android game's code, by size, whose routines the port names as ported. Green is the ported
part of each class. Updated on every push to main; see [how it is measured](CONTRIBUTING.md#progress).</sub>

## Play it

1. Download **CTW-Native-Porter.exe** from [Releases](../../releases/latest).
2. Start it, drop your GTA: Chinatown Wars APK (Android 4.4.243) on it and press **Port it**.
3. Press **Play**, or use the shortcut on your desktop.

The Porter writes a folder with the game in it:

```text
GTA Chinatown Wars PC/
  GTACTW.exe      the game
  data/           the game files from your APK
  mods/           the mod menu (F4) and the example mods
```

The folder can be moved anywhere. Windows 10 or 11, 64-bit.

See [CONTROLS.md](CONTROLS.md) for the keys.

## Mods

Press **F4** in the game for the mod menu. A mod can contain texture changes, a C or C++ DLL, or both in one folder.
The bundled **Enhancements** mod adds a third-person camera (**V**, then the mouse) and PSP-style lighting. See the [modding guide](ctw-modkit/README.md).

## Build from source

### Windows

Windows x64 with Python 3.10+, Git, CMake 3.20+, Ninja and a MinGW-w64 toolchain (GCC or LLVM-MinGW) on your PATH.
MSVC is not supported because of the game's fixed-point arithmetic.

```text
python -m pip install -r requirements-setup.txt
python scripts/setup_game.py                    # your APK from input/ into port/data
cmake -S . -B port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build
port/build/GTACTW.exe --data port/data
```

The build also makes `ctw_showcase.exe` (an asset and world viewer) and the mod kit in `port/build/mods/`.
For optional 3D people in third person, place your own **LCS Android 2.4.379** APK at
`input/lcs/GTA-LCS-2.4.379.apk` before configuring. Those same CMake commands prepare its character assets for
Enhancements; press **V** in the game. Incremental builds reuse the import and repair missing files.
`-DCTW_LCS_APK=PATH` selects another local donor; `-DCTW_IMPORT_LCS=OFF` stops automatic import.
This development step is disabled for `CTW_RELEASE=ON`; standalone exports and the Porter use explicit imports.
### Linux

Any recent x86-64 or arm64 distribution with GCC 11+ (or Clang 14+), CMake 3.20+, Ninja, Python 3.10+ and the
development packages SDL2 builds against. On Debian or Ubuntu:

```text
sudo apt install build-essential cmake ninja-build python3-pip libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxfixes-dev libxi-dev libxss-dev libxkbcommon-dev libwayland-dev wayland-protocols libgl1-mesa-dev \
    libegl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
python3 -m pip install -r requirements-setup.txt
python3 scripts/setup_game.py
cmake -S . -B port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build
port/build/GTACTW --data port/data --mods port/build/mods
```

The mod menu and the example mods build as `.so` files; a mod's `mod.ini` keeps naming `x.dll`, and the menu loads
`x.so` in its place. CI builds and tests every push on Ubuntu as well as on Windows.

Building needs no game files; running does. `python scripts/export_pc.py` writes the same standalone folder as the
Porter from a local build (`--with-modkit` adds the mod menu and the examples). The Porter itself is in
[porter/](porter/README.md).

## Tests

```text
ctest --test-dir port/build --output-on-failure
python -m unittest discover -s tests -v
```

## License

Project code is licensed under **GPL-3.0-only**, see [LICENSE](LICENSE); `GTACTW.exe --license` prints the notice.
Third-party components keep their own licenses, see [THIRD_PARTY.md](THIRD_PARTY.md). The license does not cover
the original game's code, assets or trademarks.

[Contributing](CONTRIBUTING.md) | [Mod kit](ctw-modkit/README.md) | [Porter](porter/README.md) | [Discord](https://discord.gg/ZXTvWB7gnb)
