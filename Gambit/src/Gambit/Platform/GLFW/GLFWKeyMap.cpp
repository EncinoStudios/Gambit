#include "gbpch.h"

#include "Gambit/Platform/GLFW/GLFWKeyMap.h"

#include <GLFW/glfw3.h>

namespace Gambit
{
	namespace
	{
		struct KeyMapping
		{
			KeyCode Key;
			int GLFWKey;
		};

		// In KeyCode order, so that the entry of key k is at index k - 1.
		// clang-format off
		constexpr std::array<KeyMapping, KeyCodeCount - 1> KeyMappings = {{
		{ KeyCode::Space, GLFW_KEY_SPACE },
		{ KeyCode::Apostrophe, GLFW_KEY_APOSTROPHE },
		{ KeyCode::Comma, GLFW_KEY_COMMA },
		{ KeyCode::Minus, GLFW_KEY_MINUS },
		{ KeyCode::Period, GLFW_KEY_PERIOD },
		{ KeyCode::Slash, GLFW_KEY_SLASH },
		{ KeyCode::Semicolon, GLFW_KEY_SEMICOLON },
		{ KeyCode::Equal, GLFW_KEY_EQUAL },
		{ KeyCode::Digit0, GLFW_KEY_0 },
		{ KeyCode::Digit1, GLFW_KEY_1 },
		{ KeyCode::Digit2, GLFW_KEY_2 },
		{ KeyCode::Digit3, GLFW_KEY_3 },
		{ KeyCode::Digit4, GLFW_KEY_4 },
		{ KeyCode::Digit5, GLFW_KEY_5 },
		{ KeyCode::Digit6, GLFW_KEY_6 },
		{ KeyCode::Digit7, GLFW_KEY_7 },
		{ KeyCode::Digit8, GLFW_KEY_8 },
		{ KeyCode::Digit9, GLFW_KEY_9 },
		{ KeyCode::A, GLFW_KEY_A },
		{ KeyCode::B, GLFW_KEY_B },
		{ KeyCode::C, GLFW_KEY_C },
		{ KeyCode::D, GLFW_KEY_D },
		{ KeyCode::E, GLFW_KEY_E },
		{ KeyCode::F, GLFW_KEY_F },
		{ KeyCode::G, GLFW_KEY_G },
		{ KeyCode::H, GLFW_KEY_H },
		{ KeyCode::I, GLFW_KEY_I },
		{ KeyCode::J, GLFW_KEY_J },
		{ KeyCode::K, GLFW_KEY_K },
		{ KeyCode::L, GLFW_KEY_L },
		{ KeyCode::M, GLFW_KEY_M },
		{ KeyCode::N, GLFW_KEY_N },
		{ KeyCode::O, GLFW_KEY_O },
		{ KeyCode::P, GLFW_KEY_P },
		{ KeyCode::Q, GLFW_KEY_Q },
		{ KeyCode::R, GLFW_KEY_R },
		{ KeyCode::S, GLFW_KEY_S },
		{ KeyCode::T, GLFW_KEY_T },
		{ KeyCode::U, GLFW_KEY_U },
		{ KeyCode::V, GLFW_KEY_V },
		{ KeyCode::W, GLFW_KEY_W },
		{ KeyCode::X, GLFW_KEY_X },
		{ KeyCode::Y, GLFW_KEY_Y },
		{ KeyCode::Z, GLFW_KEY_Z },
		{ KeyCode::LeftBracket, GLFW_KEY_LEFT_BRACKET },
		{ KeyCode::Backslash, GLFW_KEY_BACKSLASH },
		{ KeyCode::RightBracket, GLFW_KEY_RIGHT_BRACKET },
		{ KeyCode::GraveAccent, GLFW_KEY_GRAVE_ACCENT },
		{ KeyCode::World1, GLFW_KEY_WORLD_1 },
		{ KeyCode::World2, GLFW_KEY_WORLD_2 },
		{ KeyCode::Escape, GLFW_KEY_ESCAPE },
		{ KeyCode::Enter, GLFW_KEY_ENTER },
		{ KeyCode::Tab, GLFW_KEY_TAB },
		{ KeyCode::Backspace, GLFW_KEY_BACKSPACE },
		{ KeyCode::Insert, GLFW_KEY_INSERT },
		{ KeyCode::Delete, GLFW_KEY_DELETE },
		{ KeyCode::Right, GLFW_KEY_RIGHT },
		{ KeyCode::Left, GLFW_KEY_LEFT },
		{ KeyCode::Down, GLFW_KEY_DOWN },
		{ KeyCode::Up, GLFW_KEY_UP },
		{ KeyCode::PageUp, GLFW_KEY_PAGE_UP },
		{ KeyCode::PageDown, GLFW_KEY_PAGE_DOWN },
		{ KeyCode::Home, GLFW_KEY_HOME },
		{ KeyCode::End, GLFW_KEY_END },
		{ KeyCode::CapsLock, GLFW_KEY_CAPS_LOCK },
		{ KeyCode::ScrollLock, GLFW_KEY_SCROLL_LOCK },
		{ KeyCode::NumLock, GLFW_KEY_NUM_LOCK },
		{ KeyCode::PrintScreen, GLFW_KEY_PRINT_SCREEN },
		{ KeyCode::Pause, GLFW_KEY_PAUSE },
		{ KeyCode::F1, GLFW_KEY_F1 },
		{ KeyCode::F2, GLFW_KEY_F2 },
		{ KeyCode::F3, GLFW_KEY_F3 },
		{ KeyCode::F4, GLFW_KEY_F4 },
		{ KeyCode::F5, GLFW_KEY_F5 },
		{ KeyCode::F6, GLFW_KEY_F6 },
		{ KeyCode::F7, GLFW_KEY_F7 },
		{ KeyCode::F8, GLFW_KEY_F8 },
		{ KeyCode::F9, GLFW_KEY_F9 },
		{ KeyCode::F10, GLFW_KEY_F10 },
		{ KeyCode::F11, GLFW_KEY_F11 },
		{ KeyCode::F12, GLFW_KEY_F12 },
		{ KeyCode::F13, GLFW_KEY_F13 },
		{ KeyCode::F14, GLFW_KEY_F14 },
		{ KeyCode::F15, GLFW_KEY_F15 },
		{ KeyCode::F16, GLFW_KEY_F16 },
		{ KeyCode::F17, GLFW_KEY_F17 },
		{ KeyCode::F18, GLFW_KEY_F18 },
		{ KeyCode::F19, GLFW_KEY_F19 },
		{ KeyCode::F20, GLFW_KEY_F20 },
		{ KeyCode::F21, GLFW_KEY_F21 },
		{ KeyCode::F22, GLFW_KEY_F22 },
		{ KeyCode::F23, GLFW_KEY_F23 },
		{ KeyCode::F24, GLFW_KEY_F24 },
		{ KeyCode::F25, GLFW_KEY_F25 },
		{ KeyCode::Keypad0, GLFW_KEY_KP_0 },
		{ KeyCode::Keypad1, GLFW_KEY_KP_1 },
		{ KeyCode::Keypad2, GLFW_KEY_KP_2 },
		{ KeyCode::Keypad3, GLFW_KEY_KP_3 },
		{ KeyCode::Keypad4, GLFW_KEY_KP_4 },
		{ KeyCode::Keypad5, GLFW_KEY_KP_5 },
		{ KeyCode::Keypad6, GLFW_KEY_KP_6 },
		{ KeyCode::Keypad7, GLFW_KEY_KP_7 },
		{ KeyCode::Keypad8, GLFW_KEY_KP_8 },
		{ KeyCode::Keypad9, GLFW_KEY_KP_9 },
		{ KeyCode::KeypadDecimal, GLFW_KEY_KP_DECIMAL },
		{ KeyCode::KeypadDivide, GLFW_KEY_KP_DIVIDE },
		{ KeyCode::KeypadMultiply, GLFW_KEY_KP_MULTIPLY },
		{ KeyCode::KeypadSubtract, GLFW_KEY_KP_SUBTRACT },
		{ KeyCode::KeypadAdd, GLFW_KEY_KP_ADD },
		{ KeyCode::KeypadEnter, GLFW_KEY_KP_ENTER },
		{ KeyCode::KeypadEqual, GLFW_KEY_KP_EQUAL },
		{ KeyCode::LeftShift, GLFW_KEY_LEFT_SHIFT },
		{ KeyCode::LeftControl, GLFW_KEY_LEFT_CONTROL },
		{ KeyCode::LeftAlt, GLFW_KEY_LEFT_ALT },
		{ KeyCode::LeftSuper, GLFW_KEY_LEFT_SUPER },
		{ KeyCode::RightShift, GLFW_KEY_RIGHT_SHIFT },
		{ KeyCode::RightControl, GLFW_KEY_RIGHT_CONTROL },
		{ KeyCode::RightAlt, GLFW_KEY_RIGHT_ALT },
		{ KeyCode::RightSuper, GLFW_KEY_RIGHT_SUPER },
		{ KeyCode::Menu, GLFW_KEY_MENU },
		}};
		// clang-format on

		constexpr bool MappingsFollowKeyCodeOrder()
		{
			for (size_t index = 0; index < KeyMappings.size(); ++index)
			{
				if (static_cast<size_t>(KeyMappings[index].Key) != index + 1)
				{
					return false;
				}
			}
			return true;
		}
		static_assert(MappingsFollowKeyCodeOrder(), "GLFWKeyMap.cpp must list every key in KeyCode order");

		// GLFW key numbers are in [0, GLFW_KEY_LAST]; the reverse table is built once.
		struct ReverseTable
		{
			std::array<KeyCode, GLFW_KEY_LAST + 1> Keys{};

			constexpr ReverseTable()
			{
				for (const KeyMapping& mapping : KeyMappings)
				{
					Keys[static_cast<size_t>(mapping.GLFWKey)] = mapping.Key;
				}
			}
		};
		constexpr ReverseTable Reverse;

		static_assert(
			GLFW_MOUSE_BUTTON_LAST + 1 == static_cast<int>(MouseButtonCount),
			"the mouse buttons of GLFW and the engine match");
		static_assert(
			GLFW_GAMEPAD_BUTTON_LAST + 1 == static_cast<int>(GamepadButtonCount),
			"the gamepad buttons of GLFW and the engine match");
		static_assert(
			GLFW_GAMEPAD_AXIS_LAST + 1 == static_cast<int>(GamepadAxisCount),
			"the gamepad axes of GLFW and the engine match");
	}

	KeyCode KeyCodeFromGLFW(int glfwKey) noexcept
	{
		if (glfwKey < 0 || glfwKey > GLFW_KEY_LAST)
		{
			return KeyCode::None;
		}
		return Reverse.Keys[static_cast<size_t>(glfwKey)];
	}

	int KeyCodeToGLFW(KeyCode key) noexcept
	{
		const auto index = static_cast<size_t>(key);
		return index >= 1 && index < KeyCodeCount ? KeyMappings[index - 1].GLFWKey : GLFW_KEY_UNKNOWN;
	}

	std::optional<MouseButton> MouseButtonFromGLFW(int glfwButton) noexcept
	{
		if (glfwButton < 0 || glfwButton >= static_cast<int>(MouseButtonCount))
		{
			return std::nullopt;
		}
		return static_cast<MouseButton>(glfwButton);
	}

	int MouseButtonToGLFW(MouseButton button) noexcept
	{
		const auto index = static_cast<size_t>(button);
		return index < MouseButtonCount ? static_cast<int>(index) : -1;
	}
}
