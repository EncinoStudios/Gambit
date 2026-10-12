#include <doctest/doctest.h>

#include "Gambit/Core/Hash.h"
#include "Gambit/Input/InputState.h"

#include <cmath>
#include <vector>

using namespace Gambit;

namespace
{
	Ref<const InputActions> ParseActions(std::string_view json)
	{
		const Result<InputActions> actions = InputActions::Parse(json);
		const std::string problem = actions.HasValue() ? std::string() : actions.GetError().Message;
		REQUIRE_MESSAGE(actions.HasValue(), problem);
		return CreateRef<InputActions>(*actions);
	}

	// The same mapping as the InputActions tests: buttons, a 1D axis with several sources, WASD / stick movement.
	Ref<const InputActions> GameActions()
	{
		return ParseActions(R"({
			"Version": 1,
			"Actions": [
				{ "Name": "Jump", "Type": "Button", "Bindings": [
					{ "Type": "Key", "Key": "Space" },
					{ "Type": "GamepadButton", "Button": "A" },
					{ "Type": "MouseButton", "Button": "Right" },
					{ "Type": "GamepadAxis", "Axis": "RightTrigger", "Threshold": 0.4 } ] },
				{ "Name": "Throttle", "Type": "Axis1D", "Bindings": [
					{ "Type": "Composite1D", "Positive": "W", "Negative": "S" },
					{ "Type": "GamepadAxis", "Axis": "LeftY", "Deadzone": 0.2, "Invert": true },
					{ "Type": "Key", "Key": "LeftShift", "Scale": 2 } ] },
				{ "Name": "Move", "Type": "Axis2D", "Bindings": [
					{ "Type": "Composite2D", "Up": "W", "Down": "S", "Left": "A", "Right": "D" },
					{ "Type": "GamepadStick", "Stick": "Left", "Deadzone": 0.15, "InvertY": true } ] },
				{ "Name": "Look", "Type": "Axis2D", "Bindings": [
					{ "Type": "MouseDelta", "Scale": 0.1, "InvertY": true },
					{ "Type": "GamepadStick", "Stick": "Right", "Gamepad": 1, "Scale": 3 } ] },
				{ "Name": "Zoom", "Type": "Axis1D", "Bindings": [
					{ "Type": "Scroll", "Axis": "Y", "Scale": 0.5 },
					{ "Type": "MouseDelta", "Axis": "X", "Invert": true } ] }
			]
		})");
	}

	InputFrame Keys(std::initializer_list<KeyCode> keys)
	{
		InputFrame frame;
		for (const KeyCode key : keys)
		{
			frame.SetKey(key, true);
		}
		return frame;
	}

	int16_t Axis(float value)
	{
		return QuantizeGamepadAxis(value);
	}

	GamepadFrame& Connect(InputFrame& frame, size_t slot)
	{
		frame.Gamepads[slot].Connected = true;
		return frame.Gamepads[slot];
	}
}

TEST_CASE("Input: InputState reports button edges across ticks")
{
	InputState state(GameActions());
	const InputActionId jump = state.Find("Jump");
	REQUIRE(jump != InvalidInputAction);

	state.Update(InputFrame{});
	CHECK_FALSE(state.IsDown(jump));
	CHECK_FALSE(state.WasPressed(jump));
	CHECK_FALSE(state.WasReleased(jump));

	state.Update(Keys({ KeyCode::Space }));
	CHECK(state.IsDown(jump));
	CHECK(state.WasPressed(jump));
	CHECK_FALSE(state.WasReleased(jump));

	state.Update(Keys({ KeyCode::Space }));
	CHECK(state.IsDown(jump));
	CHECK_FALSE(state.WasPressed(jump)); // held, not newly pressed
	CHECK_FALSE(state.WasReleased(jump));

	state.Update(InputFrame{});
	CHECK_FALSE(state.IsDown(jump));
	CHECK_FALSE(state.WasPressed(jump));
	CHECK(state.WasReleased(jump));

	state.Update(InputFrame{});
	CHECK_FALSE(state.WasReleased(jump)); // one tick only
}

TEST_CASE("Input: InputState looks actions up by name or id")
{
	InputState state(GameActions());
	state.Update(Keys({ KeyCode::Space }));
	CHECK(state.IsDown("Jump"));
	CHECK(state.WasPressed("Jump"));
	CHECK_FALSE(state.WasReleased("Jump"));
	CHECK(state.GetAxis("Jump") == 1.0f);
	CHECK(state.Find("Jump") == 0);
	CHECK(state.Find("Throttle") == 1);

	// Unknown actions and a state without actions read as inactive instead of failing.
	CHECK_FALSE(state.IsDown("Fly"));
	CHECK_FALSE(state.WasPressed("Fly"));
	CHECK_FALSE(state.WasReleased("Fly"));
	CHECK(state.GetAxis("Fly") == 0.0f);
	CHECK(state.GetAxis2D("Fly") == glm::vec2(0.0f));
	CHECK_FALSE(state.IsDown(InvalidInputAction));
	CHECK_FALSE(state.IsDown(InputActionId{ 99 }));

	InputState empty;
	empty.Update(Keys({ KeyCode::Space }));
	CHECK_FALSE(empty.IsDown("Jump"));
	CHECK(empty.Find("Jump") == InvalidInputAction);
	CHECK(empty.GetFrame().IsKeyDown(KeyCode::Space));
}

TEST_CASE("Input: InputState Update and Reset manage the two frames")
{
	InputState state(GameActions());
	state.Update(Keys({ KeyCode::A }));
	state.Update(Keys({ KeyCode::B }));
	CHECK(state.GetFrame().IsKeyDown(KeyCode::B));
	CHECK(state.GetPreviousFrame().IsKeyDown(KeyCode::A));
	state.Reset();
	CHECK(state.GetFrame() == InputFrame{});
	CHECK(state.GetPreviousFrame() == InputFrame{});
	CHECK(state.GetActions() != nullptr); // the mapping stays

	state.SetActions(nullptr);
	CHECK(state.GetActions() == nullptr);
}

TEST_CASE("Input: button actions combine keys, mouse buttons and gamepads")
{
	InputState state(GameActions());

	InputFrame mouse;
	mouse.SetMouseButton(MouseButton::Right, true);
	state.Update(mouse);
	CHECK(state.IsDown("Jump"));

	InputFrame pad;
	Connect(pad, 2).SetButton(GamepadButton::A, true); // "any gamepad": slot 2 counts
	state.Update(pad);
	CHECK(state.IsDown("Jump"));

	InputFrame otherButton;
	Connect(otherButton, 0).SetButton(GamepadButton::B, true);
	state.Update(otherButton);
	CHECK_FALSE(state.IsDown("Jump"));

	InputFrame disconnected; // state of a gamepad that is not connected is ignored
	disconnected.Gamepads[1].SetButton(GamepadButton::A, true);
	state.Update(disconnected);
	CHECK_FALSE(state.IsDown("Jump"));
}

TEST_CASE("Input: a gamepad axis acts as a button from its threshold on")
{
	InputState state(GameActions());
	InputFrame frame;
	Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = Axis(0.39f);
	state.Update(frame);
	CHECK_FALSE(state.IsDown("Jump"));
	frame.Gamepads[0].Axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = Axis(0.41f);
	state.Update(frame);
	CHECK(state.IsDown("Jump"));
	CHECK(state.WasPressed("Jump"));
}

TEST_CASE("Input: keys drive a one-dimensional axis through a composite")
{
	InputState state(GameActions());
	state.Update(Keys({ KeyCode::W }));
	CHECK(state.GetAxis("Throttle") == 1.0f);
	CHECK(state.IsDown("Throttle"));
	CHECK(state.WasPressed("Throttle"));

	state.Update(Keys({ KeyCode::S }));
	CHECK(state.GetAxis("Throttle") == -1.0f);
	CHECK(state.IsDown("Throttle"));
	CHECK_FALSE(state.WasPressed("Throttle")); // still actuated

	state.Update(Keys({ KeyCode::W, KeyCode::S }));
	CHECK(state.GetAxis("Throttle") == 0.0f); // the keys cancel out
	CHECK_FALSE(state.IsDown("Throttle"));
	CHECK(state.WasReleased("Throttle"));

	state.Update(Keys({ KeyCode::LeftShift }));
	CHECK(state.GetAxis("Throttle") == 2.0f); // a single key with Scale 2
}

TEST_CASE("Input: an action with several bindings takes the strongest one")
{
	InputState state(GameActions());
	state.Update(Keys({ KeyCode::W, KeyCode::LeftShift }));
	CHECK(state.GetAxis("Throttle") == 2.0f); // 2 beats 1; they do not add up to 3

	InputFrame frame = Keys({ KeyCode::W });
	Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::LeftY)] = Axis(-1.0f); // inverted stick: +1
	state.Update(frame);
	CHECK(state.GetAxis("Throttle") == 1.0f);

	state.Update(Keys({ KeyCode::W, KeyCode::D }));
	CHECK(state.GetAxis2D("Move") == glm::vec2(1.0f / 1.41421356f)); // W and D, normalized
}

TEST_CASE("Input: a gamepad axis applies its deadzone, inversion and scale")
{
	InputState state(GameActions());
	const auto throttleWith = [&](float stick) {
		InputFrame frame;
		Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::LeftY)] = Axis(stick);
		state.Update(frame);
		return state.GetAxis("Throttle");
	};

	CHECK(throttleWith(0.0f) == 0.0f);
	CHECK(throttleWith(0.1f) == 0.0f);                                 // inside the deadzone of 0.2
	CHECK(throttleWith(-0.2f) == 0.0f);                                // on the edge
	CHECK(throttleWith(0.6f) == doctest::Approx(-0.5f).epsilon(1e-3)); // (0.6 - 0.2) / 0.8, inverted
	CHECK(throttleWith(-0.6f) == doctest::Approx(0.5f).epsilon(1e-3));
	CHECK(throttleWith(1.0f) == doctest::Approx(-1.0f));
	CHECK(throttleWith(-1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("Input: WASD composite moves in four directions and normalizes diagonals")
{
	InputState state(GameActions());
	const auto move = [&](std::initializer_list<KeyCode> keys) {
		state.Update(Keys(keys));
		return state.GetAxis2D("Move");
	};

	CHECK(move({}) == glm::vec2(0.0f));
	CHECK(move({ KeyCode::W }) == glm::vec2(0.0f, 1.0f));
	CHECK(move({ KeyCode::S }) == glm::vec2(0.0f, -1.0f));
	CHECK(move({ KeyCode::A }) == glm::vec2(-1.0f, 0.0f));
	CHECK(move({ KeyCode::D }) == glm::vec2(1.0f, 0.0f));
	CHECK(move({ KeyCode::W, KeyCode::S }) == glm::vec2(0.0f));
	CHECK(move({ KeyCode::A, KeyCode::D }) == glm::vec2(0.0f));
	CHECK(move({ KeyCode::W, KeyCode::A, KeyCode::D }) == glm::vec2(0.0f, 1.0f));

	const glm::vec2 diagonal = move({ KeyCode::W, KeyCode::D });
	CHECK(diagonal.x == doctest::Approx(0.70710678f).epsilon(1e-6));
	CHECK(diagonal.y == doctest::Approx(0.70710678f).epsilon(1e-6));
	CHECK(glm::length(diagonal) == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(state.GetAxis("Move") == doctest::Approx(1.0f).epsilon(1e-6)); // GetAxis of a 2D action is its length
	CHECK(state.IsDown("Move"));

	const glm::vec2 opposite = move({ KeyCode::S, KeyCode::A });
	CHECK(opposite.x == doctest::Approx(-0.70710678f).epsilon(1e-6));
	CHECK(opposite.y == doctest::Approx(-0.70710678f).epsilon(1e-6));
}

TEST_CASE("Input: a stick has a radial deadzone and keeps its direction")
{
	InputState state(GameActions());
	const auto stick = [&](float x, float y) {
		InputFrame frame;
		GamepadFrame& pad = Connect(frame, 0);
		pad.Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(x);
		pad.Axes[static_cast<size_t>(GamepadAxis::LeftY)] = Axis(y);
		state.Update(frame);
		return state.GetAxis2D("Move");
	};

	CHECK(stick(0.0f, 0.0f) == glm::vec2(0.0f));
	CHECK(stick(0.1f, 0.1f) == glm::vec2(0.0f)); // length 0.14 < 0.15
	CHECK(stick(0.14f, 0.0f) == glm::vec2(0.0f));

	const glm::vec2 right = stick(0.5f, 0.0f); // (0.5 - 0.15) / 0.85
	CHECK(right.x == doctest::Approx(0.41176f).epsilon(1e-3));
	CHECK(right.y == 0.0f);

	const glm::vec2 inverted = stick(0.0f, 0.5f); // InvertY: pushing the stick "up" (+0.5 as reported) yields -y
	CHECK(inverted.x == 0.0f);
	CHECK(inverted.y == doctest::Approx(-0.41176f).epsilon(1e-3));

	const glm::vec2 diagonal = stick(0.6f, 0.6f);
	CHECK(diagonal.x == doctest::Approx(-diagonal.y).epsilon(1e-6)); // symmetric: the cross-shaped deadzone is avoided
	CHECK(glm::length(diagonal) <= 1.0f);

	const glm::vec2 full = stick(1.0f, 1.0f); // beyond the unit circle is clamped to length 1
	CHECK(glm::length(full) == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("Input: keyboard and stick compete by magnitude")
{
	InputState state(GameActions());
	InputFrame frame = Keys({ KeyCode::D });
	Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(-0.6f);
	state.Update(frame);
	CHECK(state.GetAxis2D("Move") == glm::vec2(1.0f, 0.0f)); // the key (length 1) beats the stick (length 0.53)

	frame = Keys({});
	Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(-0.6f);
	state.Update(frame);
	CHECK(state.GetAxis2D("Move").x == doctest::Approx(-0.52941f).epsilon(1e-3));
}

TEST_CASE("Input: mouse delta and scroll are scaled from fixed point")
{
	InputState state(GameActions());
	InputFrame frame;
	frame.MouseDeltaX = 640;  // 10 px
	frame.MouseDeltaY = -320; // -5 px
	state.Update(frame);
	const glm::vec2 look = state.GetAxis2D("Look");
	CHECK(look.x == doctest::Approx(1.0f));
	CHECK(look.y == doctest::Approx(0.5f));                  // Scale 0.1 and InvertY: -(-5 * 0.1)
	CHECK(state.GetAxis("Zoom") == doctest::Approx(-10.0f)); // mouse X, inverted; the scroll binding is 0

	frame = InputFrame{};
	frame.ScrollY = 128; // two notches
	state.Update(frame);
	CHECK(state.GetAxis("Zoom") == doctest::Approx(1.0f)); // Scale 0.5

	frame.ScrollY = -64;
	state.Update(frame);
	CHECK(state.GetAxis("Zoom") == doctest::Approx(-0.5f));

	frame = InputFrame{};
	frame.MouseDeltaX = 1; // the smallest step: 1/64 px
	state.Update(frame);
	CHECK(state.GetAxis("Zoom") == doctest::Approx(-1.0f / 64.0f));
}

TEST_CASE("Input: a binding reads only its own gamepad")
{
	InputState state(GameActions());
	const auto look = [&](size_t slot) {
		InputFrame frame;
		Connect(frame, slot).Axes[static_cast<size_t>(GamepadAxis::RightX)] = Axis(1.0f);
		state.Update(frame);
		return state.GetAxis2D("Look");
	};

	CHECK(look(0) == glm::vec2(0.0f)); // the Look stick binding is bound to Gamepad 1
	CHECK(look(2) == glm::vec2(0.0f));
	CHECK(look(1).x == doctest::Approx(3.0f)); // Scale 3
}

TEST_CASE("Input: any-gamepad bindings pick the strongest connected pad")
{
	InputState state(GameActions());
	InputFrame frame;
	Connect(frame, 0).Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(0.4f);
	Connect(frame, 3).Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(-0.9f);
	frame.Gamepads[1].Axes[static_cast<size_t>(GamepadAxis::LeftX)] = Axis(1.0f); // not connected: ignored
	state.Update(frame);
	CHECK(state.GetAxis2D("Move").x == doctest::Approx(-(0.9f - 0.15f) / 0.85f).epsilon(1e-3));
}

TEST_CASE("Input: evaluation is platform-independent")
{
	// Pseudo-random frames through every action; the digest of all values pins the float arithmetic.
	InputState state(GameActions());
	uint64_t seed = 99;
	const auto next = [&seed] {
		seed = seed * 6364136223846793005ull + 1442695040888963407ull;
		return static_cast<uint32_t>(seed >> 32);
	};

	Hasher hasher;
	for (int tick = 0; tick < 500; ++tick)
	{
		InputFrame frame;
		const uint32_t bits = next();
		frame.SetKey(KeyCode::W, (bits & 1) != 0);
		frame.SetKey(KeyCode::A, (bits & 2) != 0);
		frame.SetKey(KeyCode::S, (bits & 4) != 0);
		frame.SetKey(KeyCode::D, (bits & 8) != 0);
		frame.SetKey(KeyCode::Space, (bits & 16) != 0);
		frame.SetKey(KeyCode::LeftShift, (bits & 32) != 0);
		frame.MouseDeltaX = static_cast<int32_t>(next() % 4001) - 2000;
		frame.MouseDeltaY = static_cast<int32_t>(next() % 4001) - 2000;
		frame.ScrollY = static_cast<int32_t>(next() % 257) - 128;
		for (size_t slot = 0; slot < 2; ++slot)
		{
			GamepadFrame& pad = Connect(frame, slot);
			for (int16_t& axis : pad.Axes)
			{
				axis = static_cast<int16_t>(static_cast<int32_t>(next() % 65535) - 32767);
			}
		}
		state.Update(frame);
		for (const char* action : { "Jump", "Throttle", "Move", "Look", "Zoom" })
		{
			const glm::vec2 value = state.GetAxis2D(action);
			hasher.UpdateValue(value.x);
			hasher.UpdateValue(value.y);
			hasher.UpdateValue(state.GetAxis(action));
			hasher.UpdateValue(state.WasPressed(action));
			hasher.UpdateValue(state.WasReleased(action));
		}
	}
	CHECK(hasher.Digest() == 0xEA7EDE11D4D0A2B6ULL);
}
