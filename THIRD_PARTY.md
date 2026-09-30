# Third-party notices

Project-owned files use GPL-3.0-only. The following components retain their upstream licenses;
their notices must accompany distributions. The identifications come from the embedded headers and the pinned dependency.

| Component | Location / upstream | License and notice |
| --- | --- | --- |
| SDL2 2.30.9 | Fetched by `port/CMakeLists.txt` from [libsdl-org/SDL](https://github.com/libsdl-org/SDL/tree/release-2.30.9) | [zlib license](LICENSES/SDL2.txt); release source includes its additional notices |
| glad OpenGL loader | `port/third_party/glad/`, [Dav1dde/glad](https://github.com/Dav1dde/glad) | Generated files identify `(WTFPL OR CC0-1.0) AND Apache-2.0`; this project chooses [CC0-1.0](LICENSES/CC0-1.0.txt) plus [Apache-2.0](LICENSES/Apache-2.0.txt) |
| Khronos platform header | `port/third_party/glad/include/KHR/khrplatform.h` | [Khronos permission notice](LICENSES/Khronos.txt), copyright 2008–2018 The Khronos Group Inc. |
| stb_image | `port/third_party/stb/stb_image.h`, [nothings/stb](https://github.com/nothings/stb) | MIT or public domain; this project chooses the [MIT license](LICENSES/stb.txt), copyright 2017 Sean Barrett |
| minimp3 | `port/third_party/minimp3/`, [lieff/minimp3](https://github.com/lieff/minimp3) | [CC0-1.0](LICENSES/CC0-1.0.txt); retain the dedication and warranty notice embedded in both headers |

Python setup dependencies are installed separately, not copied into releases: Capstone and pyelftools.
Pillow is optional (the exe icon); the format tools in `port/tools/` also use NumPy. Retain their own licenses if you
redistribute those packages. Compiler runtimes retain their toolchain notices and licenses;
check those for the toolchain used when distributing a build.

Original game data is excluded. The project license does not cover Rockstar/Take-Two game files or
trademarks.
