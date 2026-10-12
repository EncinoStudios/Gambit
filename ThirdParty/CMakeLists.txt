# Third-party dependencies (D-003). Sources are pinned git submodules and are never edited (AGENTS.md section 7);
# patches and configuration live here. Every target below builds with warnings disabled.
#
# Wired now: glm, doctest, nlohmann_json, EnTT, glfw, xxhash, lz4, monocypher, stb, Vulkan-Headers, NVRHI, glslang.
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
# doctest forward-declares std::ostream instead of including <ostream> unless it recognises libc++, so under MSVC a
# CHECK that stringifies a std::string_view (ToString(...) == "x") fails with C2027 in every test file that does not
# happen to include <sstream>/<fstream>. Always include the standard headers, as libc++ already gets automatically.
target_compile_definitions(gb_doctest INTERFACE DOCTEST_CONFIG_USE_STD_HEADERS)
gambit_header_only_library(gb_nlohmann_json nlohmann_json::nlohmann_json nlohmann_json include include/nlohmann/json.hpp)
gambit_header_only_library(gb_entt EnTT::EnTT entt src src/entt/entt.hpp)
gambit_header_only_library(gb_stb stb::stb stb "." stb_image.h)
# All engine IO goes through the VFS (D-014): images are decoded from and encoded to memory, never via stdio. The engine
# compiles the single implementation translation unit (Gambit/src/Gambit/RHI/Internal/StbImage.cpp).
target_compile_definitions(gb_stb INTERFACE STBI_NO_STDIO STBI_WRITE_NO_STDIO)

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

# Vulkan-Headers v1.4.352, the tag NVRHI main is tested with (see ThirdParty/README.md). Headers only: Vulkan is loaded
# at run time through the Vulkan-Hpp dynamic dispatcher (D-030), and VK_NO_PROTOTYPES keeps any link-time use of the
# loader out. The Vulkan-Hpp configuration must be identical in every translation unit that includes vulkan.hpp
# (NVRHI's and the engine's), otherwise the inline functions of both differ (an ODR violation), so it is set here once.
gambit_require_submodule(Vulkan-Headers include/vulkan/vulkan.hpp)
add_library(gb_vulkan_headers INTERFACE)
target_include_directories(gb_vulkan_headers SYSTEM INTERFACE "${GB_THIRDPARTY_DIR}/Vulkan-Headers/include")
target_compile_definitions(gb_vulkan_headers INTERFACE VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
# vk::detail::DynamicLoader uses dlopen.
target_link_libraries(gb_vulkan_headers INTERFACE ${CMAKE_DL_LIBS})
add_library(Vulkan::Headers ALIAS gb_vulkan_headers)

# NVRHI: static libraries, Vulkan backend and validation layer only, no install rules, nothing fetched at configure time.
gambit_require_submodule(nvrhi CMakeLists.txt)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_NVAPI OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_RTXMU OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_AFTERMATH OFF CACHE BOOL "" FORCE)
set(NVRHI_FETCH_VULKAN_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory(nvrhi EXCLUDE_FROM_ALL)
gambit_silence_warnings(nvrhi)
gambit_silence_warnings(nvrhi_vk)
# The public headers are part of the engine's RHI API; as SYSTEM headers they do not trip first-party warnings.
target_include_directories(nvrhi SYSTEM INTERFACE "${GB_THIRDPARTY_DIR}/nvrhi/include")
# nvrhi_vk uses the common code in nvrhi (state tracking, utils) but does not declare it; static link order needs it.
target_link_libraries(nvrhi_vk PUBLIC nvrhi)

# glslang: only the standalone compiler, used at build time by gb_add_shaders (D-031). No optimizer (ENABLE_OPT=OFF, so
# SPIRV-Tools is not needed), no HLSL front end, no tests or install rules. Its build generates a header with Python 3.
gambit_require_submodule(glslang CMakeLists.txt)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPIRV ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_JS OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH ${GB_USE_PCH} CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(glslang EXCLUDE_FROM_ALL)
foreach(GB_GLSLANG_TARGET glslang glslang-standalone glslang-default-resource-limits SPIRV MachineIndependent
        GenericCodeGen OSDependent)
    if(TARGET ${GB_GLSLANG_TARGET})
        gambit_silence_warnings(${GB_GLSLANG_TARGET})
    endif()
endforeach()
# A build tool, not a product: keep it out of bin/ (which holds what ships).
set_target_properties(glslang-standalone PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
