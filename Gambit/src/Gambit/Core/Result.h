#pragma once

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/Base.h"

#include <concepts>
#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace Gambit
{
	// Why an operation failed: a human-readable message and an optional, module-defined code (0 = unspecified).
	struct Error
	{
		std::string Message;
		int Code = 0;

		explicit Error(std::string message, int code = 0)
			: Message(std::move(message)), Code(code)
		{}

		template<typename... Args>
		[[nodiscard]] static Error Format(std::format_string<Args...> format, Args&&... args)
		{
			return Error(std::format(format, std::forward<Args>(args)...));
		}

		template<typename... Args>
		[[nodiscard]] static Error FormatWithCode(int code, std::format_string<Args...> format, Args&&... args)
		{
			return Error(std::format(format, std::forward<Args>(args)...), code);
		}
	};

	// Either a value of type T or an Error. This is how fallible engine functions report runtime failures (bad
	// content, missing files, ...) without exceptions. The result must be inspected: check HasValue() (or the bool
	// conversion) before calling Value(); Value() and GetError() verify their precondition in every configuration.
	template<typename T>
	class [[nodiscard]] Result
	{
		static_assert(!std::is_reference_v<T>, "Result<T&> is not supported");
		static_assert(!std::is_same_v<std::remove_cv_t<T>, Error>, "Result<Error> would be ambiguous");

	public:
		Result(Error error)
			: m_Storage(std::in_place_index<1>, std::move(error))
		{}

		// Implicit exactly when T is implicitly constructible from U, so Result<std::string> accepts "text" but not
		// a std::string_view (which std::string only accepts explicitly).
		template<typename U>
			requires(
				std::convertible_to<U &&, T> && !std::same_as<std::remove_cvref_t<U>, Result>
				&& !std::same_as<std::remove_cvref_t<U>, Error>)
		Result(U&& value)
			: m_Storage(std::in_place_index<0>, std::forward<U>(value))
		{}

		[[nodiscard]] bool HasValue() const noexcept { return m_Storage.index() == 0; }
		[[nodiscard]] explicit operator bool() const noexcept { return HasValue(); }

		[[nodiscard]] T& Value() &
		{
			VerifyHasValue();
			return *std::get_if<0>(&m_Storage);
		}

		[[nodiscard]] const T& Value() const&
		{
			VerifyHasValue();
			return *std::get_if<0>(&m_Storage);
		}

		[[nodiscard]] T&& Value() &&
		{
			VerifyHasValue();
			return std::move(*std::get_if<0>(&m_Storage));
		}

		[[nodiscard]] T& operator*() & { return Value(); }
		[[nodiscard]] const T& operator*() const& { return Value(); }
		[[nodiscard]] T&& operator*() && { return std::move(*this).Value(); }
		[[nodiscard]] T* operator->() { return &Value(); }
		[[nodiscard]] const T* operator->() const { return &Value(); }

		[[nodiscard]] const Error& GetError() const&
		{
			GB_CORE_VERIFY(!HasValue(), "Result::GetError() called on a successful result");
			return *std::get_if<1>(&m_Storage);
		}

		// The value, or `fallback` converted to T when this holds an error.
		template<typename U>
		[[nodiscard]] T ValueOr(U&& fallback) const&
		{
			return HasValue() ? *std::get_if<0>(&m_Storage) : static_cast<T>(std::forward<U>(fallback));
		}

		template<typename U>
		[[nodiscard]] T ValueOr(U&& fallback) &&
		{
			return HasValue() ? std::move(*std::get_if<0>(&m_Storage)) : static_cast<T>(std::forward<U>(fallback));
		}

	private:
		void VerifyHasValue() const
		{
			GB_CORE_VERIFY(HasValue(), "Result::Value() called on an error: {}", std::get_if<1>(&m_Storage)->Message);
		}

		std::variant<T, Error> m_Storage;
	};

	// Success-or-error for operations that produce no value. A default-constructed Result<void> is a success.
	template<>
	class [[nodiscard]] Result<void>
	{
	public:
		Result() = default;
		Result(Error error)
			: m_Error(std::move(error))
		{}

		[[nodiscard]] bool HasValue() const noexcept { return !m_Error.has_value(); }
		[[nodiscard]] explicit operator bool() const noexcept { return HasValue(); }

		[[nodiscard]] const Error& GetError() const&
		{
			GB_CORE_VERIFY(m_Error.has_value(), "Result::GetError() called on a successful result");
			return *m_Error;
		}

	private:
		std::optional<Error> m_Error;
	};
}
