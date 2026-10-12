#pragma once

#include <cstdint>

namespace Gambit
{
	// The duration of one update together with the simulation tick it belongs to. Simulation code reads time only
	// from here (D-011); fixed updates always carry the same duration (1 / SimulationHz).
	class TimeStep
	{
	public:
		constexpr TimeStep() noexcept = default;
		constexpr explicit TimeStep(float seconds, uint64_t tick = 0) noexcept
			: m_Seconds(seconds), m_Tick(tick)
		{}

		[[nodiscard]] constexpr float GetSeconds() const noexcept { return m_Seconds; }
		[[nodiscard]] constexpr float GetMilliseconds() const noexcept { return m_Seconds * 1000.0f; }
		// For a fixed update the number of the tick being executed (the first tick is 1); for a variable-rate update
		// the number of ticks completed so far (0 before the first one).
		[[nodiscard]] constexpr uint64_t GetTick() const noexcept { return m_Tick; }

	private:
		float m_Seconds = 0.0f;
		uint64_t m_Tick = 0;
	};
}
