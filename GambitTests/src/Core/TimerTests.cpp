#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Log.h"
#include "Gambit/Core/LogSinks.h"
#include "Gambit/Core/TimeStep.h"
#include "Gambit/Core/Timer.h"

#include <chrono>
#include <thread>

using namespace Gambit;

TEST_CASE("Core: Timer measures elapsed time and can be reset")
{
	Timer timer;
	CHECK(timer.ElapsedSeconds() >= 0.0);

	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	const double seconds = timer.ElapsedSeconds();
	CHECK(seconds >= 0.015);
	CHECK(seconds < 5.0);
	CHECK(timer.ElapsedMilliseconds() >= seconds * 1000.0);

	timer.Reset();
	CHECK(timer.ElapsedSeconds() < seconds);
}

TEST_CASE("Core: ScopedTimer logs its name and duration to the core logger on destruction")
{
	LogConfig config = Tests::MakeTestLogConfig();
	config.Console = false;
	config.RingBufferCapacity = 16;
	Tests::ScopedLogInit logInit(config);
	const Ref<RingBufferSink> ring = Log::GetRingBuffer();
	REQUIRE(ring != nullptr);

	{
		ScopedTimer timer("Load scene");
		CHECK(ring->Snapshot().empty()); // nothing is logged before the scope ends
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
	{
		ScopedTimer quiet("Noisy detail", LogLevel::Trace);
	}

	const std::vector<LogEntry> entries = ring->Snapshot();
	REQUIRE(entries.size() == 2);
	CHECK(entries[0].LoggerName == "GAMBIT");
	CHECK(entries[0].Level == LogLevel::Info);
	CHECK(entries[0].Message.starts_with("Load scene: "));
	CHECK(entries[0].Message.ends_with(" ms"));
	CHECK(entries[1].Level == LogLevel::Trace);
	CHECK(entries[1].Message.starts_with("Noisy detail: "));
}

TEST_CASE("Core: TimeStep carries seconds and the tick")
{
	constexpr TimeStep step(0.016f, 42);
	static_assert(step.GetSeconds() == 0.016f);
	static_assert(step.GetTick() == 42);
	CHECK(step.GetMilliseconds() == doctest::Approx(16.0f));

	constexpr TimeStep untimed;
	static_assert(untimed.GetSeconds() == 0.0f);
	static_assert(untimed.GetTick() == 0);

	CHECK(TimeStep(1.0f).GetMilliseconds() == 1000.0f);
}
