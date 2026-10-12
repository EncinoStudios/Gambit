# Third-party dependencies (D-003). Sources are pinned git submodules and are never edited (AGENTS.md section 7);
# patches and configuration live here. Every target below builds with warnings disabled.
#
# Wired now: glm, doctest, nlohmann_json, EnTT, glfw, xxhash, lz4, monocypher, stb.
# The remaining submodules (see ThirdParty/README.md) are wired by the task that first needs them.

set(GB_THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Fails early with an actionable message when a submodule has not been checked out.
function(gambit_require_submodule name sentinel)
    if(NOT EXISTS "${GB_THIRDPARTY_DIR}/${name}/${sentinel}")
        message(FATAL_ERROR
            "ThirdParty/${name} is missing. Run: git submodule update --init --recursive")
    endif()
endfunction()

# Suppress every warning of a third-party target that is compiled from source.
function(gambit_silence_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# Header-only dependency: an INTERFACE target whose include directory is a SYSTEM one, so that its headers do not
# produce warnings in first-party code either.
function(gambit_header_only_library target alias name include_dir sentinel)
    gambit_require_submodule(${name} ${sentinel})
    add_library(${target} INTERFACE)
    target_include_directories(${target} SYSTEM INTERFACE "${GB_THIRDPARTY_DIR}/${name}/${include_dir}")
    add_library(${alias} ALIAS ${target})
endfunction()

gambit_header_only_library(gb_glm glm::glm glm "." glm/glm.hpp)
gambit_header_only_library(gb_doctest doctest::doctest doctest "." doctest/doctest.h)
gambit_header_only_library(gb_nlohmann_json nlohmann_json::nlohmann_json nlohmann_json include include/nlohmann/json.hpp)
gambit_header_only_library(gb_entt EnTT::EnTT entt src src/entt/entt.hpp)
gambit_header_only_library(gb_stb stb::stb stb "." stb_image.h)

# GLFW: static, no examples/tests/docs, no install rules. The engine talks to Vulkan, never OpenGL.
gambit_require_submodule(glfw CMakeLists.txt)
set(BUILD_SHARED_LIBS OFF)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw EXCLUDE_FROM_ALL)
gambit_silence_warnings(glfw)
target_compile_definitions(glfw INTERFACE GLFW_INCLUDE_NONE)
target_include_directories(glfw SYSTEM INTERFACE "${GB_THIRDPARTY_DIR}/glfw/include")
add_library(glfw::glfw ALIAS glfw)

# xxHash: the library part only (the xxhsum CLI is GPL and is not built).
gambit_require_submodule(xxHash xxhash.c)
add_library(gb_xxhash STATIC "${GB_THIRDPARTY_DIR}/xxHash/xxhash.c")
target_include_directories(gb_xxhash SYSTEM PUBLIC "${GB_THIRDPARTY_DIR}/xxHash")
gambit_silence_warnings(gb_xxhash)
add_library(xxhash::xxhash ALIAS gb_xxhash)

# LZ4: lib/ only (BSD-2-Clause). lz4frame.c needs the older xxhash copy in lz4/lib; like LZ4's own build, its
# symbols are prefixed (LZ4_XXH*) so they never clash with gb_xxhash. Consumers only see LZ4's public headers, copied
# to the build tree, so that <xxhash.h> always resolves to xxHash itself whatever the link order.
gambit_require_submodule(lz4 lib/lz4.c)
add_library(gb_lz4 STATIC
    "${GB_THIRDPARTY_DIR}/lz4/lib/lz4.c"
    "${GB_THIRDPARTY_DIR}/lz4/lib/lz4hc.c"
    "${GB_THIRDPARTY_DIR}/lz4/lib/lz4frame.c"
    "${GB_THIRDPARTY_DIR}/lz4/lib/xxhash.c")
target_compile_definitions(gb_lz4 PRIVATE XXH_NAMESPACE=LZ4_)
set(GB_LZ4_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/lz4/include")
foreach(GB_LZ4_HEADER lz4.h lz4hc.h lz4frame.h lz4frame_static.h)
    configure_file("${GB_THIRDPARTY_DIR}/lz4/lib/${GB_LZ4_HEADER}" "${GB_LZ4_INCLUDE_DIR}/${GB_LZ4_HEADER}" COPYONLY)
endforeach()
target_include_directories(gb_lz4 PRIVATE "${GB_THIRDPARTY_DIR}/lz4/lib")
target_include_directories(gb_lz4 SYSTEM INTERFACE "${GB_LZ4_INCLUDE_DIR}")
gambit_silence_warnings(gb_lz4)
add_library(lz4::lz4 ALIAS gb_lz4)

# Monocypher plus its optional SHA-512/Ed25519-compatible module (D-070 signs manifests with Ed25519).
gambit_require_submodule(Monocypher src/monocypher.c)
add_library(gb_monocypher STATIC
    "${GB_THIRDPARTY_DIR}/Monocypher/src/monocypher.c"
    "${GB_THIRDPARTY_DIR}/Monocypher/src/optional/monocypher-ed25519.c")
target_include_directories(gb_monocypher SYSTEM PUBLIC
    "${GB_THIRDPARTY_DIR}/Monocypher/src"
    "${GB_THIRDPARTY_DIR}/Monocypher/src/optional")
gambit_silence_warnings(gb_monocypher)
add_library(monocypher::monocypher ALIAS gb_monocypher)
