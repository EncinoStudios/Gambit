#include <doctest/doctest.h>

#include "Gambit/Core/Utf8.h"

#include <string>

using namespace Gambit;

TEST_CASE("Core: UTF-8 validation accepts well-formed text")
{
	CHECK(IsValidUtf8(""));
	CHECK(IsValidUtf8("plain ASCII 123"));
	CHECK(IsValidUtf8(std::string("a\0b", 3))); // NUL is valid UTF-8
	CHECK(IsValidUtf8("\xC3\xA9"));             // U+00E9, two bytes
	CHECK(IsValidUtf8("\xE2\x82\xAC"));         // U+20AC, three bytes
	CHECK(IsValidUtf8("\xF0\x9F\x98\x80"));     // U+1F600, four bytes
	CHECK(IsValidUtf8("\xF4\x8F\xBF\xBF"));     // U+10FFFF, the last code point
	CHECK(IsValidUtf8("\xEF\xBB\xBF"));         // a byte order mark is a code point like any other
}

TEST_CASE("Core: UTF-8 validation rejects malformed text")
{
	CHECK_FALSE(IsValidUtf8("\x80"));                 // a continuation byte alone
	CHECK_FALSE(IsValidUtf8("\xC3"));                 // truncated
	CHECK_FALSE(IsValidUtf8("\xE2\x82"));             // truncated
	CHECK_FALSE(IsValidUtf8("\xF0\x9F\x98"));         // truncated
	CHECK_FALSE(IsValidUtf8("\xC3\x28"));             // bad continuation
	CHECK_FALSE(IsValidUtf8("\xC0\xAF"));             // overlong '/'
	CHECK_FALSE(IsValidUtf8("\xC1\xBF"));             // overlong
	CHECK_FALSE(IsValidUtf8("\xE0\x80\xAF"));         // overlong three-byte form
	CHECK_FALSE(IsValidUtf8("\xF0\x80\x80\xAF"));     // overlong four-byte form
	CHECK_FALSE(IsValidUtf8("\xED\xA0\x80"));         // a surrogate (U+D800)
	CHECK_FALSE(IsValidUtf8("\xF4\x90\x80\x80"));     // beyond U+10FFFF
	CHECK_FALSE(IsValidUtf8("\xF8\x88\x80\x80\x80")); // five-byte form
	CHECK_FALSE(IsValidUtf8("\xFF"));
	CHECK_FALSE(IsValidUtf8("ok then \xFE"));
}
