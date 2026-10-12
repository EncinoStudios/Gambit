#include <doctest/doctest.h>

#include "Gambit/Core/UUID.h"

#include <algorithm>
#include <limits>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Gambit::UUID;

TEST_CASE("Core: UUID default is invalid and zero")
{
	constexpr UUID none;
	static_assert(none.GetValue() == 0);
	static_assert(!none.IsValid());
	static_assert(UUID(5).IsValid());
	CHECK(none == UUID(0));
}

TEST_CASE("Core: UUID ToString is 0x plus 16 upper-case hex digits")
{
	CHECK(UUID(0).ToString() == "0x0000000000000000");
	CHECK(UUID(0xDEADBEEF).ToString() == "0x00000000DEADBEEF");
	CHECK(UUID(0x1A2B3C4D5E6F7081).ToString() == "0x1A2B3C4D5E6F7081");
	CHECK(UUID(std::numeric_limits<uint64_t>::max()).ToString() == "0xFFFFFFFFFFFFFFFF");
}

TEST_CASE("Core: UUID Parse accepts 1 to 16 hex digits after the prefix")
{
	CHECK(UUID::Parse("0x1").value() == UUID(1));
	CHECK(UUID::Parse("0X1").value() == UUID(1));
	CHECK(UUID::Parse("0xdeadbeef").value() == UUID(0xDEADBEEF));
	CHECK(UUID::Parse("0xDEADbeef").value() == UUID(0xDEADBEEF));
	CHECK(UUID::Parse("0x00000000DEADBEEF").value() == UUID(0xDEADBEEF));
	CHECK(UUID::Parse("0xFFFFFFFFFFFFFFFF").value() == UUID(std::numeric_limits<uint64_t>::max()));
}

TEST_CASE("Core: UUID the null UUID parses, so serialized null references round-trip")
{
	const std::optional<UUID> parsed = UUID::Parse("0x0000000000000000");
	REQUIRE(parsed.has_value());
	CHECK_FALSE(parsed->IsValid());
}

TEST_CASE("Core: UUID Parse rejects malformed text")
{
	for (const char* text : { "", "0", "0x", "0X", "x1", "1234", "deadbeef", "0x12345678901234567", "0xG", "0x12 34",
	                          " 0x1", "0x1 ", "0x-1", "0x+1", "0x0x1", "0x1.0", "-0x1", "0y1", "00x1" })
	{
		CHECK_MESSAGE(!UUID::Parse(text).has_value(), "text: '", text, "'");
	}
	CHECK_FALSE(UUID::Parse(std::string_view(
								"0x1\0"
								"2",
								5))
	                .has_value()); // embedded NUL
}

TEST_CASE("Core: UUID ToString and Parse round-trip")
{
	for (const uint64_t value : { uint64_t{ 0 }, uint64_t{ 1 }, uint64_t{ 0xFF }, uint64_t{ 0x123456789ABCDEF0 },
	                              std::numeric_limits<uint64_t>::max() })
	{
		const UUID original(value);
		CHECK(UUID::Parse(original.ToString()).value() == original);
	}
}

TEST_CASE("Core: UUID Generate returns distinct valid values")
{
	std::unordered_set<UUID> seen;
	for (int i = 0; i < 10000; ++i)
	{
		const UUID generated = UUID::Generate();
		CHECK(generated.IsValid());
		seen.insert(generated);
	}
	CHECK(seen.size() == 10000);
}

TEST_CASE("Core: UUID Generate is safe to call from several threads and never repeats")
{
	constexpr int ThreadCount = 4;
	constexpr int PerThread = 5000;
	std::vector<std::vector<UUID>> results(ThreadCount);
	std::vector<std::thread> threads;
	for (int t = 0; t < ThreadCount; ++t)
	{
		threads.emplace_back([&results, t] {
			results[t].reserve(PerThread);
			for (int i = 0; i < PerThread; ++i)
			{
				results[t].push_back(UUID::Generate());
			}
		});
	}
	for (std::thread& thread : threads)
	{
		thread.join();
	}

	std::unordered_set<UUID> all;
	for (const std::vector<UUID>& list : results)
	{
		CHECK(list.size() == PerThread);
		all.insert(list.begin(), list.end());
	}
	CHECK(all.size() == static_cast<size_t>(ThreadCount * PerThread));
}

TEST_CASE("Core: UUID ordering and hashing")
{
	CHECK(UUID(1) < UUID(2));
	CHECK(UUID(2) >= UUID(2));
	CHECK(UUID(1) != UUID(2));

	std::set<UUID> ordered{ UUID(30), UUID(10), UUID(20) };
	CHECK(*ordered.begin() == UUID(10));

	CHECK(std::hash<UUID>{}(UUID(0x1234)) == std::hash<UUID>{}(UUID(0x1234)));
	std::unordered_map<UUID, int> map;
	map[UUID(7)] = 70;
	map[UUID(8)] = 80;
	CHECK(map.at(UUID(7)) == 70);
	CHECK(map.size() == 2);
}
