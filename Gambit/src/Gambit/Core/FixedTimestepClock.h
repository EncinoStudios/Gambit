#pragma once

#include <chrono>
#include <cstdint>

namespace Gambit
{
	// Turns variable frame durations into a whole number of fixed simulation ticks (D-011) with no drift: the
	// accumulator counts in integer units of 1 / (10^9 * hz) seconds, so any sequence of integer nanosecond frame times
	// yields exactly floor(total * hz / 10^9) ticks. A frame that would need more than MaxStepsPerFrame ticks (a hitch,
	// a debugger pause) is clamped and the surplus time is dropped, so the simulation slows down instead of spiralling.
	class FixedTimestepClock
	{
	public:
		inline static constexpr uint32_t MaxSimulationHz = 1000;

		// Both values must be at least 1; `hz` at most MaxSimulationHz.
		FixedTimestepClock(uint32_t hz, uint32_t maxStepsPerFrame) noexcept;

		// Adds a frame's duration and returns how many ticks are due now (at most MaxStepsPerFrame). Negative durations
		// count as zero.
		[[nodiscard]] uint32_t Advance(std::chrono::nanoseconds frameTime) noexcept;

		// How far time has progressed between the last executed tick and the next one, in [0, 1).
		[[nodiscard]] float GetAlpha() const noexcept;

		// Whether the last Advance had to drop ticks, and how many were dropped in all.
		[[nodiscard]] bool WasClamped() const noexcept { return m_LastDropped > 0; }
		[[nodiscard]] uint64_t GetLastDroppedTicks() const noexcept { return m_LastDropped; }
		[[nodiscard]] uint64_t GetTotalDroppedTicks() const noexcept { return m_TotalDropped; }

		[[nodiscard]] uint32_t GetHz() const noexcept { return m_Hz; }
		[[nodiscard]] uint32_t GetMaxStepsPerFrame() const noexcept { return m_MaxStepsPerFrame; }
		// Seconds per tick, as the simulation sees it.
		[[nodiscard]] float GetStepSeconds() const noexcept { return 1.0f / static_cast<float>(m_Hz); }

		// Forgets the accumulated time (not the drop statistics).
		void Reset() noexcept { m_Accumulator = 0; }

	private:
		// One tick is this many accumulator units; a frame adds nanoseconds * hz.
		static constexpr uint64_t UnitsPerTick = 1'000'000'000;

		uint32_t m_Hz;
		uint32_t m_MaxStepsPerFrame;
		uint64_t m_Accumulator = 0;
		uint64_t m_LastDropped = 0;
		uint64_t m_TotalDropped = 0;
	};
}
