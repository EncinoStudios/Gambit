#include <doctest/doctest.h>

#include "Gambit/Core/Hash.h"

#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace Gambit;

TEST_CASE("Core: Hash XXH64 known answers")
{
	// Reference values of the xxHash project / Python xxhash.
	CHECK(Hash::XXH64(nullptr, 0, 0) == 0xEF46DB3751D8E999ull);
	CHECK(Hash::XXH64("") == 0xEF46DB3751D8E999ull);
	CHECK(Hash::XXH64("a") == 0xD24EC4F1A98C6E5Bull);
	CHECK(Hash::XXH64("abc") == 0x44BC2CF5AD770999ull);
	CHECK(Hash::XXH64("abc", 12345) == 0x1700E64F6F23509ull);
	CHECK(Hash::XXH64("Nobody inspects the spammish repetition") == 0xFBCEA83C8A378BF1ull);
	CHECK(Hash::XXH64("The quick brown fox jumps over the lazy dog") == 0x0B242D361FDA71BCull);

	std::vector<uint8_t> pattern;
	for (int repeat = 0; repeat < 4; ++repeat)
	{
		for (int value = 0; value < 256; ++value)
		{
			pattern.push_back(static_cast<uint8_t>(value));
		}
	}
	CHECK(Hash::XXH64(pattern.data(), pattern.size(), 0) == 0x6F3914F18FE4DF57ull);
	CHECK(Hash::XXH64(BufferView(pattern.data(), pattern.size())) == 0x6F3914F18FE4DF57ull);
}

TEST_CASE("Core: Hash the seed changes the result")
{
	CHECK(Hash::XXH64("abc", 1) != Hash::XXH64("abc", 2));
	CHECK(Hash::XXH64("abc", 0) == Hash::XXH64("abc"));
}

TEST_CASE("Core: Hash Hasher matches the one-shot hash however the input is split")
{
	std::string data;
	for (int i = 0; i < 1000; ++i)
	{
		data += static_cast<char>('a' + (i * 7) % 26);
	}
	const uint64_t expected = Hash::XXH64(data);

	for (const size_t chunk :
	     { size_t{ 1 }, size_t{ 3 }, size_t{ 31 }, size_t{ 32 }, size_t{ 33 }, size_t{ 500 }, size_t{ 1000 } })
	{
		Hasher hasher;
		for (size_t offset = 0; offset < data.size(); offset += chunk)
		{
			hasher.Update(std::string_view(data).substr(offset, chunk));
		}
		CHECK_MESSAGE(hasher.Digest() == expected, "chunk size ", chunk);
	}

	Hasher empty;
	empty.Update(nullptr, 0);
	empty.Update(std::string_view());
	CHECK(empty.Digest() == Hash::XXH64(""));
}

TEST_CASE("Core: Hash Hasher honors the seed, Reset and copying")
{
	Hasher seeded(77);
	seeded.Update("hello");
	CHECK(seeded.Digest() == Hash::XXH64("hello", 77));
	CHECK(seeded.Digest() == Hash::XXH64("hello", 77)); // Digest does not consume the state

	Hasher fork = seeded; // copy forks the running state
	fork.Update(" world");
	seeded.Update("!");
	CHECK(fork.Digest() == Hash::XXH64("hello world", 77));
	CHECK(seeded.Digest() == Hash::XXH64("hello!", 77));

	seeded.Reset(5);
	seeded.Update("x");
	CHECK(seeded.Digest() == Hash::XXH64("x", 5));
}

TEST_CASE("Core: Hash UpdateValue hashes the little-endian object representation")
{
	Hasher integers;
	integers.UpdateValue(uint32_t{ 1 });
	integers.UpdateValue(uint32_t{ 2 });
	integers.UpdateValue(uint32_t{ 3 });
	CHECK(integers.Digest() == 0xB5148CB100A911FCull);

	Hasher floating;
	floating.UpdateValue(1.5);
	CHECK(floating.Digest() == 0x49F7B96B6B5CCAF9ull);

	enum class Mode : uint8_t
	{
		Off,
		On
	};
	Hasher enums;
	enums.UpdateValue(Mode::On);
	enums.UpdateValue(true);
	const uint8_t bytes[2]{ 1, 1 };
	CHECK(enums.Digest() == Hash::XXH64(bytes, 2, 0));

	// Floats are hashed by bit pattern.
	Hasher zero;
	zero.UpdateValue(0.0f);
	Hasher negativeZero;
	negativeZero.UpdateValue(-0.0f);
	CHECK(zero.Digest() != negativeZero.Digest());
}

TEST_CASE("Core: Hash FNV1a known answers")
{
	CHECK(Hash::FNV1a("") == 0xCBF29CE484222325ull);
	CHECK(Hash::FNV1a("a") == 0xAF63DC4C8601EC8Cull);
	CHECK(Hash::FNV1a("foobar") == 0x85944171F73967E8ull);
	CHECK(Hash::FNV1a("hello") == 0xA430D84680AABD0Bull);
	CHECK(Hash::FNV1a("Transform") == 0xC1FFF4F356DFB2FBull);
}

TEST_CASE("Core: Hash FNV1a and _hash are usable at compile time")
{
	static_assert(Hash::FNV1a("hello") == 0xA430D84680AABD0Bull);
	static_assert("hello"_hash == Hash::FNV1a("hello"));
	static_assert(""_hash == Hash::FNV1aOffsetBasis);

	constexpr uint64_t id = "Transform"_hash;
	int matched = 0;
	switch (id)
	{
		case "Camera"_hash:
			matched = 2;
			break;
		case "Transform"_hash:
			matched = 1;
			break;
		default:
			break;
	}
	CHECK(matched == 1);

	// Embedded NUL and high-bit bytes are hashed too.
	CHECK(Hash::FNV1a(std::string_view("a\0b", 3)) != Hash::FNV1a("a"));
	CHECK(Hash::FNV1a("\xE9") != Hash::FNV1a("e"));
}

TEST_CASE("Core: Hash HashCombine is stable and order dependent")
{
	static_assert(HashCombine(0, 1) == 0x9E3779B97F4A7C16ull);
	CHECK(HashCombine(HashCombine(0, 1), 2) == 0xCD94BF3ECEF65C8Aull);
	CHECK(HashCombine(0x123, 0x456) == 0x9E3779B97F4AC850ull);
	CHECK(HashCombine(HashCombine(0, 1), 2) != HashCombine(HashCombine(0, 2), 1));
}
