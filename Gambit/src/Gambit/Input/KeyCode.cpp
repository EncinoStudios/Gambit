#include "gbpch.h"

#include "Gambit/Input/KeyCode.h"

namespace Gambit
{
	namespace
	{
		constexpr std::array<std::string_view, KeyCodeCount> KeyNames = { "None",
#define GB_KEY_NAME(name) #name,
			                                                              GB_KEY_CODES(GB_KEY_NAME)
#undef GB_KEY_NAME
		};

		constexpr std::array<std::string_view, MouseButtonCount> MouseButtonNames = { "Left",    "Right",   "Middle",
			                                                                          "Button4", "Button5", "Button6",
			                                                                          "Button7", "Button8" };

		constexpr std::array<std::string_view, GamepadButtonCount> GamepadButtonNames = {
			"A",     "B",         "X",          "Y",      "LeftBumper", "RightBumper", "Back",    "Start",
			"Guide", "LeftThumb", "RightThumb", "DpadUp", "DpadRight",  "DpadDown",    "DpadLeft"
		};

		constexpr std::array<std::string_view, GamepadAxisCount> GamepadAxisNames = { "LeftX",       "LeftY",
			                                                                          "RightX",      "RightY",
			                                                                          "LeftTrigger", "RightTrigger" };

		template<typename Enum, size_t N>
		std::string_view NameOf(const std::array<std::string_view, N>& names, Enum value) noexcept
		{
			const auto index = static_cast<size_t>(value);
			return index < N ? names[index] : std::string_view{};
		}

		// Index of `name` in `names`, starting the search at `first`.
		template<size_t N>
		std::optional<size_t>
		IndexOf(const std::array<std::string_view, N>& names, std::string_view name, size_t first = 0) noexcept
		{
			for (size_t index = first; index < N; ++index)
			{
				if (names[index] == name)
				{
					return index;
				}
			}
			return std::nullopt;
		}
	}

	std::string_view ToString(KeyCode key) noexcept
	{
		return NameOf(KeyNames, key);
	}

	std::string_view ToString(MouseButton button) noexcept
	{
		return NameOf(MouseButtonNames, button);
	}

	std::string_view ToString(GamepadButton button) noexcept
	{
		return NameOf(GamepadButtonNames, button);
	}

	std::string_view ToString(GamepadAxis axis) noexcept
	{
		return NameOf(GamepadAxisNames, axis);
	}

	std::optional<KeyCode> ParseKeyCode(std::string_view name) noexcept
	{
		const std::optional<size_t> index = IndexOf(KeyNames, name, 1); // "None" is not a key
		return index ? std::optional<KeyCode>(static_cast<KeyCode>(*index)) : std::nullopt;
	}

	std::optional<MouseButton> ParseMouseButton(std::string_view name) noexcept
	{
		const std::optional<size_t> index = IndexOf(MouseButtonNames, name);
		return index ? std::optional<MouseButton>(static_cast<MouseButton>(*index)) : std::nullopt;
	}

	std::optional<GamepadButton> ParseGamepadButton(std::string_view name) noexcept
	{
		const std::optional<size_t> index = IndexOf(GamepadButtonNames, name);
		return index ? std::optional<GamepadButton>(static_cast<GamepadButton>(*index)) : std::nullopt;
	}

	std::optional<GamepadAxis> ParseGamepadAxis(std::string_view name) noexcept
	{
		const std::optional<size_t> index = IndexOf(GamepadAxisNames, name);
		return index ? std::optional<GamepadAxis>(static_cast<GamepadAxis>(*index)) : std::nullopt;
	}
}
