# Build configurations: Debug, Release and the custom Dist (D-001).
# Dist is Release plus GB_DIST (a PUBLIC definition of the Gambit target, see Gambit/CMakeLists.txt): asserts, the
# editor and ImGui debug UI are stripped.

get_property(GB_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
set(GB_CONFIGURATIONS Debug Release Dist)

if(GB_IS_MULTI_CONFIG)
    set(CMAKE_CONFIGURATION_TYPES "${GB_CONFIGURATIONS}" CACHE STRING "Available build configurations" FORCE)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
    endif()
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${GB_CONFIGURATIONS})
    if(NOT CMAKE_BUILD_TYPE IN_LIST GB_CONFIGURATIONS)
        message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of: ${GB_CONFIGURATIONS} (got '${CMAKE_BUILD_TYPE}')")
    endif()
endif()

# Dist starts from the Release flags; everything else that differs is expressed through GB_DIST.
foreach(GB_LANG C CXX)
    set(CMAKE_${GB_LANG}_FLAGS_DIST "${CMAKE_${GB_LANG}_FLAGS_RELEASE}" CACHE STRING "Flags used by the ${GB_LANG} compiler in Dist")
endforeach()
foreach(GB_LINK_KIND EXE SHARED MODULE STATIC)
    set(CMAKE_${GB_LINK_KIND}_LINKER_FLAGS_DIST "${CMAKE_${GB_LINK_KIND}_LINKER_FLAGS_RELEASE}" CACHE STRING "Flags used by the ${GB_LINK_KIND} linker in Dist")
endforeach()
mark_as_advanced(
    CMAKE_C_FLAGS_DIST CMAKE_CXX_FLAGS_DIST
    CMAKE_EXE_LINKER_FLAGS_DIST CMAKE_SHARED_LINKER_FLAGS_DIST
    CMAKE_MODULE_LINKER_FLAGS_DIST CMAKE_STATIC_LINKER_FLAGS_DIST)
