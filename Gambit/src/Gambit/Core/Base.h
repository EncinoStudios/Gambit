#pragma once

#include "Gambit/Core/PlatformDetection.h"

#include <memory>
#include <utility>

#if defined(GB_COMPILER_MSVC)
#	include <intrin.h>
#	define GB_DEBUGBREAK() __debugbreak()
#elif defined(GB_COMPILER_CLANG)
#	define GB_DEBUGBREAK() __builtin_debugtrap()
#else
#	include <csignal>
#	define GB_DEBUGBREAK() std::raise(SIGTRAP)
#endif

#define GB_EXPAND_MACRO(x) x
#define GB_INTERNAL_STRINGIFY(x) #x
// Expands macro arguments before stringifying them: GB_STRINGIFY_MACRO(GB_VERSION_MAJOR) yields "0", not the name.
#define GB_STRINGIFY_MACRO(x) GB_INTERNAL_STRINGIFY(x)

// Bit mask with bit `x` set (32-bit, for flag enums and bit fields).
#define GB_BIT(x) (1u << (x))

namespace Gambit
{
	// Shared ownership. Prefer Scope<T> unless ownership is genuinely shared.
	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	[[nodiscard]] constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	// Unique ownership.
	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	[[nodiscard]] constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}
}
