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

if(BUILD_TESTING AND TARGET ctw_engine)
    add_library(api_probe_mod SHARED ../tests/api_probe_mod.c)
    target_include_directories(api_probe_mod PRIVATE ${CTW_INCLUDE})
    set_target_properties(api_probe_mod PROPERTIES PREFIX "" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmods
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/testmods)
    add_executable(modkitapitest ../tests/modkit_api_test.cpp)
    target_include_directories(modkitapitest PRIVATE ${CTW_INCLUDE})
    target_link_libraries(modkitapitest PRIVATE ctw_engine)
    add_test(NAME modkit_api COMMAND modkitapitest $<TARGET_FILE:ModMenu> $<TARGET_FILE:api_probe_mod>)
endif()
