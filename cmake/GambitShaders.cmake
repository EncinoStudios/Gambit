# Shader build pipeline (D-031): GLSL is compiled to SPIR-V at build time with the glslang built from ThirdParty, and the
# SPIR-V is embedded into the target as generated C++ arrays (no loose shader files at run time).
#
#   gb_add_shaders(<target> <shader files...>)
#
# Each file must be named <Name>.<stage>.glsl with stage vert, frag, comp or geom; it is looked up at run time as
# "<Name>.<stage>" (ShaderLibrary::Find). Shared headers live in Gambit/Shaders/Include and are included with
# #include "File.glsl" (the shader enables GL_GOOGLE_include_directive). Depfiles make header edits recompile the shaders
# that include them, and a compile error fails the build with glslang's message.
#
# glslang runs in the target's binary directory and gets every path relative to it. Its depfile writer escapes ':' and
# backslashes Makefile-style, which CMake's depfile reader (it rewrites the file for Ninja, policy CMP0116) does not
# undo: an absolute Windows path such as C:\Gambit\Shaders\X.glsl would come back as a file that does not exist, and
# Ninja would rebuild the shader every time. Relative paths with forward slashes contain neither character (glslang
# writes included files with forward slashes too); only a source tree on another drive than the build tree falls back
# to absolute paths.
#
# The target gets a generated <target>EmbeddedShaders.cpp defining Gambit::EmbeddedShaders::Get<target>Shaders(), and
# the matching header <target>EmbeddedShaders.h on its include path. The engine (target Gambit) finds its own shaders
# automatically; other targets hand theirs to ShaderLibrary::Register.

set(GB_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../Gambit/Shaders/Include")
cmake_path(NORMAL_PATH GB_SHADER_INCLUDE_DIR)
set(GB_EMBED_SHADERS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GambitEmbedShaders.cmake")

# `path` relative to the current binary directory, the working directory of the shader commands; the absolute path when
# there is no relative one (another drive).
function(gb_shader_command_path path output)
    cmake_path(RELATIVE_PATH path BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" OUTPUT_VARIABLE relative)
    if(relative STREQUAL "" OR IS_ABSOLUTE "${relative}")
        set(relative "${path}")
    endif()
    set(${output} "${relative}" PARENT_SCOPE)
endfunction()

function(gb_add_shaders target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "gb_add_shaders: '${target}' is not a target")
    endif()
    if(NOT target MATCHES "^[A-Za-z][A-Za-z0-9_]*$")
        message(FATAL_ERROR "gb_add_shaders: the target name '${target}' must be a C++ identifier")
    endif()
    if(ARGC LESS 2)
        message(FATAL_ERROR "gb_add_shaders(${target}): no shader files given")
    endif()

    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/Shaders/${target}")
    set(names "")
    set(binaries "")
    set(sources "")
    gb_shader_command_path("${GB_SHADER_INCLUDE_DIR}" include_argument)
    foreach(source IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH source BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE source)
        list(APPEND sources "${source}")
        cmake_path(GET source FILENAME file_name)
        if(NOT file_name MATCHES "^([A-Za-z][A-Za-z0-9_]*)\\.(vert|frag|comp|geom)\\.glsl$")
            message(FATAL_ERROR
                "gb_add_shaders(${target}): '${source}' must be named <Name>.<stage>.glsl with stage vert, frag, comp "
                "or geom")
        endif()
        set(name "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}")
        set(stage "${CMAKE_MATCH_2}")
        if(name IN_LIST names)
            message(FATAL_ERROR "gb_add_shaders(${target}): two shaders are named '${name}'")
        endif()

        set(binary "${output_dir}/${name}.spv")
        set(depfile "${output_dir}/${name}.d")
        gb_shader_command_path("${binary}" binary_argument)
        gb_shader_command_path("${source}" source_argument)
        add_custom_command(
            OUTPUT "${binary}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
            COMMAND glslang-standalone
                --quiet
                --target-env vulkan1.2
                -S ${stage}
                "$<$<CONFIG:Debug>:-g>"
                "-I${include_argument}"
                --depfile "${depfile}"
                -o "${binary_argument}"
                "${source_argument}"
            MAIN_DEPENDENCY "${source}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            # A file-level dependency, so a rebuilt compiler recompiles the shaders.
            DEPENDS glslang-standalone
            DEPFILE "${depfile}"
            COMMENT "Compiling shader ${name}"
            COMMAND_EXPAND_LISTS
            VERBATIM)
        list(APPEND names "${name}")
        list(APPEND binaries "${binary}")
    endforeach()

    # The manifest carries the lists to the embedding script (a list cannot be passed through -D safely).
    set(manifest "${output_dir}/Manifest.cmake")
    file(CONFIGURE OUTPUT "${manifest}" CONTENT "set(GB_SHADER_NAMES \"@names@\")\nset(GB_SHADER_BINARIES \"@binaries@\")\n" @ONLY)

    set(generated_source "${output_dir}/${target}EmbeddedShaders.cpp")
    set(generated_header_dir "${output_dir}/include")
    set(function "Get${target}Shaders")
    file(CONFIGURE OUTPUT "${generated_header_dir}/${target}EmbeddedShaders.h" CONTENT
"#pragma once

// Generated by cmake/GambitShaders.cmake. Do not edit.

#include \"Gambit/RHI/ShaderLibrary.h\"

#include <span>

namespace Gambit::EmbeddedShaders
{
	// The SPIR-V that gb_add_shaders(@target@ ...) embedded, sorted by name.
	[[nodiscard]] std::span<const EmbeddedShader> @function@();
}
" @ONLY)

    add_custom_command(
        OUTPUT "${generated_source}"
        COMMAND "${CMAKE_COMMAND}"
            "-DGB_MANIFEST=${manifest}"
            "-DGB_OUTPUT=${generated_source}"
            "-DGB_HEADER=${target}EmbeddedShaders.h"
            "-DGB_FUNCTION=${function}"
            -P "${GB_EMBED_SHADERS_SCRIPT}"
        DEPENDS ${binaries} "${manifest}" "${GB_EMBED_SHADERS_SCRIPT}"
        COMMENT "Embedding the shaders of ${target}"
        VERBATIM)

    # The GLSL files are listed too (never compiled as C++) so that IDEs show them with the target.
    target_sources(${target} PRIVATE "${generated_source}" ${sources})
    set_source_files_properties(${sources} TARGET_DIRECTORY ${target} PROPERTIES HEADER_FILE_ONLY ON)
    target_include_directories(${target} PRIVATE "${generated_header_dir}")
    # The generated source is plain data; it never needs the precompiled header.
    set_source_files_properties("${generated_source}" TARGET_DIRECTORY ${target} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endfunction()
