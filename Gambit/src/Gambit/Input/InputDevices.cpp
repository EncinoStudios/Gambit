#include "gbpch.h"

#include "Gambit/Input/InputDevices.h"

#include "Gambit/Core/Events/KeyEvent.h"
#include "Gambit/Core/Events/MouseEvent.h"

namespace Gambit
{
	namespace
	{
		int32_t SaturateToInt32(int64_t value) noexcept
		{
			return static_cast<int32_t>(
				std::clamp<int64_t>(value, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
		}

		// Fixed point like QuantizeToFixedPoint, but 64 bits wide: the position of a locked cursor is unbounded, and
		// its movement must still register once the position stored in the frame has saturated.
		int64_t ToWideFixedPoint(double value) noexcept
		{
			constexpr double Limit = 4.0e18; // differences of two such values fit in int64
			const double scaled = value * static_cast<double>(InputFixedPointScale);
			if (scaled != scaled)
			{
				return 0;
			}
			return static_cast<int64_t>(std::floor(std::clamp(scaled, -Limit, Limit) + 0.5));
		}
	}

	void InputDevices::OnEvent(const Event& event)
	{
		switch (event.GetEventType())
		{
			case EventType::KeyPressed:
			{
				const KeyCode key = static_cast<const KeyPressedEvent&>(event).GetKeyCode();
				const auto index = static_cast<size_t>(key);
				if (index > 0 && index < KeyCodeCount)
				{
					m_PressedSinceSample[index >> 6] |= uint64_t{ 1 } << (index & 63);
				}
				break;
			}
			case EventType::MouseButtonPressed:
				m_MouseButtonsPressedSinceSample |= static_cast<uint8_t>(
					1u << static_cast<unsigned>(static_cast<const MouseButtonPressedEvent&>(event).GetMouseButton()));
				break;
			case EventType::MouseScrolled:
			{
				const auto& scrolled = static_cast<const MouseScrolledEvent&>(event);
				m_ScrollX += QuantizeToFixedPoint(scrolled.GetOffsetX());
				m_ScrollY += QuantizeToFixedPoint(scrolled.GetOffsetY());
				break;
			}
			case EventType::KeyTyped:
			{
				// Only valid text is kept: a recorded frame must deserialize again (InputFrame::Read checks it).
				const uint32_t codepoint = static_cast<const KeyTypedEvent&>(event).GetCodepoint();
				if (m_TextCount < MaxTextCodepointsPerFrame && IsValidTextCodepoint(codepoint))
				{
					m_Text[m_TextCount++] = codepoint;
				}
				break;
			}
			default:
				break;
		}
	}

	InputFrame InputDevices::Sample()
	{
		InputFrame frame;

		for (size_t index = 1; index < KeyCodeCount; ++index)
		{
			const auto key = static_cast<KeyCode>(index);
			const bool latched = (m_PressedSinceSample[index >> 6] & (uint64_t{ 1 } << (index & 63))) != 0;
			frame.SetKey(key, latched || m_State.IsKeyDown(key));
		}
		for (size_t index = 0; index < MouseButtonCount; ++index)
		{
			const auto button = static_cast<MouseButton>(index);
			const bool latched = (m_MouseButtonsPressedSinceSample & (1u << index)) != 0;
			frame.SetMouseButton(button, latched || m_State.IsMouseButtonDown(button));
		}

		const glm::dvec2 cursor = m_State.GetCursorPosition();
		frame.MouseX = QuantizeToFixedPoint(cursor.x);
		frame.MouseY = QuantizeToFixedPoint(cursor.y);
		const int64_t mouseX = ToWideFixedPoint(cursor.x);
		const int64_t mouseY = ToWideFixedPoint(cursor.y);
		const CursorMode mode = m_State.GetCursorMode();
		// The first sample, and the first one after the cursor was captured or released, has no meaningful movement:
		// the position jumps when the mode changes.
		if (m_HasPreviousMouse && mode == m_PreviousCursorMode)
		{
			frame.MouseDeltaX = SaturateToInt32(mouseX - m_PreviousMouseX);
			frame.MouseDeltaY = SaturateToInt32(mouseY - m_PreviousMouseY);
		}
		m_HasPreviousMouse = true;
		m_PreviousCursorMode = mode;
		m_PreviousMouseX = mouseX;
		m_PreviousMouseY = mouseY;

		frame.ScrollX = SaturateToInt32(m_ScrollX);
		frame.ScrollY = SaturateToInt32(m_ScrollY);

		for (uint32_t slot = 0; slot < MaxGamepads; ++slot)
		{
			GamepadState state;
			if (!m_State.GetGamepadState(slot, state))
			{
				continue;
			}
			GamepadFrame& gamepad = frame.Gamepads[slot];
			gamepad.Connected = true;
			for (size_t button = 0; button < GamepadButtonCount; ++button)
			{
				gamepad.SetButton(static_cast<GamepadButton>(button), state.Buttons[button]);
			}
			for (size_t axis = 0; axis < GamepadAxisCount; ++axis)
			{
				gamepad.Axes[axis] = QuantizeGamepadAxis(state.Axes[axis]);
			}
		}

		frame.TextCount = m_TextCount;
		frame.Text = m_Text;

		m_PressedSinceSample = {};
		m_MouseButtonsPressedSinceSample = 0;
		m_ScrollX = 0;
		m_ScrollY = 0;
		m_Text = {};
		m_TextCount = 0;
		return frame;
	}
}
