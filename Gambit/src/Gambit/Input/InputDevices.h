#pragma once

#include "Gambit/Core/Events/Event.h"
#include "Gambit/Input/DeviceState.h"
#include "Gambit/Input/InputFrame.h"

namespace Gambit
{
	// Turns the live devices into InputFrames: polls the current state of keys, mouse and gamepads from an
	// IDeviceState and collects what polling cannot see (scrolling, typed text, taps shorter than a tick) from the
	// events the window reports. This is the non-deterministic edge of the input system: everything after Sample()
	// is plain data (D-012).
	class InputDevices
	{
	public:
		// `state` must outlive the InputDevices.
		explicit InputDevices(const IDeviceState& state) noexcept
			: m_State(state)
		{}

		// Feed every event of the window here (Application does); the event is not modified or marked handled.
		void OnEvent(const Event& event);

		// The input since the previous Sample: current key, mouse and gamepad state, the movement of the cursor, the
		// scrolling and typed text accumulated meanwhile. Keys and buttons pressed and released within the interval
		// still read as down once.
		[[nodiscard]] InputFrame Sample();

	private:
		const IDeviceState& m_State;
		std::array<uint64_t, 2> m_PressedSinceSample{};
		uint8_t m_MouseButtonsPressedSinceSample = 0;
		int64_t m_ScrollX = 0; // fixed point, accumulated from events
		int64_t m_ScrollY = 0;
		std::array<uint32_t, MaxTextCodepointsPerFrame> m_Text{};
		uint8_t m_TextCount = 0;
		bool m_HasPreviousMouse = false;
		CursorMode m_PreviousCursorMode = CursorMode::Normal;
		int64_t m_PreviousMouseX = 0; // fixed point, not saturated to the int32 range of a frame
		int64_t m_PreviousMouseY = 0;
	};
}
