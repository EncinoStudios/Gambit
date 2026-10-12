#include <doctest/doctest.h>

#include "Gambit/Core/Events/GamepadEvent.h"
#include "Gambit/Core/Events/KeyEvent.h"
#include "Gambit/Core/Events/MouseEvent.h"
#include "Gambit/Core/Events/WindowEvent.h"
#include "Gambit/Input/InputDevices.h"
#include "Gambit/Input/InputSource.h"

#include <array>
#include <limits>
#include <optional>
#include <set>

using namespace Gambit;

namespace
{
	// Device state that the test scripts.
	class FakeDeviceState final : public IDeviceState
	{
	public:
		[[nodiscard]] bool IsKeyDown(KeyCode key) const override { return Keys.contains(key); }
		[[nodiscard]] bool IsMouseButtonDown(MouseButton button) const override { return Buttons.contains(button); }
		[[nodiscard]] glm::dvec2 GetCursorPosition() const override { return Cursor; }
		[[nodiscard]] CursorMode GetCursorMode() const override { return Mode; }
		[[nodiscard]] bool GetGamepadState(uint32_t slot, GamepadState& state) const override
		{
			if (slot >= Pads.size() || !Pads[slot].has_value())
			{
				return false;
			}
			state = *Pads[slot];
			return true;
		}

		std::set<KeyCode> Keys;
		std::set<MouseButton> Buttons;
		glm::dvec2 Cursor{ 0.0 };
		CursorMode Mode = CursorMode::Normal;
		std::array<std::optional<GamepadState>, 4> Pads;
	};
}

TEST_CASE("Input: InputDevices samples the polled state")
{
	FakeDeviceState state;
	InputDevices devices(state);
	CHECK(devices.Sample() == InputFrame{});

	state.Keys = { KeyCode::W, KeyCode::LeftShift, KeyCode::Menu };
	state.Buttons = { MouseButton::Left, MouseButton::Button8 };
	state.Cursor = { 640.5, 360.25 };
	const InputFrame frame = devices.Sample();
	CHECK(frame.IsKeyDown(KeyCode::W));
	CHECK(frame.IsKeyDown(KeyCode::LeftShift));
	CHECK(frame.IsKeyDown(KeyCode::Menu));
	CHECK_FALSE(frame.IsKeyDown(KeyCode::A));
	CHECK(frame.IsMouseButtonDown(MouseButton::Left));
	CHECK(frame.IsMouseButtonDown(MouseButton::Button8));
	CHECK_FALSE(frame.IsMouseButtonDown(MouseButton::Right));
	CHECK(frame.MouseX == 640 * 64 + 32);
	CHECK(frame.MouseY == 360 * 64 + 16);

	state.Keys.clear();
	state.Buttons.clear();
	CHECK(devices.Sample().Keys == std::array<uint64_t, 2>{});
}

TEST_CASE("Input: InputDevices reports the cursor movement between samples")
{
	FakeDeviceState state;
	InputDevices devices(state);

	state.Cursor = { 100.0, 50.0 };
	InputFrame frame = devices.Sample();
	CHECK(frame.MouseDeltaX == 0); // nothing to compare with yet
	CHECK(frame.MouseDeltaY == 0);

	state.Cursor = { 103.5, 49.0 };
	frame = devices.Sample();
	CHECK(frame.MouseDeltaX == 224); // 3.5 px
	CHECK(frame.MouseDeltaY == -64);
	CHECK(frame.MouseX == 103 * 64 + 32);

	frame = devices.Sample(); // no movement
	CHECK(frame.MouseDeltaX == 0);
	CHECK(frame.MouseDeltaY == 0);
}

TEST_CASE("Input: InputDevices ignores the position jump when the cursor mode changes")
{
	FakeDeviceState state;
	InputDevices devices(state);
	state.Cursor = { 100.0, 100.0 };
	static_cast<void>(devices.Sample());

	state.Mode = CursorMode::Locked;
	state.Cursor = { 0.0, 0.0 }; // capturing the cursor resets the virtual position
	InputFrame frame = devices.Sample();
	CHECK(frame.MouseDeltaX == 0);
	CHECK(frame.MouseDeltaY == 0);

	state.Cursor = { 2.0, -3.0 }; // while locked the movement counts again, with unbounded positions
	frame = devices.Sample();
	CHECK(frame.MouseDeltaX == 128);
	CHECK(frame.MouseDeltaY == -192);

	state.Mode = CursorMode::Hidden;
	state.Cursor = { 500.0, 500.0 };
	frame = devices.Sample();
	CHECK(frame.MouseDeltaX == 0);
}

TEST_CASE("Input: InputDevices saturates absurd cursor positions")
{
	FakeDeviceState state;
	InputDevices devices(state);
	state.Cursor = { 1.0e15, -1.0e15 };
	static_cast<void>(devices.Sample());
	state.Cursor = { -1.0e15, 1.0e15 };
	const InputFrame frame = devices.Sample();
	CHECK(frame.MouseX == std::numeric_limits<int32_t>::min());
	CHECK(frame.MouseY == std::numeric_limits<int32_t>::max());
	CHECK(frame.MouseDeltaX == std::numeric_limits<int32_t>::min()); // clamped difference of two saturated values
	CHECK(frame.MouseDeltaY == std::numeric_limits<int32_t>::max());
}

TEST_CASE("Input: InputDevices keeps reporting the movement of a locked cursor beyond the stored range")
{
	// A locked cursor's virtual position grows without bound; past about 33.5 million pixels the position stored in
	// the frame saturates, but the movement must not stop.
	FakeDeviceState state;
	state.Mode = CursorMode::Locked;
	InputDevices devices(state);
	state.Cursor = { 4.0e7, -4.0e7 };
	static_cast<void>(devices.Sample());
	state.Cursor = { 4.0e7 + 2.5, -4.0e7 - 1.0 };
	const InputFrame frame = devices.Sample();
	CHECK(frame.MouseX == std::numeric_limits<int32_t>::max());
	CHECK(frame.MouseY == std::numeric_limits<int32_t>::min());
	CHECK(frame.MouseDeltaX == 160);
	CHECK(frame.MouseDeltaY == -64);
}

TEST_CASE("Input: InputDevices keeps keys and buttons that were pressed and released between samples")
{
	FakeDeviceState state;
	InputDevices devices(state);

	KeyPressedEvent keyDown(KeyCode::Space);
	devices.OnEvent(keyDown);
	MouseButtonPressedEvent buttonDown(MouseButton::Middle);
	devices.OnEvent(buttonDown);
	// Both are already up again when the tick samples the devices.
	InputFrame frame = devices.Sample();
	CHECK(frame.IsKeyDown(KeyCode::Space));
	CHECK(frame.IsMouseButtonDown(MouseButton::Middle));

	frame = devices.Sample(); // the latch lasts one tick
	CHECK_FALSE(frame.IsKeyDown(KeyCode::Space));
	CHECK_FALSE(frame.IsMouseButtonDown(MouseButton::Middle));

	// A release event alone does not press anything, and an unknown key is ignored.
	KeyReleasedEvent keyUp(KeyCode::A);
	devices.OnEvent(keyUp);
	KeyPressedEvent unknown(KeyCode::None);
	devices.OnEvent(unknown);
	CHECK(devices.Sample() == InputFrame{});
	CHECK_FALSE(keyDown.Handled); // events pass through untouched
}

TEST_CASE("Input: InputDevices accumulates scrolling until the next sample")
{
	FakeDeviceState state;
	InputDevices devices(state);
	for (const float offset : { 1.0f, 1.0f, -0.25f })
	{
		MouseScrolledEvent event(0.5f * offset, offset);
		devices.OnEvent(event);
	}
	InputFrame frame = devices.Sample();
	CHECK(frame.ScrollY == 112); // 1.75 steps
	CHECK(frame.ScrollX == 56);

	frame = devices.Sample();
	CHECK(frame.ScrollX == 0);
	CHECK(frame.ScrollY == 0);

	MouseScrolledEvent huge(0.0f, 1.0e30f);
	devices.OnEvent(huge);
	devices.OnEvent(huge);
	CHECK(devices.Sample().ScrollY == std::numeric_limits<int32_t>::max());
}

TEST_CASE("Input: InputDevices collects typed text in order and drops the overflow")
{
	FakeDeviceState state;
	InputDevices devices(state);
	for (uint32_t i = 0; i < 20; ++i)
	{
		KeyTypedEvent event('a' + i);
		devices.OnEvent(event);
	}
	InputFrame frame = devices.Sample();
	REQUIRE(frame.TextCount == MaxTextCodepointsPerFrame);
	for (uint32_t i = 0; i < MaxTextCodepointsPerFrame; ++i)
	{
		CHECK(frame.Text[i] == 'a' + i);
	}

	frame = devices.Sample();
	CHECK(frame.TextCount == 0);
	KeyTypedEvent last(0x1F600);
	devices.OnEvent(last);
	frame = devices.Sample();
	REQUIRE(frame.TextCount == 1);
	CHECK(frame.Text[0] == 0x1F600);

	// Code points that are not text never reach a frame, so a recorded frame always deserializes again.
	for (const uint32_t invalid : { 0u, 0xD800u, 0xDFFFu, 0x110000u, 0xFFFFFFFFu })
	{
		KeyTypedEvent event(invalid);
		devices.OnEvent(event);
	}
	KeyTypedEvent valid('x');
	devices.OnEvent(valid);
	frame = devices.Sample();
	REQUIRE(frame.TextCount == 1);
	CHECK(frame.Text[0] == 'x');
	const Result<InputFrame> roundTrip = InputFrame::Deserialize(frame.Serialize().View());
	REQUIRE(roundTrip.HasValue());
	CHECK(*roundTrip == frame);
}

TEST_CASE("Input: InputDevices quantizes gamepad state")
{
	FakeDeviceState state;
	InputDevices devices(state);
	GamepadState pad;
	pad.Buttons[static_cast<size_t>(GamepadButton::A)] = true;
	pad.Buttons[static_cast<size_t>(GamepadButton::DpadLeft)] = true;
	pad.Axes[static_cast<size_t>(GamepadAxis::LeftX)] = -1.0f;
	pad.Axes[static_cast<size_t>(GamepadAxis::LeftY)] = 0.5f;
	pad.Axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = 1.0f;
	state.Pads[2] = pad;

	const InputFrame frame = devices.Sample();
	CHECK_FALSE(frame.Gamepads[0].Connected);
	CHECK_FALSE(frame.Gamepads[1].Connected);
	CHECK(frame.Gamepads[3].Connected == false);
	const GamepadFrame& sampled = frame.Gamepads[2];
	REQUIRE(sampled.Connected);
	CHECK(sampled.IsButtonDown(GamepadButton::A));
	CHECK(sampled.IsButtonDown(GamepadButton::DpadLeft));
	CHECK_FALSE(sampled.IsButtonDown(GamepadButton::B));
	CHECK(sampled.Axes[static_cast<size_t>(GamepadAxis::LeftX)] == -32767);
	CHECK(sampled.Axes[static_cast<size_t>(GamepadAxis::LeftY)] == 16384);
	CHECK(sampled.Axes[static_cast<size_t>(GamepadAxis::RightTrigger)] == 32767);
	CHECK(sampled.Axes[static_cast<size_t>(GamepadAxis::RightY)] == 0);

	// What a device produces is always a frame that can be stored in a log.
	CHECK(InputFrame::Deserialize(frame.Serialize()).HasValue());

	// Out-of-range platform readings are clamped to the range of the axis, so they can still be recorded.
	state.Pads[2]->Axes[static_cast<size_t>(GamepadAxis::LeftTrigger)] = -0.5f;
	state.Pads[2]->Axes[static_cast<size_t>(GamepadAxis::RightX)] = -1.5f;
	const InputFrame clamped = devices.Sample();
	CHECK(clamped.Gamepads[2].Axes[static_cast<size_t>(GamepadAxis::LeftTrigger)] == 0);
	CHECK(clamped.Gamepads[2].Axes[static_cast<size_t>(GamepadAxis::RightX)] == -32767);
	CHECK(InputFrame::Deserialize(clamped.Serialize()).HasValue());

	state.Pads[2].reset(); // unplugged
	CHECK_FALSE(devices.Sample().Gamepads[2].Connected);
}

TEST_CASE("Input: InputDevices ignores events it does not collect")
{
	FakeDeviceState state;
	InputDevices devices(state);
	WindowCloseEvent close;
	WindowResizeEvent resize(1, 1);
	MouseMovedEvent moved(5.0f, 5.0f);
	GamepadConnectedEvent connected(0, "Pad");
	KeyReleasedEvent released(KeyCode::A);
	for (Event* event : std::initializer_list<Event*>{ &close, &resize, &moved, &connected, &released })
	{
		devices.OnEvent(*event);
	}
	CHECK(devices.Sample() == InputFrame{});
}

TEST_CASE("Input: DeviceInputSource samples the devices on every tick")
{
	FakeDeviceState state;
	InputDevices devices(state);
	DeviceInputSource source(devices);
	CHECK_FALSE(source.IsFinished());

	state.Keys = { KeyCode::E };
	CHECK(source.Sample().IsKeyDown(KeyCode::E));
	state.Keys.clear();
	CHECK_FALSE(source.Sample().IsKeyDown(KeyCode::E));
	CHECK_FALSE(source.IsFinished()); // live input never ends
}
