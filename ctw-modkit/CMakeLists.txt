# GTA: Chinatown Wars PC port - mod kit: the mod menu (a plugin the game loads), the mod interface and example mods.
# Builds on its own through the public headers:
#   cmake -S . -B build && cmake --build build
# Everything ends up in build/mods/ - copy its contents into the game's "mods" folder:
#   build/mods/ModMenu.dll            the mod loader + in-game mod menu (F4)
#   build/mods/<ModFolder>/...        the example mods
cmake_minimum_required(VERSION 3.20)
project(ctw_modkit C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CTW_MODS_OUT ${CMAKE_BINARY_DIR}/mods)
set(CTW_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/include)

# ---- the mod menu -------------------------------------------------------------------------------------------
add_library(ModMenu SHARED modmenu/modmenu.cpp)
target_include_directories(ModMenu PRIVATE ${CTW_INCLUDE})
set_target_properties(ModMenu PROPERTIES PREFIX "" OUTPUT_NAME ModMenu
    RUNTIME_OUTPUT_DIRECTORY ${CTW_MODS_OUT} LIBRARY_OUTPUT_DIRECTORY ${CTW_MODS_OUT})
if(MINGW)
    target_link_options(ModMenu PRIVATE -static -static-libgcc -static-libstdc++)
endif()
target_link_libraries(ModMenu PRIVATE ${CMAKE_DL_LIBS})   # dlopen (Linux, macOS)

# ---- example mods -------------------------------------------------------------------------------------------
# ctw_mod(<source folder> <installed mod folder>): packages mod.ini and textures, plus a DLL built from every .c/.cpp
# file in the folder when there are any.
function(ctw_mod src folder)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/examples/${src}/mod.ini ${CTW_MODS_OUT}/${folder}/mod.ini COPYONLY)
    file(GLOB pngs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/textures/*.png)
    foreach(png ${pngs})
        get_filename_component(name ${png} NAME)
        configure_file(${png} ${CTW_MODS_OUT}/${folder}/textures/${name} COPYONLY)
    endforeach()
    file(GLOB mod_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/*.c
                                            ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/*.cpp)
    if(NOT mod_sources)
        return()
    endif()
    add_library(${src} SHARED ${mod_sources})
    target_include_directories(${src} PRIVATE ${CTW_INCLUDE})
    set_target_properties(${src} PROPERTIES PREFIX "" OUTPUT_NAME ${src}
        RUNTIME_OUTPUT_DIRECTORY ${CTW_MODS_OUT}/${folder} LIBRARY_OUTPUT_DIRECTORY ${CTW_MODS_OUT}/${folder})
    if(MINGW)
        target_link_options(${src} PRIVATE -static-libgcc)
    endif()
    if(NOT MSVC)
        target_link_libraries(${src} PRIVATE m)
    endif()
endfunction()

ctw_mod(cheat_example CheatExample)
ctw_mod(enhancements Enhancements)
ctw_mod(trumpify Trumpify)

# Optional donor assets belong to the local Enhancements build, never the engine
# or distributed mod sources. Normal development builds need no extra command.
if(TARGET ctw_game AND NOT CTW_RELEASE)
    option(CTW_IMPORT_LCS "Import an available local LCS APK for development" ON)
    set(CTW_LCS_APK "${CMAKE_CURRENT_LIST_DIR}/../input/lcs/GTA-LCS-2.4.379.apk"
        CACHE FILEPATH "Own LCS Android 2.4.379 APK for development characters")
    if(CTW_IMPORT_LCS AND EXISTS "${CTW_LCS_APK}")
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        set(lcs_scripts "${CMAKE_CURRENT_LIST_DIR}/../scripts")
        add_custom_target(lcs_dev_assets ALL
            COMMAND "${Python3_EXECUTABLE}" "${lcs_scripts}/import_lcs_dev.py"
                --apk "${CTW_LCS_APK}" --out "${CTW_MODS_OUT}/Enhancements/lcs"
            BYPRODUCTS "${CTW_MODS_OUT}/Enhancements/lcs/.ctw-import.json"
            DEPENDS "${CTW_LCS_APK}" "${lcs_scripts}/import_lcs_dev.py"
                "${lcs_scripts}/lcs_assets.py" "${lcs_scripts}/apk_manifest.py"
            COMMENT "Preparing local LCS characters for Enhancements"
            VERBATIM)
        add_dependencies(enhancements lcs_dev_assets)
    endif()
endif()

if(BUILD_TESTING AND TARGET ctw_engine)
    add_executable(lcscharacterstest ../tests/lcs_characters_test.c)
    target_include_directories(lcscharacterstest PRIVATE ${CTW_INCLUDE})
    add_test(NAME enhancements_characters COMMAND lcscharacterstest)
    add_executable(enhancementscameratest ../tests/enhancements_camera_test.c)
    target_include_directories(enhancementscameratest PRIVATE ${CTW_INCLUDE})
    if(NOT MSVC)
        target_link_libraries(enhancementscameratest PRIVATE m)
    endif()
    add_test(NAME enhancements_camera COMMAND enhancementscameratest)
    add_library(api_probe_mod SHARED ../tests/api_probe_mod.c)
    target_include_directories(api_probe_mod PRIVATE ${CTW_INCLUDE})
    set_target_properties(api_probe_mod PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmods
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmods)
    add_executable(modkitapitest ../tests/modkit_api_test.cpp)
    target_include_directories(modkitapitest PRIVATE ${CTW_INCLUDE})
    target_link_libraries(modkitapitest PRIVATE ctw_engine)
    add_test(NAME modkit_api COMMAND modkitapitest $<TARGET_FILE:ModMenu> $<TARGET_FILE:api_probe_mod>)
    if(TARGET ctw_gameplay)
        add_executable(mousecapturetest ../tests/mouse_capture_test.cpp)
        target_link_libraries(mousecapturetest PRIVATE ctw_gameplay)
        add_dependencies(mousecapturetest ModMenu enhancements)
        add_test(NAME gameplay_mouse_capture COMMAND mousecapturetest $<TARGET_FILE:ModMenu> $<TARGET_FILE:enhancements>)
        set_tests_properties(gameplay_mouse_capture PROPERTIES SKIP_RETURN_CODE 77)
    endif()
endif()
