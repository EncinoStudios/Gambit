#pragma once

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Input/InputFrame.h"
#include "Gambit/Input/InputLog.h"

#include <cstddef>

namespace Gambit
{
	class InputDevices;

	// Where the input of each simulation tick comes from. The Application asks its source once per fixed tick, so a
	// live session (DeviceInputSource) and a replay (ReplayInputSource) drive the simulation through the same path.
	class InputSource
	{
	public:
		virtual ~InputSource() = default;

		// The input of the next tick.
		[[nodiscard]] virtual InputFrame Sample() = 0;
		// True when the source has no more input (a replay at its end); live sources never finish.
		[[nodiscard]] virtual bool IsFinished() const noexcept { return false; }
	};

	// Samples the devices: non-deterministic by nature, so a recording of its frames (InputLog) is what makes a session
	// reproducible.
	class DeviceInputSource final : public InputSource
	{
	public:
		// `devices` must outlive the source.
		explicit DeviceInputSource(InputDevices& devices) noexcept
			: m_Devices(devices)
		{}

		[[nodiscard]] InputFrame Sample() override;

	private:
		InputDevices& m_Devices;
	};

	// Plays an InputLog back, one frame per tick. After the last frame it yields empty frames (nothing pressed) and
	// reports IsFinished().
	class ReplayInputSource final : public InputSource
	{
	public:
		explicit ReplayInputSource(Ref<const InputLog> log)
			: m_Log(std::move(log))
		{
			GB_CORE_ASSERT(m_Log != nullptr, "a replay needs an input log");
		}

		[[nodiscard]] InputFrame Sample() override;
		[[nodiscard]] bool IsFinished() const noexcept override { return m_Position >= m_Log->GetFrameCount(); }

		// Index of the frame the next Sample returns.
		[[nodiscard]] size_t GetPosition() const noexcept { return m_Position; }
		[[nodiscard]] const InputLog& GetLog() const noexcept { return *m_Log; }

	private:
		Ref<const InputLog> m_Log;
		size_t m_Position = 0;
	};
}
