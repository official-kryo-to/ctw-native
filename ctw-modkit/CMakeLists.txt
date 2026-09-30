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

# ctw_asset_mod(<source folder> <installed mod folder>): copies mod.ini + textures/*.png
function(ctw_asset_mod src folder)
    configure_file(${CMAKE_CURRENT_LIST_DIR}/examples/${src}/mod.ini ${CTW_MODS_OUT}/${folder}/mod.ini COPYONLY)
    file(GLOB pngs CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/examples/${src}/textures/*.png)
    foreach(png ${pngs})
        get_filename_component(name ${png} NAME)
        configure_file(${png} ${CTW_MODS_OUT}/${folder}/textures/${name} COPYONLY)
    endforeach()
endfunction()

ctw_code_mod(cheat_example CheatExample)
ctw_asset_mod(trumpify Trumpify)
