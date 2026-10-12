cmake_minimum_required(VERSION 3.24)

# The engine version lives in exactly one place (Version.h); CMake reads it from there.
set(GB_VERSION_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/Gambit/src/Gambit/Core/Version.h")
file(STRINGS "${GB_VERSION_HEADER}" GB_VERSION_LINES REGEX "^#define GB_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(GB_VERSION_PART MAJOR MINOR PATCH)
    set(GB_VERSION_${GB_VERSION_PART} "")
    foreach(GB_VERSION_LINE IN LISTS GB_VERSION_LINES)
        if(GB_VERSION_LINE MATCHES "^#define GB_VERSION_${GB_VERSION_PART} ([0-9]+)$")
            set(GB_VERSION_${GB_VERSION_PART} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(GB_VERSION_${GB_VERSION_PART} STREQUAL "")
        message(FATAL_ERROR "Could not read GB_VERSION_${GB_VERSION_PART} from ${GB_VERSION_HEADER}")
    endif()
endforeach()

# Must be decided before project(): the std::format / <chrono> support in libc++ needs macOS 13.3, and ccache
# needs embedded (/Z7) debug info on MSVC (CMP0141, CMake 3.25+; silently ignored on 3.24). The policy has to be set
# explicitly: CMAKE_POLICY_DEFAULT_CMP0141 is only read by cmake_minimum_required(), which has already run.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3" CACHE STRING "Minimum macOS deployment target")
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

project(Gambit
    VERSION ${GB_VERSION_MAJOR}.${GB_VERSION_MINOR}.${GB_VERSION_PATCH}
    DESCRIPTION "Gambit: a deterministic 3D game engine"
    LANGUAGES C CXX)

if(NOT PROJECT_IS_TOP_LEVEL)
    message(FATAL_ERROR "Gambit must be configured as the top-level project.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(GB_BUILD_EDITOR "Build GambitEditor (never built in the Dist configuration)" ON)
option(GB_BUILD_RUNTIME "Build GambitRuntime, the shipping player" ON)
option(GB_BUILD_CLI "Build the gambit command-line tool" ON)
option(GB_BUILD_TESTS "Build GambitTests and register it with CTest" ON)
option(GB_WARNINGS_AS_ERRORS "Treat warnings in first-party code as errors (D-001, always ON in CI)" ON)
option(GB_USE_CCACHE "Use ccache as compiler launcher when it is installed" ON)

include(GambitConfigurations)
include(GambitCompilerOptions)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

gambit_enable_ccache()

if(GB_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(ThirdParty)
add_subdirectory(Gambit)

if(GB_BUILD_CLI)
    add_subdirectory(GambitCLI)
endif()

if(GB_BUILD_RUNTIME)
    add_subdirectory(GambitRuntime)
endif()

if(GB_BUILD_EDITOR)
    if(CMAKE_BUILD_TYPE STREQUAL "Dist")
        message(STATUS "GambitEditor is not built in the Dist configuration (D-001)")
    else()
        add_subdirectory(GambitEditor)
    endif()
endif()

if(GB_BUILD_TESTS)
    add_subdirectory(GambitTests)
endif()
