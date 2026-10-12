#include <doctest/doctest.h>

#include "Gambit/Core/Base.h"
#include "Gambit/Core/PlatformDetection.h"
#include "Gambit/Core/Version.h"

#include <format>
#include <string>

#define GB_TEST_NUMBER 42

namespace
{
	struct Tracked
	{
		explicit Tracked(int& destroyed)
			: Destroyed(destroyed)
		{}
		~Tracked() { ++Destroyed; }
		int& Destroyed;
	};
}

TEST_CASE("Core: Ref shares ownership and Scope owns uniquely")
{
	int destroyed = 0;
	{
		const Gambit::Ref<Tracked> first = Gambit::CreateRef<Tracked>(destroyed);
		const Gambit::Ref<Tracked> second = first;
		CHECK(first.use_count() == 2);
		CHECK(first.get() == second.get());
	}
	CHECK(destroyed == 1);

	{
		Gambit::Scope<Tracked> scope = Gambit::CreateScope<Tracked>(destroyed);
		CHECK(scope != nullptr);
		const Gambit::Scope<Tracked> moved = std::move(scope);
		CHECK(scope == nullptr);
	}
	CHECK(destroyed == 2);
}

TEST_CASE("Core: macros GB_BIT, GB_STRINGIFY_MACRO and GB_EXPAND_MACRO")
{
	CHECK(GB_BIT(0) == 1u);
	CHECK(GB_BIT(5) == 32u);
	CHECK(GB_BIT(31) == 0x80000000u);

	// The argument is macro-expanded before it is stringified.
	CHECK(std::string(GB_STRINGIFY_MACRO(GB_TEST_NUMBER)) == "42");
	CHECK(std::string(GB_STRINGIFY_MACRO(plain_identifier)) == "plain_identifier");
	CHECK(GB_EXPAND_MACRO(GB_TEST_NUMBER) == 42);
}

TEST_CASE("Core: exactly one platform, compiler and architecture is detected")
{
	const int platforms = []() {
		int count = 0;
#if defined(GB_PLATFORM_WINDOWS)
		++count;
#endif
#if defined(GB_PLATFORM_LINUX)
		++count;
#endif
#if defined(GB_PLATFORM_MACOS)
		++count;
#endif
		return count;
	}();
	const int compilers = []() {
		int count = 0;
#if defined(GB_COMPILER_MSVC)
		++count;
#endif
#if defined(GB_COMPILER_CLANG)
		++count;
#endif
#if defined(GB_COMPILER_GCC)
		++count;
#endif
		return count;
	}();
	const int architectures = []() {
		int count = 0;
#if defined(GB_ARCH_X64)
		++count;
#endif
#if defined(GB_ARCH_ARM64)
		++count;
#endif
		return count;
	}();

	CHECK(platforms == 1);
	CHECK(compilers == 1);
	CHECK(architectures == 1);
}

TEST_CASE("Core: version constants agree with the version string")
{
	CHECK(
		Gambit::VersionString
		== std::format("{}.{}.{}", Gambit::VersionMajor, Gambit::VersionMinor, Gambit::VersionPatch));
	CHECK(std::string(GB_VERSION_STRING) == std::string(Gambit::VersionString));
	CHECK(Gambit::VersionMajor >= 0);
}
