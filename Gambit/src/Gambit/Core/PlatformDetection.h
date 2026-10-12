#pragma once

// Platform, compiler and architecture detection. Exactly one GB_PLATFORM_*, one GB_COMPILER_* and one GB_ARCH_*
// macro is defined; unsupported targets fail to compile here instead of somewhere obscure later.

#if defined(_WIN32)
#	if !defined(_WIN64)
#		error "Gambit requires 64-bit Windows"
#	endif
#	define GB_PLATFORM_WINDOWS 1
#elif defined(__APPLE__) || defined(__MACH__)
#	include <TargetConditionals.h>
#	if TARGET_IPHONE_SIMULATOR || TARGET_OS_IPHONE
#		error "iOS is not supported"
#	elif TARGET_OS_MAC
#		define GB_PLATFORM_MACOS 1
#	else
#		error "Unknown Apple platform"
#	endif
#elif defined(__ANDROID__)
#	error "Android is not supported"
#elif defined(__linux__)
#	define GB_PLATFORM_LINUX 1
#else
#	error "Unsupported platform"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#	define GB_COMPILER_MSVC 1
#elif defined(__clang__)
#	define GB_COMPILER_CLANG 1
#elif defined(__GNUC__)
#	define GB_COMPILER_GCC 1
#else
#	error "Unsupported compiler"
#endif

#if defined(__x86_64__) || defined(_M_X64)
#	define GB_ARCH_X64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#	define GB_ARCH_ARM64 1
#else
#	error "Unsupported CPU architecture"
#endif
