cmake_minimum_required(VERSION 3.20)
project(ctw_port CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../third_party/SDL2/CMakeLists.txt" AND NOT FETCHCONTENT_SOURCE_DIR_SDL2)
    # Corresponding-source release archives include the exact SDL2 source used by the build.
    set(FETCHCONTENT_SOURCE_DIR_SDL2 "${CMAKE_CURRENT_LIST_DIR}/../third_party/SDL2" CACHE PATH "Local SDL2 source")
endif()
FetchContent_Declare(SDL2 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git GIT_TAG release-2.30.9 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL2)

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad PUBLIC third_party/glad/include)

# ---- engine: everything shared by the showcase and the game (src/) ----------------------------------------
add_library(ctw_engine STATIC
    src/os/os_sdl.cpp src/os/wad.cpp src/os/gamefs.cpp src/os/dxtbin.cpp src/os/pak.cpp src/os/datafile.cpp
    src/gfx/spriteset.cpp src/gfx/font.cpp src/gfx/model.cpp src/gfx/texload.cpp src/gfx/assets.cpp src/gfx/stb_impl.cpp src/gfx/pedsprites.cpp src/gfx/rendertables.cpp
    src/text/gxt.cpp src/audio/audio.cpp
    src/world/worldmap.cpp src/world/timecycle.cpp src/world/worldrenderer.cpp src/world/collision.cpp src/world/collisionsweep.cpp src/world/props.cpp)
target_include_directories(ctw_engine PUBLIC src third_party/stb third_party/minimp3)
target_link_libraries(ctw_engine PUBLIC glad SDL2::SDL2-static)

# ---- showcase: the asset viewer (textures, text, models, audio, fly-through city) ------------------------
add_executable(ctw_showcase showcase/main.cpp showcase/viewer.cpp showcase/modelview.cpp showcase/worldview.cpp)
target_link_libraries(ctw_showcase PRIVATE ctw_engine SDL2::SDL2main)

# ---- the game ----------------------------------------------------------------------------------------------
# GTACTW.exe. Version info (company kryo.to Inc) + the icon, which tools/make_icon.py makes from your own APK
# (data/gtactw.ico). Standalone exports (scripts/export_pc.py) put the icon into the exe themselves.
# -DCTW_RELEASE=ON: no console window (scripts/export_pc.py wants a release build).
option(CTW_RELEASE "release build: windowed, no console" OFF)
set(CTW_VERSION 0.1.0 CACHE STRING "Version written into GTACTW.exe (releases pass the Porter's version)")
set(CTW_ICON_LINE "")
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/data/gtactw.ico)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/data/gtactw.ico ${CMAKE_BINARY_DIR}/gtactw.ico COPYONLY)
    set(CTW_ICON_LINE "1 ICON \"gtactw.ico\"")
endif()
string(REPLACE "." "," CTW_VERSION_COMMAS ${CTW_VERSION})
configure_file(game/res/gtactw.rc.in ${CMAKE_BINARY_DIR}/gtactw.rc @ONLY)
set(CTW_RES "")
if(MINGW)   # windres run from the build folder with relative names (it breaks on include paths with spaces)
    find_program(CTW_WINDRES windres REQUIRED)
    add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/gtactw_res.o
        COMMAND ${CTW_WINDRES} -O coff gtactw.rc gtactw_res.o
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        DEPENDS ${CMAKE_BINARY_DIR}/gtactw.rc)
    set(CTW_RES ${CMAKE_BINARY_DIR}/gtactw_res.o)
endif()
add_library(ctw_gameplay STATIC game/game.cpp game/followcam.cpp game/player.cpp game/hud.cpp game/plugins.cpp game/vehicle.cpp game/carcam.cpp game/cargens.cpp game/roads.cpp game/aispline.cpp game/traffic.cpp game/particles.cpp game/sound.cpp game/skidmarks.cpp game/lookups.cpp game/watercannon.cpp game/propdynamics.cpp game/rigidbody.cpp game/radio.cpp)
target_include_directories(ctw_gameplay PUBLIC game)
target_link_libraries(ctw_gameplay PUBLIC ctw_engine)
add_executable(ctw_game game/main.cpp ${CTW_RES})
set_target_properties(ctw_game PROPERTIES OUTPUT_NAME GTACTW)
target_include_directories(ctw_game PRIVATE game)
target_link_libraries(ctw_game PRIVATE ctw_gameplay SDL2::SDL2main)
if(MINGW)
    target_link_options(ctw_game PRIVATE -static -static-libgcc -static-libstdc++)
    if(CTW_RELEASE)
        target_link_options(ctw_game PRIVATE -mwindows)
    endif()
endif()

# ---- tools --------------------------------------------------------------------------------------------------
add_executable(wadtest tools/wadtest.cpp src/os/wad.cpp src/os/datafile.cpp)
target_include_directories(wadtest PRIVATE src)
add_executable(audiotest tools/audiotest.cpp)
target_include_directories(audiotest PRIVATE third_party/minimp3)

include(CTest)
if(BUILD_TESTING)
    add_executable(gameplaytest ../tests/gameplay_test.cpp game/player.cpp game/followcam.cpp)
    target_include_directories(gameplaytest PRIVATE game)
    target_link_libraries(gameplaytest PRIVATE ctw_engine)
    add_test(NAME gameplay COMMAND gameplaytest)
    add_executable(lookuptest ../tests/lookup_test.cpp game/lookups.cpp src/gfx/rendertables.cpp src/os/datafile.cpp)
    target_include_directories(lookuptest PRIVATE game src)
    add_test(NAME lookups COMMAND lookuptest)
    add_executable(portregressiontest ../tests/port_regression_test.cpp)
    target_link_libraries(portregressiontest PRIVATE ctw_gameplay)
    add_test(NAME port_regressions COMMAND portregressiontest)
    add_executable(radiorendertest ../tests/radio_render_test.cpp)
    target_link_libraries(radiorendertest PRIVATE ctw_gameplay)
    add_test(NAME radio_rendering COMMAND radiorendertest)
    set_tests_properties(radio_rendering PROPERTIES SKIP_RETURN_CODE 77)
endif()
