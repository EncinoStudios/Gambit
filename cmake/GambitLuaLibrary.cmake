# The engine's Lua library (D-041): the .lua files in Gambit/Lua are embedded into the engine at build time, like the
# shaders (D-031), so a script can `require("Gambit.<Name>")` in the editor and in an exported game alike, without loose
# files.
#
#   gb_embed_lua_library(<target> <directory>)
#
# Every <directory>/**/*.lua becomes the module "Gambit.<path with dots>" (Gambit/Lua/StateMachine.lua is
# "Gambit.StateMachine"). The target gets a generated source defining Gambit::GetEngineLuaLibrary(), declared by
# Gambit/Scripting/LuaLibrary.h. The files must be ASCII.

set(GB_EMBED_TEXT_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GambitEmbedText.cmake")

function(gb_embed_lua_library target directory)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "gb_embed_lua_library: '${target}' is not a target")
    endif()
    file(GLOB_RECURSE files CONFIGURE_DEPENDS "${directory}/*.lua")
    if(NOT files)
        message(FATAL_ERROR "gb_embed_lua_library: no .lua files in ${directory}")
    endif()
    list(SORT files)

    set(names "")
    foreach(file IN LISTS files)
        cmake_path(RELATIVE_PATH file BASE_DIRECTORY "${directory}" OUTPUT_VARIABLE relative)
        string(REGEX REPLACE "\\.lua$" "" module "${relative}")
        if(NOT module MATCHES "^[A-Za-z_][A-Za-z0-9_]*(/[A-Za-z_][A-Za-z0-9_]*)*$")
            message(FATAL_ERROR "gb_embed_lua_library: '${relative}' is not a valid module path")
        endif()
        string(REPLACE "/" "." module "${module}")
        list(APPEND names "Gambit.${module}")
    endforeach()

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/Generated/LuaLibrary")
    set(manifest "${output_dir}/Manifest.cmake")
    file(CONFIGURE OUTPUT "${manifest}" CONTENT "set(GB_EMBED_NAMES \"@names@\")\nset(GB_EMBED_FILES \"@files@\")\n" @ONLY)
    set(generated_source "${output_dir}/EngineLuaLibrary.cpp")
    add_custom_command(
        OUTPUT "${generated_source}"
        COMMAND "${CMAKE_COMMAND}"
            "-DGB_MANIFEST=${manifest}"
            "-DGB_OUTPUT=${generated_source}"
            "-DGB_HEADER=Gambit/Scripting/LuaLibrary.h"
            "-DGB_FUNCTION=GetEngineLuaLibrary"
            -P "${GB_EMBED_TEXT_SCRIPT}"
        DEPENDS ${files} "${manifest}" "${GB_EMBED_TEXT_SCRIPT}"
        COMMENT "Embedding the engine Lua library"
        VERBATIM)

    # The Lua files are listed too (never compiled) so that IDEs show them with the target.
    target_sources(${target} PRIVATE "${generated_source}" ${files})
    set_source_files_properties(${files} TARGET_DIRECTORY ${target} PROPERTIES HEADER_FILE_ONLY ON)
    set_source_files_properties("${generated_source}" TARGET_DIRECTORY ${target} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endfunction()
