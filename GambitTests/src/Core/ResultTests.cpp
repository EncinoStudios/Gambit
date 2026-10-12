#include <doctest/doctest.h>

#include "Gambit/Core/Result.h"

#include <memory>
#include <string>
#include <type_traits>

namespace
{
	Gambit::Result<int> ParsePositive(int value)
	{
		if (value <= 0)
		{
			return Gambit::Error::FormatWithCode(7, "{} is not positive", value);
		}
		return value;
	}

	Gambit::Result<void> Validate(bool ok)
	{
		if (!ok)
		{
			return Gambit::Error("validation failed");
		}
		return {};
	}

	Gambit::Result<std::unique_ptr<int>> MakeUnique(bool ok)
	{
		if (!ok)
		{
			return Gambit::Error("no pointer");
		}
		return std::make_unique<int>(5);
	}
}

TEST_CASE("Result: holds a value")
{
	const Gambit::Result<int> result = ParsePositive(3);
	REQUIRE(result.HasValue());
	CHECK(static_cast<bool>(result));
	CHECK(result.Value() == 3);
	CHECK(*result == 3);
	CHECK(result.ValueOr(-1) == 3);
}

TEST_CASE("Result: holds an error with message and code")
{
	const Gambit::Result<int> result = ParsePositive(-2);
	REQUIRE_FALSE(result.HasValue());
	CHECK_FALSE(static_cast<bool>(result));
	CHECK(result.GetError().Message == "-2 is not positive");
	CHECK(result.GetError().Code == 7);
	CHECK(result.ValueOr(-1) == -1);
}

TEST_CASE("Result: errors default to code 0 and format their message")
{
	const Gambit::Error plain("plain");
	CHECK(plain.Message == "plain");
	CHECK(plain.Code == 0);

	const Gambit::Error formatted = Gambit::Error::Format("{}-{}", "a", 1);
	CHECK(formatted.Message == "a-1");
	CHECK(formatted.Code == 0);
}

TEST_CASE("Result: converts implicitly from compatible values")
{
	Gambit::Result<std::string> fromLiteral = "text";
	REQUIRE(fromLiteral.HasValue());
	CHECK(fromLiteral->size() == 4);
	CHECK(*fromLiteral == "text");

	Gambit::Result<std::string> fromError = Gambit::Error("failed");
	REQUIRE_FALSE(fromError.HasValue());
	CHECK(fromError.GetError().Message == "failed");

	// Result<std::string> built from an Error must never be mistaken for a string value.
	static_assert(std::is_convertible_v<Gambit::Error, Gambit::Result<std::string>>);
	static_assert(std::is_convertible_v<const char*, Gambit::Result<std::string>>);
}

TEST_CASE("Result: supports move-only values")
{
	Gambit::Result<std::unique_ptr<int>> result = MakeUnique(true);
	REQUIRE(result.HasValue());
	CHECK(**result == 5);

	const std::unique_ptr<int> taken = std::move(result).Value();
	REQUIRE(taken != nullptr);
	CHECK(*taken == 5);

	CHECK_FALSE(MakeUnique(false).HasValue());
}

TEST_CASE("Result: value can be modified in place")
{
	Gambit::Result<std::string> result = std::string("abc");
	result.Value() += "d";
	*result += "e";
	CHECK(*result == "abcde");
}

TEST_CASE("Result: void results report success or an error")
{
	const Gambit::Result<void> success = Validate(true);
	CHECK(success.HasValue());
	CHECK(static_cast<bool>(success));

	const Gambit::Result<void> failure = Validate(false);
	REQUIRE_FALSE(failure.HasValue());
	CHECK(failure.GetError().Message == "validation failed");

	const Gambit::Result<void> defaulted;
	CHECK(defaulted.HasValue());
}

TEST_CASE("Result: only the void specialization is default constructible")
{
	static_assert(!std::is_default_constructible_v<Gambit::Result<int>>);
	static_assert(std::is_default_constructible_v<Gambit::Result<void>>);
}
