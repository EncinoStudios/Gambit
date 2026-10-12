#include "gbpch.h"

#include "Gambit/Core/FixedTimestepClock.h"

#include <limits>

namespace Gambit
{
	namespace
	{
		// Longer frames are cut to this before accumulating, which keeps the arithmetic far from overflow
		// (3.6e12 ns * 1000 Hz fits in 64 bits many times over) without changing the outcome: the surplus is dropped
		// anyway.
		constexpr int64_t MaxFrameNanoseconds = 3'600'000'000'000;
	}

	FixedTimestepClock::FixedTimestepClock(uint32_t hz, uint32_t maxStepsPerFrame) noexcept
		: m_Hz(hz), m_MaxStepsPerFrame(maxStepsPerFrame)
	{
		GB_CORE_ASSERT(hz >= 1 && hz <= MaxSimulationHz, "simulation rate {} Hz is out of range", hz);
		GB_CORE_ASSERT(maxStepsPerFrame >= 1, "at least one simulation step per frame is required");
	}

	uint32_t FixedTimestepClock::Advance(std::chrono::nanoseconds frameTime) noexcept
	{
		const int64_t nanoseconds = std::clamp<int64_t>(frameTime.count(), 0, MaxFrameNanoseconds);
		m_Accumulator += static_cast<uint64_t>(nanoseconds) * m_Hz;

		const uint64_t due = m_Accumulator / UnitsPerTick;
		m_LastDropped = 0;
		if (due > m_MaxStepsPerFrame)
		{
			m_LastDropped = due - m_MaxStepsPerFrame;
			m_TotalDropped += m_LastDropped;
			m_Accumulator %= UnitsPerTick; // keep the partial tick, drop the whole ticks that were not run
			return m_MaxStepsPerFrame;
		}
		m_Accumulator -= due * UnitsPerTick;
		return static_cast<uint32_t>(due);
	}

	float FixedTimestepClock::GetAlpha() const noexcept
	{
		// A partial tick just short of a whole one would round to 1.0f; the largest float below 1 keeps the promise.
		constexpr float BelowOne = 1.0f - std::numeric_limits<float>::epsilon() / 2.0f;
		const auto alpha = static_cast<float>(static_cast<double>(m_Accumulator) / static_cast<double>(UnitsPerTick));
		return std::min(alpha, BelowOne);
	}
}
