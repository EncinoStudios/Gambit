#pragma once

#include "Gambit/Core/Log.h"

#include <chrono>
#include <string>
#include <string_view>

namespace Gambit
{
	// Wall-clock stopwatch (monotonic) for profiling and tooling. Never use it in simulation code (D-011).
	class Timer
	{
	public:
		Timer() noexcept { Reset(); }

		void Reset() noexcept { m_Start = Clock::now(); }

		[[nodiscard]] double ElapsedSeconds() const noexcept
		{
			return std::chrono::duration<double>(Clock::now() - m_Start).count();
		}

		[[nodiscard]] double ElapsedMilliseconds() const noexcept { return ElapsedSeconds() * 1000.0; }

	private:
		using Clock = std::chrono::steady_clock;
		Clock::time_point m_Start;
	};

	// Logs "<name>: <ms> ms" to the core logger when it goes out of scope.
	class ScopedTimer
	{
	public:
		explicit ScopedTimer(std::string_view name, LogLevel level = LogLevel::Info)
			: m_Name(name), m_Level(level)
		{}
		~ScopedTimer();
		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;

	private:
		std::string m_Name;
		LogLevel m_Level;
		Timer m_Timer;
	};
}
