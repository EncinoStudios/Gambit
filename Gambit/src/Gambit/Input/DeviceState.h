#pragma once

#include "Gambit/Input/KeyCode.h"
#include "Gambit/Math/Math.h"

#include <array>
#include <cstdint>

namespace Gambit
{
	enum class CursorMode : uint8_t
	{
		Normal, // visible and free
		Hidden, // invisible while over the window, but free
		Locked  // invisible and captured: the position is unbounded, only the movement matters (first person look)
	};

	// One gamepad as the platform reports it, already in the engine's layout: sticks in [-1, 1], triggers in [0, 1].
	struct GamepadState
	{
		std::array<bool, GamepadButtonCount> Buttons{};
		std::array<float, GamepadAxisCount> Axes{};
	};

	// What the platform can tell about the input devices at this moment (polling). InputDevices turns it into
	// InputFrames; the Window implements it, tests implement it with scripted values.
	class IDeviceState
	{
	public:
		virtual ~IDeviceState() = default;

		[[nodiscard]] virtual bool IsKeyDown(KeyCode key) const = 0;
		[[nodiscard]] virtual bool IsMouseButtonDown(MouseButton button) const = 0;
		// In window coordinates; unbounded while the cursor is locked.
		[[nodiscard]] virtual glm::dvec2 GetCursorPosition() const = 0;
		[[nodiscard]] virtual CursorMode GetCursorMode() const = 0;
		// False when no gamepad is connected in `slot` (0 to MaxGamepads - 1).
		[[nodiscard]] virtual bool GetGamepadState(uint32_t slot, GamepadState& state) const = 0;
	};
}
