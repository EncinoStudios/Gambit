#pragma once

#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Input/KeyCode.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace Gambit
{
	class BinaryReader;
	class BinaryWriter;

	// Mouse position, mouse delta and scroll are stored in 1/64 units (pixels, scroll steps): fixed point, so a frame
	// is plain integers that compare and hash bit for bit.
	inline constexpr int32_t InputFixedPointScale = 64;
	// Gamepad axes are stored as int16 in [-32767, 32767] (triggers [0, 32767]).
	inline constexpr int16_t GamepadAxisMaximum = 32767;
	// Characters typed within one tick; later ones are dropped.
	inline constexpr size_t MaxTextCodepointsPerFrame = 16;

	// Converts a device reading to fixed point, saturating instead of overflowing; NaN becomes 0.
	[[nodiscard]] int32_t QuantizeToFixedPoint(double value) noexcept;
	// Converts a gamepad stick axis in [-1, 1] to int16 in [-GamepadAxisMaximum, GamepadAxisMaximum], saturating;
	// NaN becomes 0.
	[[nodiscard]] int16_t QuantizeGamepadAxis(float value) noexcept;
	// Converts a trigger in [0, 1] to int16 in [0, GamepadAxisMaximum], saturating; NaN becomes 0.
	[[nodiscard]] int16_t QuantizeGamepadTrigger(float value) noexcept;

	// Whether the axis is a trigger (range [0, 1]) rather than a stick axis (range [-1, 1]).
	[[nodiscard]] constexpr bool IsGamepadTrigger(GamepadAxis axis) noexcept
	{
		return axis == GamepadAxis::LeftTrigger || axis == GamepadAxis::RightTrigger;
	}

	// Whether a code point may be stored as typed text: a Unicode scalar value other than U+0000 (no surrogates,
	// nothing beyond U+10FFFF).
	[[nodiscard]] constexpr bool IsValidTextCodepoint(uint32_t codepoint) noexcept
	{
		return codepoint != 0 && codepoint <= 0x10FFFF && (codepoint < 0xD800 || codepoint > 0xDFFF);
	}

	// The state of one gamepad at a tick.
	struct GamepadFrame
	{
		bool Connected = false;
		uint16_t Buttons = 0; // bit n = GamepadButton n
		std::array<int16_t, GamepadAxisCount> Axes{};

		[[nodiscard]] bool IsButtonDown(GamepadButton button) const noexcept
		{
			return (Buttons & (1u << static_cast<unsigned>(button))) != 0;
		}

		void SetButton(GamepadButton button, bool down) noexcept
		{
			const auto bit = static_cast<uint16_t>(1u << static_cast<unsigned>(button));
			Buttons = down ? static_cast<uint16_t>(Buttons | bit) : static_cast<uint16_t>(Buttons & ~bit);
		}

		// The axis as a float in [-1, 1] (triggers [0, 1]); a division by a constant, so identical everywhere.
		[[nodiscard]] float GetAxis(GamepadAxis axis) const noexcept
		{
			return std::clamp(
				static_cast<float>(Axes[static_cast<size_t>(axis)]) / static_cast<float>(GamepadAxisMaximum), -1.0f,
				1.0f);
		}

		friend bool operator==(const GamepadFrame&, const GamepadFrame&) noexcept = default;
	};

	// Everything the player did during one simulation tick (D-012): plain data sampled once per tick from the devices
	// (or read from an input log), the only way input reaches the simulation. Gameplay reads actions through
	// InputState, never the frame directly.
	struct InputFrame
	{
		std::array<uint64_t, 2> Keys{}; // bit n = KeyCode n
		uint8_t MouseButtons = 0;       // bit n = MouseButton n
		int32_t MouseX = 0;             // position in window coordinates
		int32_t MouseY = 0;
		int32_t MouseDeltaX = 0; // movement since the previous tick
		int32_t MouseDeltaY = 0;
		int32_t ScrollX = 0; // accumulated since the previous tick
		int32_t ScrollY = 0;
		std::array<GamepadFrame, MaxGamepads> Gamepads{};
		uint8_t TextCount = 0;                                  // valid entries of Text
		std::array<uint32_t, MaxTextCodepointsPerFrame> Text{}; // Unicode code points typed this tick, in order

		[[nodiscard]] bool IsKeyDown(KeyCode key) const noexcept
		{
			const auto index = static_cast<size_t>(key);
			return index < KeyCodeCount && (Keys[index >> 6] & (uint64_t{ 1 } << (index & 63))) != 0;
		}

		// KeyCode::None and out-of-range keys are ignored.
		void SetKey(KeyCode key, bool down) noexcept
		{
			const auto index = static_cast<size_t>(key);
			if (index == 0 || index >= KeyCodeCount)
			{
				return;
			}
			const uint64_t bit = uint64_t{ 1 } << (index & 63);
			if (down)
			{
				Keys[index >> 6] |= bit;
			}
			else
			{
				Keys[index >> 6] &= ~bit;
			}
		}

		[[nodiscard]] bool IsMouseButtonDown(MouseButton button) const noexcept
		{
			return (MouseButtons & (1u << static_cast<unsigned>(button))) != 0;
		}

		void SetMouseButton(MouseButton button, bool down) noexcept
		{
			const auto bit = static_cast<uint8_t>(1u << static_cast<unsigned>(button));
			MouseButtons = down ? static_cast<uint8_t>(MouseButtons | bit) : static_cast<uint8_t>(MouseButtons & ~bit);
		}

		// Appends a typed code point; false (and nothing stored) when the frame already holds
		// MaxTextCodepointsPerFrame of them or the code point is not valid text (IsValidTextCodepoint).
		bool AddText(uint32_t codepoint) noexcept
		{
			if (TextCount >= MaxTextCodepointsPerFrame || !IsValidTextCodepoint(codepoint))
			{
				return false;
			}
			Text[TextCount++] = codepoint;
			return true;
		}

		// Whether the frame holds only values a device can produce: no key bits beyond the last key (or KeyCode::None),
		// at most MaxTextCodepointsPerFrame valid code points, no state on a disconnected gamepad, no gamepad button
		// bits beyond the last button, stick axes in [-GamepadAxisMaximum, GamepadAxisMaximum] and triggers in
		// [0, GamepadAxisMaximum]. Every other field may hold any value of its type.
		[[nodiscard]] Result<void> Validate() const;

		// Binary form: a versioned header ("GBIF", format version) and the fixed little-endian layout of the frame.
		// Frames inside an InputLog use the same layout without the header.
		[[nodiscard]] Buffer Serialize() const;
		// Rejects bad magic or version, truncated or trailing data, and frames that fail Validate().
		[[nodiscard]] static Result<InputFrame> Deserialize(BufferView bytes);

		void Write(BinaryWriter& writer) const;
		[[nodiscard]] static Result<InputFrame> Read(BinaryReader& reader);

		// Compares everything that is stored; text entries beyond TextCount are ignored.
		friend bool operator==(const InputFrame& a, const InputFrame& b) noexcept
		{
			return a.Keys == b.Keys && a.MouseButtons == b.MouseButtons && a.MouseX == b.MouseX && a.MouseY == b.MouseY
				&& a.MouseDeltaX == b.MouseDeltaX && a.MouseDeltaY == b.MouseDeltaY && a.ScrollX == b.ScrollX
				&& a.ScrollY == b.ScrollY && a.Gamepads == b.Gamepads && a.TextCount == b.TextCount
				&& std::equal(a.Text.begin(), a.Text.begin() + a.TextCount, b.Text.begin());
		}
	};

	// Version of the frame layout; part of the InputFrame header and of the InputLog header.
	inline constexpr uint16_t InputFrameFormatVersion = 1;
}
