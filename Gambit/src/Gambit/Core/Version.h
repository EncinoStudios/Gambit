#pragma once

#include "Gambit/Core/Base.h"

#include <string_view>

// The single source of truth for the engine version. CMake parses the three numeric defines below, so keep them
// as plain `#define GB_VERSION_<PART> <number>` lines.
#define GB_VERSION_MAJOR 0
#define GB_VERSION_MINOR 1
#define GB_VERSION_PATCH 0

#define GB_VERSION_STRING                                                                                              \
	GB_STRINGIFY_MACRO(GB_VERSION_MAJOR)                                                                               \
	"." GB_STRINGIFY_MACRO(GB_VERSION_MINOR) "." GB_STRINGIFY_MACRO(GB_VERSION_PATCH)

namespace Gambit
{
	inline constexpr int VersionMajor = GB_VERSION_MAJOR;
	inline constexpr int VersionMinor = GB_VERSION_MINOR;
	inline constexpr int VersionPatch = GB_VERSION_PATCH;
	inline constexpr std::string_view VersionString = GB_VERSION_STRING;
}
