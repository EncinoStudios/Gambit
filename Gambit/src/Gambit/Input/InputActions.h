#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Input/KeyCode.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	class VFS;

	// What an action yields: a pressed/released state, one axis, or a 2D vector.
	enum class InputActionType : uint8_t
	{
		Button,
		Axis1D,
		Axis2D
	};

	// The physical source of a binding. Which fields of InputBinding a type uses is listed on the fields.
	enum class InputBindingType : uint8_t
	{
		Key,           // Key; in an Axis1D action it contributes +Scale while down
		MouseButton,   // Button; same
		MouseDelta,    // Axis (Axis1D) or both axes (Axis2D), Scale, Invert / InvertX, InvertY
		Scroll,        // like MouseDelta, for the scroll wheel
		GamepadButton, // PadButton, Gamepad; contributes +Scale in an Axis1D action
		GamepadAxis,   // PadAxis, Gamepad, Deadzone, Scale, Invert; in a Button action Threshold
		GamepadStick,  // Stick, Gamepad, Deadzone, Scale, InvertX, InvertY (Axis2D only)
		Composite1D,   // Key (positive) and NegativeKey, Scale (Axis1D only)
		Composite2D    // Up, Down, Left, Right, Scale (Axis2D only; a diagonal is normalized)
	};

	enum class InputAxis : uint8_t
	{
		X,
		Y
	};

	enum class GamepadStick : uint8_t
	{
		Left,
		Right
	};

	// One way to drive an action. Plain data: the editor and the JSON file edit it directly.
	struct InputBinding
	{
		InputBindingType Type = InputBindingType::Key;
		KeyCode Key = KeyCode::None;
		KeyCode NegativeKey = KeyCode::None;
		KeyCode Up = KeyCode::None;
		KeyCode Down = KeyCode::None;
		KeyCode Left = KeyCode::None;
		KeyCode Right = KeyCode::None;
		MouseButton Button = MouseButton::Left;
		GamepadButton PadButton = GamepadButton::A;
		GamepadAxis PadAxis = GamepadAxis::LeftX;
		GamepadStick Stick = GamepadStick::Left;
		InputAxis Axis = InputAxis::X;
		int Gamepad = -1;       // 0 to MaxGamepads - 1, or -1 for any connected gamepad
		float Scale = 1.0f;     // multiplies the value
		float Deadzone = 0.0f;  // in [0, 1): values below it read as 0, the rest is rescaled to [0, 1]
		float Threshold = 0.5f; // in (0, 1]: from this magnitude on a GamepadAxis counts as a pressed button
		bool Invert = false;    // flips the sign of a one-dimensional value
		bool InvertX = false;
		bool InvertY = false;

		friend bool operator==(const InputBinding&, const InputBinding&) noexcept = default;
	};

	struct InputAction
	{
		std::string Name;
		InputActionType Type = InputActionType::Button;
		std::vector<InputBinding> Bindings;

		friend bool operator==(const InputAction&, const InputAction&) noexcept = default;
	};

	// Index of an action inside its InputActions; cheaper than a name lookup in per-tick code.
	using InputActionId = uint32_t;
	inline constexpr InputActionId InvalidInputAction = ~InputActionId{ 0 };

	// The input mapping of a project (InputActions.json, D-012): named actions with bindings that turn an InputFrame
	// into gameplay input. An action with several bindings takes the value of the binding with the largest magnitude
	// (the first one on ties), so two bindings for the same movement never add up to more than one.
	class InputActions
	{
	public:
		InputActions() = default;
		// Takes a list of actions and checks it like the parser does.
		[[nodiscard]] static Result<InputActions> Create(std::vector<InputAction> actions);

		// Parses the JSON text. All problems are reported with the path of the offending element, e.g.
		// "Actions[2].Bindings[0].Key: unknown key 'Foo'"; unknown properties are errors, so typos do not go unnoticed.
		[[nodiscard]] static Result<InputActions> Parse(std::string_view json);
		[[nodiscard]] static Result<InputActions> Load(const VFS& vfs, std::string_view path);

		// The canonical JSON document (D-025: stable order, 2-space indent, trailing newline). Parse(ToJson()) gives an
		// equal object.
		[[nodiscard]] std::string ToJson() const;

		[[nodiscard]] const std::vector<InputAction>& GetActions() const noexcept { return m_Actions; }
		[[nodiscard]] InputActionId Find(std::string_view name) const noexcept;

		friend bool operator==(const InputActions& a, const InputActions& b) noexcept
		{
			return a.m_Actions == b.m_Actions;
		}

	private:
		explicit InputActions(std::vector<InputAction> actions);

		std::vector<InputAction> m_Actions;
		std::unordered_map<std::string, InputActionId> m_Index;
	};

	inline constexpr uint32_t InputActionsFormatVersion = 1;
}
