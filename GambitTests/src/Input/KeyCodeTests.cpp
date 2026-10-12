#include <doctest/doctest.h>

#include "Gambit/Input/KeyCode.h"

#include <set>
#include <string>

#include <GLFW/glfw3.h>

#include "Gambit/Platform/GLFW/GLFWKeyMap.h"

using namespace Gambit;

TEST_CASE("Input: every key has a unique name that parses back")
{
	std::set<std::string> names;
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		const auto key = static_cast<KeyCode>(index);
		const std::string_view name = ToString(key);
		REQUIRE_FALSE(name.empty());
		CHECK(names.insert(std::string(name)).second);
		CHECK(ParseKeyCode(name) == key);
	}
	CHECK(names.size() == KeyCodeCount - 1);
	CHECK(KeyCodeCount <= 128);
}

TEST_CASE("Input: key names are the documented identifiers")
{
	CHECK(ToString(KeyCode::Space) == "Space");
	CHECK(ToString(KeyCode::W) == "W");
	CHECK(ToString(KeyCode::Digit5) == "Digit5");
	CHECK(ToString(KeyCode::F12) == "F12");
	CHECK(ToString(KeyCode::Keypad0) == "Keypad0");
	CHECK(ToString(KeyCode::LeftShift) == "LeftShift");
	CHECK(ToString(KeyCode::Menu) == "Menu");
	CHECK(ToString(KeyCode::None) == "None");
	CHECK(ToString(KeyCode::Count).empty());
	CHECK(ToString(static_cast<KeyCode>(250)).empty());
}

TEST_CASE("Input: key numbering is stable because input logs store it")
{
	// Appending keys is allowed; renumbering would silently corrupt every recorded replay.
	CHECK(static_cast<int>(KeyCode::None) == 0);
	CHECK(static_cast<int>(KeyCode::Space) == 1);
	CHECK(static_cast<int>(KeyCode::A) == 19);
	CHECK(static_cast<int>(KeyCode::Z) == 44);
	CHECK(static_cast<int>(KeyCode::Escape) == 51);
	CHECK(static_cast<int>(KeyCode::F1) == 70);
	CHECK(static_cast<int>(KeyCode::Menu) == 120);
	CHECK(KeyCodeCount == 121);
}

TEST_CASE("Input: parsing key names is exact")
{
	CHECK_FALSE(ParseKeyCode("").has_value());
	CHECK_FALSE(ParseKeyCode("None").has_value());
	CHECK_FALSE(ParseKeyCode("w").has_value()); // case-sensitive
	CHECK_FALSE(ParseKeyCode("Shift").has_value());
	CHECK_FALSE(ParseKeyCode("W ").has_value());
	CHECK_FALSE(ParseKeyCode("Count").has_value());
}

TEST_CASE("Input: mouse button and gamepad names round-trip")
{
	for (size_t index = 0; index < MouseButtonCount; ++index)
	{
		const auto button = static_cast<MouseButton>(index);
		CHECK(ParseMouseButton(ToString(button)) == button);
	}
	CHECK(ToString(MouseButton::Left) == "Left");
	CHECK(ToString(MouseButton::Button8) == "Button8");
	CHECK_FALSE(ParseMouseButton("Button9").has_value());
	CHECK(ToString(MouseButton::Count).empty());

	for (size_t index = 0; index < GamepadButtonCount; ++index)
	{
		const auto button = static_cast<GamepadButton>(index);
		CHECK(ParseGamepadButton(ToString(button)) == button);
	}
	CHECK(ToString(GamepadButton::A) == "A");
	CHECK(ToString(GamepadButton::DpadLeft) == "DpadLeft");
	CHECK_FALSE(ParseGamepadButton("Cross").has_value());

	for (size_t index = 0; index < GamepadAxisCount; ++index)
	{
		const auto axis = static_cast<GamepadAxis>(index);
		CHECK(ParseGamepadAxis(ToString(axis)) == axis);
	}
	CHECK(ToString(GamepadAxis::RightTrigger) == "RightTrigger");
	CHECK_FALSE(ParseGamepadAxis("Trigger").has_value());
}

TEST_CASE("Input: GLFW keys map to the engine's compact codes and back")
{
	for (size_t index = 1; index < KeyCodeCount; ++index)
	{
		const auto key = static_cast<KeyCode>(index);
		const int glfwKey = KeyCodeToGLFW(key);
		CHECK_MESSAGE(glfwKey > 0, "key: ", ToString(key));
		CHECK_MESSAGE(KeyCodeFromGLFW(glfwKey) == key, "key: ", ToString(key));
	}

	CHECK(KeyCodeFromGLFW(GLFW_KEY_A) == KeyCode::A);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_0) == KeyCode::Digit0);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_SPACE) == KeyCode::Space);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_ESCAPE) == KeyCode::Escape);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_F25) == KeyCode::F25);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_KP_ENTER) == KeyCode::KeypadEnter);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_RIGHT_SUPER) == KeyCode::RightSuper);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_MENU) == KeyCode::Menu);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_LAST) == KeyCode::Menu);
	CHECK(KeyCodeToGLFW(KeyCode::W) == GLFW_KEY_W);
	CHECK(KeyCodeToGLFW(KeyCode::LeftControl) == GLFW_KEY_LEFT_CONTROL);
}

TEST_CASE("Input: unknown GLFW keys map to None")
{
	CHECK(KeyCodeFromGLFW(GLFW_KEY_UNKNOWN) == KeyCode::None);
	CHECK(KeyCodeFromGLFW(0) == KeyCode::None);
	CHECK(KeyCodeFromGLFW(31) == KeyCode::None);
	CHECK(KeyCodeFromGLFW(GLFW_KEY_LAST + 1) == KeyCode::None);
	CHECK(KeyCodeFromGLFW(100000) == KeyCode::None);
	CHECK(KeyCodeFromGLFW(-5) == KeyCode::None);
	CHECK(KeyCodeToGLFW(KeyCode::None) == GLFW_KEY_UNKNOWN);
	CHECK(KeyCodeToGLFW(KeyCode::Count) == GLFW_KEY_UNKNOWN);
	CHECK(KeyCodeToGLFW(static_cast<KeyCode>(255)) == GLFW_KEY_UNKNOWN);
}

TEST_CASE("Input: GLFW mouse buttons map one to one")
{
	CHECK(MouseButtonFromGLFW(GLFW_MOUSE_BUTTON_LEFT) == MouseButton::Left);
	CHECK(MouseButtonFromGLFW(GLFW_MOUSE_BUTTON_RIGHT) == MouseButton::Right);
	CHECK(MouseButtonFromGLFW(GLFW_MOUSE_BUTTON_MIDDLE) == MouseButton::Middle);
	CHECK(MouseButtonFromGLFW(GLFW_MOUSE_BUTTON_8) == MouseButton::Button8);
	CHECK_FALSE(MouseButtonFromGLFW(8).has_value());
	CHECK_FALSE(MouseButtonFromGLFW(-1).has_value());
	CHECK(MouseButtonToGLFW(MouseButton::Middle) == GLFW_MOUSE_BUTTON_MIDDLE);
	CHECK(MouseButtonToGLFW(MouseButton::Count) == -1);
}

TEST_CASE("Input: gamepad numbering matches GLFW")
{
	CHECK(static_cast<int>(GamepadButton::A) == GLFW_GAMEPAD_BUTTON_A);
	CHECK(static_cast<int>(GamepadButton::LeftBumper) == GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
	CHECK(static_cast<int>(GamepadButton::Guide) == GLFW_GAMEPAD_BUTTON_GUIDE);
	CHECK(static_cast<int>(GamepadButton::DpadLeft) == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
	CHECK(static_cast<int>(GamepadAxis::LeftX) == GLFW_GAMEPAD_AXIS_LEFT_X);
	CHECK(static_cast<int>(GamepadAxis::RightY) == GLFW_GAMEPAD_AXIS_RIGHT_Y);
	CHECK(static_cast<int>(GamepadAxis::RightTrigger) == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER);
}
