# Chinatown Wars PC

A native C++ port of GTA: Chinatown Wars for Android **4.4.243**, built with SDL2 and OpenGL.

Bring your own legally obtained copy. Game files are prepared locally and never included in this repository.
This is an independent project, not affiliated with Rockstar Games or Take-Two.

## Build and run

On Windows, install Python 3.10+, Git, CMake 3.20+, Ninja and an x64 MinGW-w64 C/C++ compiler.
Use PowerShell with those tools on your PATH.

```powershell
python -m pip install -r requirements-setup.txt
python scripts/setup_game.py
cmake -S port -B port/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build port/build
.\port\build\GTACTW.exe --data port/data
```

Setup asks for your APK, checks its version and explains each step. It does not download the game.
The separate showcase is `port/build/ctw_showcase.exe`.

[Current state](STATUS.md) | [Contributing](CONTRIBUTING.md) | [Mod kit](ctw-modkit/README.md)

Community: [Discord](https://discord.gg/ZXTvWB7gnb)
