# GTA: Chinatown Wars PC port - mod kit: the mod menu (a plugin the game loads) + the mod SDK + example mods.
# Needs nothing from the game's source code.
#   cmake -S . -B build && cmake --build build
# Everything ends up in build/mods/ - copy its contents into the game's "mods" folder:
#   build/mods/ModMenu.dll            the mod loader + in-game mod menu (F4)
#   build/mods/<ModFolder>/...        the example mods
cmake_minimum_required(VERSION 3.20)
project(ctw_modkit C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CTW_MODS_OUT ${CMAKE_BINARY_DIR}/mods)
set(CTW_INCLUDE ${CMAKE_CURRENT_LIST_DIR}/include)

if(MINGW)
    set(CTW_STATIC_RUNTIME -static -static-libgcc -static-libstdc++)
endif()

# ---- the mod menu -------------------------------------------------------------------------------------------
add_library(ModMenu SHARED modmenu/modmenu.cpp)
target_include_directories(ModMenu PRIVATE ${CTW_INCLUDE})
set_target_properties(ModMenu PROPERTIES PREFIX "" OUTPUT_NAME ModMenu
    RUNTIME_OUTPUT_DIRECTORY ${CTW_MODS_OUT} LIBRARY_OUTPUT_DIRECTORY ${CTW_MODS_OUT})
if(MINGW)
    target_link_options(ModMenu PRIVATE ${CTW_STATIC_RUNTIME})
endif()

# ---- example mods -------------------------------------------------------------------------------------------
# ctw_code_mod(<source folder> <installed mod folder>): builds <source folder>/<source folder>.c into a DLL
function(ctw_code_mod src folder)
    add_library(${src} SHARED ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/${src}.c)
    target_include_directories(${src} PRIVATE ${CTW_INCLUDE})
    set_target_properties(${src} PROPERTIES PREFIX "" OUTPUT_NAME ${src}
        RUNTIME_OUTPUT_DIRECTORY ${CTW_MODS_OUT}/${folder} LIBRARY_OUTPUT_DIRECTORY ${CTW_MODS_OUT}/${folder})
    if(MINGW)
        target_link_options(${src} PRIVATE -static-libgcc)
    endif()
    configure_file(${CMAKE_CURRENT_LIST_DIR}/examples/${src}/mod.ini ${CTW_MODS_OUT}/${folder}/mod.ini COPYONLY)
endfunction()

# ctw_asset_mod(<source folder> <installed mod folder>): copies mod.ini + textures/
function(ctw_asset_mod src folder)
    file(COPY ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/ DESTINATION ${CTW_MODS_OUT}/${folder})
endfunction()

ctw_code_mod(time_control TimeControl)
ctw_code_mod(free_camera CameraAndCoords)
ctw_asset_mod(pink_water PinkWater)
