# Compiler options for first-party Gambit targets (engine, tools, tests).
# Third-party code never goes through gambit_configure_target(); ThirdParty/CMakeLists.txt silences its warnings.

# Use ccache as compiler launcher when available. `time_macros` lets ccache cache the translation units that use a
# precompiled header. `pch_defines` is deliberately not set: with it ccache caches PCH creation in preprocessor mode,
# which cannot see #define changes, and hands out a stale PCH after a macro-only header edit. Without it ccache simply
# compiles each PCH (one per target) and caches everything else.
function(gambit_enable_ccache)
    if(NOT GB_USE_CCACHE)
        return()
    endif()
    find_program(GB_CCACHE_PROGRAM ccache)
    if(NOT GB_CCACHE_PROGRAM)
        return()
    endif()
    set(GB_CCACHE_LAUNCHER "${CMAKE_COMMAND}" -E env "CCACHE_SLOPPINESS=time_macros" "${GB_CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER "${GB_CCACHE_LAUNCHER}" PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${GB_CCACHE_LAUNCHER}" PARENT_SCOPE)
    message(STATUS "ccache enabled: ${GB_CCACHE_PROGRAM}")
endfunction()

# Warnings (D-001) and the floating-point determinism flags (D-011) for a first-party target.
# Never add -ffast-math or any flag that lets the compiler fuse or reorder floating-point operations.
function(gambit_configure_target target)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_features(${target} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /EHsc /Zc:__cplusplus /Zc:preprocessor
            /fp:precise)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(GB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -ffp-contract=off)
        if(GB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
