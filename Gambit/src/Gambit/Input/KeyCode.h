#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Gambit
{
// Every key the engine knows, numbered compactly from 1 so that the pressed keys of an InputFrame fit a 128-bit set.
// The names are the identifiers used in InputActions.json. The numbers are written into input logs and replays
// (InputFrame serialization): never reorder or remove a key, only append before Count.
// clang-format off
#define GB_KEY_CODES(KEY)                                                                                             \
	KEY(Space) KEY(Apostrophe) KEY(Comma) KEY(Minus) KEY(Period) KEY(Slash) KEY(Semicolon) KEY(Equal)                 \
	KEY(Digit0) KEY(Digit1) KEY(Digit2) KEY(Digit3) KEY(Digit4) KEY(Digit5) KEY(Digit6) KEY(Digit7)                   \
	KEY(Digit8) KEY(Digit9)                                                                                           \
	KEY(A) KEY(B) KEY(C) KEY(D) KEY(E) KEY(F) KEY(G) KEY(H) KEY(I) KEY(J) KEY(K) KEY(L) KEY(M) KEY(N)                 \
	KEY(O) KEY(P) KEY(Q) KEY(R) KEY(S) KEY(T) KEY(U) KEY(V) KEY(W) KEY(X) KEY(Y) KEY(Z)                               \
	KEY(LeftBracket) KEY(Backslash) KEY(RightBracket) KEY(GraveAccent) KEY(World1) KEY(World2)                        \
	KEY(Escape) KEY(Enter) KEY(Tab) KEY(Backspace) KEY(Insert) KEY(Delete) KEY(Right) KEY(Left)                       \
	KEY(Down) KEY(Up)                                                                                                 \
	KEY(PageUp) KEY(PageDown) KEY(Home) KEY(End) KEY(CapsLock) KEY(ScrollLock) KEY(NumLock)                           \
	KEY(PrintScreen) KEY(Pause)                                                                                       \
	KEY(F1) KEY(F2) KEY(F3) KEY(F4) KEY(F5) KEY(F6) KEY(F7) KEY(F8) KEY(F9) KEY(F10) KEY(F11) KEY(F12)                \
	KEY(F13) KEY(F14) KEY(F15) KEY(F16) KEY(F17) KEY(F18) KEY(F19) KEY(F20) KEY(F21) KEY(F22) KEY(F23)                \
	KEY(F24) KEY(F25)                                                                                                 \
	KEY(Keypad0) KEY(Keypad1) KEY(Keypad2) KEY(Keypad3) KEY(Keypad4) KEY(Keypad5) KEY(Keypad6)                        \
	KEY(Keypad7) KEY(Keypad8) KEY(Keypad9)                                                                            \
	KEY(KeypadDecimal) KEY(KeypadDivide) KEY(KeypadMultiply) KEY(KeypadSubtract) KEY(KeypadAdd)                       \
	KEY(KeypadEnter) KEY(KeypadEqual)                                                                                 \
	KEY(LeftShift) KEY(LeftControl) KEY(LeftAlt) KEY(LeftSuper) KEY(RightShift) KEY(RightControl)                     \
	KEY(RightAlt) KEY(RightSuper)                                                                                     \
	KEY(Menu)
	// clang-format on

	enum class KeyCode : uint8_t
	{
		None = 0,
#define GB_KEY_ENUMERATOR(name) name,
		GB_KEY_CODES(GB_KEY_ENUMERATOR)
#undef GB_KEY_ENUMERATOR
		Count
	};

	// Number of key slots in a key set: None plus every key (index = numeric KeyCode).
	inline constexpr size_t KeyCodeCount = static_cast<size_t>(KeyCode::Count);
	static_assert(KeyCodeCount <= 128, "the key set of an InputFrame has 128 bits");

	enum class MouseButton : uint8_t
	{
		Left = 0,
		Right,
		Middle,
		Button4,
		Button5,
		Button6,
		Button7,
		Button8,
		Count
	};
	inline constexpr size_t MouseButtonCount = static_cast<size_t>(MouseButton::Count);

	// Order and numbering match GLFW's gamepad layout (Xbox naming).
	enum class GamepadButton : uint8_t
	{
		A = 0,
		B,
		X,
		Y,
		LeftBumper,
		RightBumper,
		Back,
		Start,
		Guide,
		LeftThumb,
		RightThumb,
		DpadUp,
		DpadRight,
		DpadDown,
		DpadLeft,
		Count
	};
	inline constexpr size_t GamepadButtonCount = static_cast<size_t>(GamepadButton::Count);

	enum class GamepadAxis : uint8_t
	{
		LeftX = 0,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		Count
	};
	inline constexpr size_t GamepadAxisCount = static_cast<size_t>(GamepadAxis::Count);

	// Gamepad slots an InputFrame can hold.
	inline constexpr size_t MaxGamepads = 4;

	// Identifier of the value ("W", "LeftShift", "Left"); empty for out-of-range values.
	[[nodiscard]] std::string_view ToString(KeyCode key) noexcept;
	[[nodiscard]] std::string_view ToString(MouseButton button) noexcept;
	[[nodiscard]] std::string_view ToString(GamepadButton button) noexcept;
	[[nodiscard]] std::string_view ToString(GamepadAxis axis) noexcept;

	// The inverse of ToString (case-sensitive); nullopt for unknown names. KeyCode::None cannot be parsed.
	[[nodiscard]] std::optional<KeyCode> ParseKeyCode(std::string_view name) noexcept;
	[[nodiscard]] std::optional<MouseButton> ParseMouseButton(std::string_view name) noexcept;
	[[nodiscard]] std::optional<GamepadButton> ParseGamepadButton(std::string_view name) noexcept;
	[[nodiscard]] std::optional<GamepadAxis> ParseGamepadAxis(std::string_view name) noexcept;
}
