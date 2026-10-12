#include <doctest/doctest.h>

#include "Gambit/Core/FixedTimestepClock.h"

#include <chrono>
#include <cstdint>
#include <limits>

using namespace Gambit;
using std::chrono::nanoseconds;

namespace
{
	nanoseconds Ms(int64_t milliseconds)
	{
		return nanoseconds(milliseconds * 1'000'000);
	}
}

TEST_CASE("Core: FixedTimestepClock turns frame times into exact tick counts")
{
	FixedTimestepClock clock(50, 8); // 20 ms per tick
	CHECK(clock.Advance(Ms(5)) == 0);
	CHECK(clock.Advance(Ms(5)) == 0);
	CHECK(clock.Advance(Ms(5)) == 0);
	CHECK(clock.Advance(Ms(5)) == 1);
	CHECK(clock.GetAlpha() == 0.0f);
	CHECK(clock.Advance(Ms(45)) == 2);
	CHECK(clock.GetAlpha() == doctest::Approx(0.25f));
	CHECK(clock.Advance(Ms(15)) == 1);
	CHECK(clock.GetAlpha() == 0.0f);
	CHECK_FALSE(clock.WasClamped());
	CHECK(clock.GetStepSeconds() == 0.02f);
	CHECK(clock.GetHz() == 50);
	CHECK(clock.GetMaxStepsPerFrame() == 8);
}

TEST_CASE("Core: FixedTimestepClock handles tick lengths that are not whole nanoseconds")
{
	// 60 Hz is 16.666... ms: the accumulator counts in exact fractions, so 40 frames of 25 ms are exactly 60 ticks.
	FixedTimestepClock clock(60, 8);
	uint64_t ticks = 0;
	for (int frame = 1; frame <= 40; ++frame)
	{
		ticks += clock.Advance(Ms(25));
		CHECK(ticks == static_cast<uint64_t>(frame * 3 / 2)); // 1.5 ticks per frame, never rounded early or late
	}
	CHECK(ticks == 60);
	CHECK(clock.GetAlpha() == 0.0f);

	// Three frames of 50 ms are exactly nine ticks.
	FixedTimestepClock exact(60, 8);
	CHECK(exact.Advance(Ms(50)) == 3);
	CHECK(exact.GetAlpha() == 0.0f);
}

TEST_CASE("Core: FixedTimestepClock does not drift over millions of frames")
{
	FixedTimestepClock clock(60, 64);
	uint64_t seed = 12345;
	uint64_t totalNanoseconds = 0;
	uint64_t ticks = 0;
	for (int frame = 0; frame < 1'000'000; ++frame)
	{
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		const int64_t duration = 1'000'000 + static_cast<int64_t>((seed >> 33) % 40'000'000); // 1 to 41 ms
		totalNanoseconds += static_cast<uint64_t>(duration);
		ticks += clock.Advance(nanoseconds(duration));
		REQUIRE_FALSE(clock.WasClamped());
	}
	CHECK(ticks == totalNanoseconds * 60 / 1'000'000'000ull); // exactly floor(time * rate)
}

TEST_CASE("Core: FixedTimestepClock clamps a long frame and drops the surplus")
{
	FixedTimestepClock clock(60, 8);
	CHECK(clock.Advance(nanoseconds(1'000'000'000)) == 8); // a one second hitch is worth 60 ticks
	CHECK(clock.WasClamped());
	CHECK(clock.GetLastDroppedTicks() == 52);
	CHECK(clock.GetTotalDroppedTicks() == 52);
	CHECK(clock.GetAlpha() < 1.0f);

	// The next ordinary frame is not affected by the dropped time.
	CHECK(clock.Advance(Ms(17)) == 1);
	CHECK_FALSE(clock.WasClamped());
	CHECK(clock.GetLastDroppedTicks() == 0);
	CHECK(clock.GetTotalDroppedTicks() == 52);
}

TEST_CASE("Core: FixedTimestepClock keeps the partial tick when it clamps")
{
	FixedTimestepClock clock(50, 4);    // 20 ms per tick
	CHECK(clock.Advance(Ms(210)) == 4); // 10.5 ticks
	CHECK(clock.GetLastDroppedTicks() == 6);
	CHECK(clock.GetAlpha() == doctest::Approx(0.5f));
	CHECK(clock.Advance(Ms(10)) == 1); // the half tick that was kept completes
	CHECK_FALSE(clock.WasClamped());

	CHECK(clock.Advance(Ms(80)) == 4); // exactly the limit is not a clamp
	CHECK_FALSE(clock.WasClamped());
	CHECK(clock.Advance(Ms(100)) == 4);
	CHECK(clock.WasClamped());
	CHECK(clock.GetLastDroppedTicks() == 1);
	CHECK(clock.GetTotalDroppedTicks() == 7);
}

TEST_CASE("Core: FixedTimestepClock ignores negative durations and survives absurd ones")
{
	FixedTimestepClock clock(60, 8);
	CHECK(clock.Advance(nanoseconds(-5'000'000'000)) == 0);
	CHECK(clock.GetAlpha() == 0.0f);

	CHECK(clock.Advance(nanoseconds(std::numeric_limits<int64_t>::max())) == 8);
	CHECK(clock.WasClamped());
	CHECK(clock.GetAlpha() >= 0.0f);
	CHECK(clock.GetAlpha() < 1.0f);
	CHECK(clock.Advance(Ms(17)) == 1);
}

TEST_CASE("Core: FixedTimestepClock supports the whole range of rates")
{
	FixedTimestepClock slow(1, 8);
	CHECK(slow.Advance(Ms(999)) == 0);
	CHECK(slow.Advance(Ms(1)) == 1);
	CHECK(slow.GetStepSeconds() == 1.0f);
	CHECK(slow.Advance(nanoseconds(999'999'999)) == 0);
	CHECK(slow.GetAlpha() < 1.0f); // 0.999999999 would round to 1.0f
	CHECK(slow.GetAlpha() > 0.9999f);

	FixedTimestepClock fast(FixedTimestepClock::MaxSimulationHz, 8);
	CHECK(fast.Advance(Ms(1)) == 1);
	CHECK(fast.Advance(Ms(3)) == 3);
	CHECK(fast.GetStepSeconds() == 0.001f);

	FixedTimestepClock single(60, 1);
	CHECK(single.Advance(Ms(50)) == 1);
	CHECK(single.GetLastDroppedTicks() == 2);
}

TEST_CASE("Core: FixedTimestepClock Reset forgets the accumulated time but not the statistics")
{
	FixedTimestepClock clock(50, 2);
	CHECK(clock.Advance(Ms(110)) == 2);
	CHECK(clock.GetTotalDroppedTicks() == 3);
	CHECK(clock.GetAlpha() == doctest::Approx(0.5f));
	clock.Reset();
	CHECK(clock.GetAlpha() == 0.0f);
	CHECK(clock.GetTotalDroppedTicks() == 3);
	CHECK(clock.Advance(Ms(10)) == 0);
}
