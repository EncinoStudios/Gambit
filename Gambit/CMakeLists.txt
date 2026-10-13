# The engine library. Sources are globbed (CONFIGURE_DEPENDS) so that parallel tasks adding files never conflict
# on this list.
file(GLOB_RECURSE GB_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(Gambit STATIC ${GB_ENGINE_SOURCES})
add_library(Gambit::Gambit ALIAS Gambit)

gambit_configure_target(Gambit)

target_include_directories(Gambit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(GB_USE_PCH)
    target_precompile_headers(Gambit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/gbpch.h")
endif()

# Configuration and platform definitions are PUBLIC so that every consumer of the engine headers agrees on them.
target_compile_definitions(Gambit PUBLIC
    $<$<CONFIG:Debug>:GB_DEBUG>
    $<$<CONFIG:Release>:GB_RELEASE>
    $<$<CONFIG:Dist>:GB_DIST>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>)

# glm is configured once, here, so that every consumer of Math/Math.h agrees (an ODR hazard otherwise). SIMD
# intrinsics (GLM_FORCE_INTRINSICS) are deliberately not enabled: simulation code must compile to the same
# floating-point operations on every platform (D-011).
target_compile_definitions(Gambit PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_RADIANS
    GLM_ENABLE_EXPERIMENTAL)

find_package(Threads REQUIRED)
# EnTT is part of the public API: World exposes its registry and components are EnTT storages (D-021). So is
# nlohmann_json: the serialization and command APIs pass JSON documents (Serialization/Json.h). So is NVRHI: the
# renderer and editor talk to the GPU through NVRHI handles (D-030); Vulkan itself stays private to RHI/Vulkan.
target_link_libraries(Gambit PUBLIC Threads::Threads glm::glm EnTT::EnTT nlohmann_json::nlohmann_json nvrhi)
target_link_libraries(Gambit PRIVATE
    xxhash::xxhash glfw::glfw stb::stb cgltf::cgltf mikktspace::mikktspace nvrhi_vk Vulkan::Headers)

# Engine shaders (D-031): every <Name>.<stage>.glsl outside Shaders/Include is compiled and embedded.
file(GLOB_RECURSE GB_ENGINE_SHADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.vert.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.frag.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.comp.glsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/*.geom.glsl")
list(FILTER GB_ENGINE_SHADERS EXCLUDE REGEX "/Shaders/Include/")
gb_add_shaders(Gambit ${GB_ENGINE_SHADERS})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${GB_ENGINE_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/Shaders" PREFIX "Shaders" FILES ${GB_ENGINE_SHADERS})
