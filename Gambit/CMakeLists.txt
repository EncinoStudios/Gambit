# The engine library. Sources are globbed (CONFIGURE_DEPENDS) so that parallel tasks adding files never conflict
# on this list.
file(GLOB_RECURSE GB_ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(Gambit STATIC ${GB_ENGINE_SOURCES})
add_library(Gambit::Gambit ALIAS Gambit)

gambit_configure_target(Gambit)

target_include_directories(Gambit PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_precompile_headers(Gambit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/gbpch.h")

# Configuration and platform definitions are PUBLIC so that every consumer of the engine headers agrees on them.
target_compile_definitions(Gambit PUBLIC
    $<$<CONFIG:Debug>:GB_DEBUG>
    $<$<CONFIG:Release>:GB_RELEASE>
    $<$<CONFIG:Dist>:GB_DIST>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>)

find_package(Threads REQUIRED)
target_link_libraries(Gambit PUBLIC Threads::Threads)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${GB_ENGINE_SOURCES})
