#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Log.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

// Assertions come in two flavours, each with an engine ("CORE") and a client variant:
//   GB_CORE_ASSERT / GB_ASSERT  programmer-error checks; compiled out completely in Dist (the condition is not
//                               evaluated, so it must not have side effects).
//   GB_CORE_VERIFY / GB_VERIFY  always on in every configuration; the condition is always evaluated.
// Both take an optional std::format message: GB_CORE_ASSERT(index < size, "index {} out of range", index).
// Assertions are for bugs, never for bad user content: report that through Result<T> or a logged error instead.

namespace Gambit
{
	enum class AssertKind : uint8_t
	{
		Assert,
		Verify
	};

	struct AssertFailure
	{
		AssertKind Kind = AssertKind::Assert;
		bool Core = true; // reported by engine code (GB_CORE_*) rather than client code
		std::string_view Expression;
		std::string_view Message; // may be empty
		std::string_view File;
		uint32_t Line = 0;
	};

	// What the failing assertion does once the handler returned.
	enum class AssertAction : uint8_t
	{
		Break,   // trap into the debugger (terminates the process when none is attached)
		Continue // keep executing; for tests that verify assertions fire
	};

	using AssertHandler = AssertAction (*)(const AssertFailure& failure);

	// Installs a process-wide handler and returns the previous one. nullptr restores the default handler, which
	// logs the failure at critical level (to stderr when no log sink is attached), flushes the log and returns Break.
	// The handler may run on any thread.
	AssertHandler SetAssertHandler(AssertHandler handler) noexcept;

	// Entry point of the macros: invokes the installed handler.
	AssertAction ReportAssertFailure(
		AssertKind kind, bool core, std::string_view expression, std::string_view file, uint32_t line,
		std::string_view message);

	namespace Internal
	{
		inline std::string FormatAssertMessage()
		{
			return {};
		}

		template<typename... Args>
		std::string FormatAssertMessage(std::format_string<Args...> format, Args&&... args)
		{
			return std::format(format, std::forward<Args>(args)...);
		}
	}
}

// GB_CORE_ASSERT(false, "unreachable") and checks of constant expressions are intentional; without this, MSVC /W4
// reports C4127 (conditional expression is constant) at each such call site.
#if defined(GB_COMPILER_MSVC)
#	define GB_INTERNAL_CONSTANT_CONDITION_BEGIN __pragma(warning(push)) __pragma(warning(disable : 4127))
#	define GB_INTERNAL_CONSTANT_CONDITION_END __pragma(warning(pop))
#else
#	define GB_INTERNAL_CONSTANT_CONDITION_BEGIN
#	define GB_INTERNAL_CONSTANT_CONDITION_END
#endif

#define GB_INTERNAL_CHECK(core, kind, condition, ...)                                                                  \
	do                                                                                                                 \
	{                                                                                                                  \
		GB_INTERNAL_CONSTANT_CONDITION_BEGIN                                                                           \
		if (!(condition)) [[unlikely]]                                                                                 \
		{                                                                                                              \
			if (::Gambit::ReportAssertFailure(                                                                         \
					::Gambit::AssertKind::kind, core, #condition, __FILE__, static_cast<::std::uint32_t>(__LINE__),    \
					::Gambit::Internal::FormatAssertMessage(__VA_ARGS__))                                              \
			    == ::Gambit::AssertAction::Break)                                                                      \
			{                                                                                                          \
				GB_DEBUGBREAK();                                                                                       \
			}                                                                                                          \
		}                                                                                                              \
		GB_INTERNAL_CONSTANT_CONDITION_END                                                                             \
	} while (false)

// Keeps the operands "used" (no unused-variable warnings) and type-checked without evaluating them.
#define GB_INTERNAL_UNEVALUATED(condition, ...)                                                                        \
	do                                                                                                                 \
	{                                                                                                                  \
		static_cast<void>(sizeof(static_cast<bool>(condition)));                                                       \
		static_cast<void>(sizeof(::Gambit::Internal::FormatAssertMessage(__VA_ARGS__)));                               \
	} while (false)

#define GB_CORE_VERIFY(condition, ...) GB_INTERNAL_CHECK(true, Verify, condition, __VA_ARGS__)
#define GB_VERIFY(condition, ...) GB_INTERNAL_CHECK(false, Verify, condition, __VA_ARGS__)

#if defined(GB_DIST)
#	define GB_CORE_ASSERT(condition, ...) GB_INTERNAL_UNEVALUATED(condition, __VA_ARGS__)
#	define GB_ASSERT(condition, ...) GB_INTERNAL_UNEVALUATED(condition, __VA_ARGS__)
#else
#	define GB_CORE_ASSERT(condition, ...) GB_INTERNAL_CHECK(true, Assert, condition, __VA_ARGS__)
#	define GB_ASSERT(condition, ...) GB_INTERNAL_CHECK(false, Assert, condition, __VA_ARGS__)
#endif
