#include "gbpch.h"

#include "Gambit/Input/InputActions.h"

#include "Gambit/Core/VFS.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <cmath>
#include <initializer_list>
#include <set>

namespace Gambit
{
	namespace
	{
		using Json = nlohmann::json;
		using OrderedJson = nlohmann::ordered_json;

		constexpr size_t MaxActions = 1024;
		constexpr size_t MaxBindingsPerAction = 64;
		constexpr size_t MaxNameLength = 128;
		constexpr float MaxScale = 1.0e6f;

		struct BindingTypeName
		{
			InputBindingType Type;
			std::string_view Name;
		};

		constexpr std::array<BindingTypeName, 9> BindingTypeNames = { {
			{ InputBindingType::Key, "Key" },
			{ InputBindingType::MouseButton, "MouseButton" },
			{ InputBindingType::MouseDelta, "MouseDelta" },
			{ InputBindingType::Scroll, "Scroll" },
			{ InputBindingType::GamepadButton, "GamepadButton" },
			{ InputBindingType::GamepadAxis, "GamepadAxis" },
			{ InputBindingType::GamepadStick, "GamepadStick" },
			{ InputBindingType::Composite1D, "Composite1D" },
			{ InputBindingType::Composite2D, "Composite2D" },
		} };

		std::string_view ToString(InputActionType type) noexcept
		{
			switch (type)
			{
				case InputActionType::Button:
					return "Button";
				case InputActionType::Axis1D:
					return "Axis1D";
				case InputActionType::Axis2D:
					return "Axis2D";
			}
			return {};
		}

		std::string_view ToString(InputBindingType type) noexcept
		{
			for (const BindingTypeName& entry : BindingTypeNames)
			{
				if (entry.Type == type)
				{
					return entry.Name;
				}
			}
			return {};
		}

		std::string ActionPath(size_t action)
		{
			return std::format("Actions[{}]", action);
		}

		std::string BindingPath(size_t action, size_t binding)
		{
			return std::format("Actions[{}].Bindings[{}]", action, binding);
		}

		Error PathError(const std::string& path, const std::string& message)
		{
			return Error(std::format("{}: {}", path, message));
		}

		// Which action types a binding can drive.
		bool IsBindingAllowed(InputActionType action, InputBindingType binding) noexcept
		{
			switch (action)
			{
				case InputActionType::Button:
					return binding == InputBindingType::Key || binding == InputBindingType::MouseButton
						|| binding == InputBindingType::GamepadButton || binding == InputBindingType::GamepadAxis;
				case InputActionType::Axis1D:
					return binding != InputBindingType::GamepadStick && binding != InputBindingType::Composite2D;
				case InputActionType::Axis2D:
					return binding == InputBindingType::MouseDelta || binding == InputBindingType::Scroll
						|| binding == InputBindingType::GamepadStick || binding == InputBindingType::Composite2D;
			}
			return false;
		}

		// Whether `text` is well-formed UTF-8 (no overlong forms, surrogates or code points beyond U+10FFFF). JSON text
		// must be: the parser rejects anything else, and writing such a name would throw inside nlohmann::json.
		bool IsValidUtf8(std::string_view text) noexcept
		{
			size_t index = 0;
			while (index < text.size())
			{
				const auto lead = static_cast<unsigned char>(text[index]);
				size_t length = 1;
				uint32_t codepoint = lead;
				if (lead >= 0xC2 && lead <= 0xDF)
				{
					length = 2;
					codepoint = lead & 0x1Fu;
				}
				else if (lead >= 0xE0 && lead <= 0xEF)
				{
					length = 3;
					codepoint = lead & 0x0Fu;
				}
				else if (lead >= 0xF0 && lead <= 0xF4)
				{
					length = 4;
					codepoint = lead & 0x07u;
				}
				else if (lead >= 0x80)
				{
					return false; // a continuation byte, or a lead byte of an overlong or out-of-range sequence
				}
				if (text.size() - index < length)
				{
					return false;
				}
				for (size_t offset = 1; offset < length; ++offset)
				{
					const auto continuation = static_cast<unsigned char>(text[index + offset]);
					if ((continuation & 0xC0u) != 0x80u)
					{
						return false;
					}
					codepoint = (codepoint << 6) | (continuation & 0x3Fu);
				}
				const bool overlong = (length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000);
				if (overlong || codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
				{
					return false;
				}
				index += length;
			}
			return true;
		}

		bool IsValidKey(KeyCode key) noexcept
		{
			return key != KeyCode::None && static_cast<size_t>(key) < KeyCodeCount;
		}

		// Checks the fields a binding of its type uses.
		Result<void> ValidateBinding(const InputBinding& binding, InputActionType action, const std::string& path)
		{
			if (!IsBindingAllowed(action, binding.Type))
			{
				return PathError(
					path,
					std::format("a {} binding cannot drive a {} action", ToString(binding.Type), ToString(action)));
			}
			if (!std::isfinite(binding.Scale) || std::abs(binding.Scale) > MaxScale)
			{
				return PathError(path, "Scale must be a finite number of reasonable size");
			}
			if (!(binding.Deadzone >= 0.0f && binding.Deadzone < 1.0f))
			{
				return PathError(path, "Deadzone must be in [0, 1)");
			}
			if (!(binding.Threshold > 0.0f && binding.Threshold <= 1.0f))
			{
				return PathError(path, "Threshold must be in (0, 1]");
			}
			if (binding.Gamepad < -1 || binding.Gamepad >= static_cast<int>(MaxGamepads))
			{
				return PathError(path, std::format("Gamepad must be 0 to {}", MaxGamepads - 1));
			}

			const auto requireKeys = [&](std::initializer_list<KeyCode> keys) -> Result<void> {
				for (const KeyCode key : keys)
				{
					if (!IsValidKey(key))
					{
						return PathError(path, "a key is missing or invalid");
					}
				}
				return {};
			};
			switch (binding.Type)
			{
				case InputBindingType::Key:
					return requireKeys({ binding.Key });
				case InputBindingType::Composite1D:
					return requireKeys({ binding.Key, binding.NegativeKey });
				case InputBindingType::Composite2D:
					return requireKeys({ binding.Up, binding.Down, binding.Left, binding.Right });
				case InputBindingType::MouseButton:
					return static_cast<size_t>(binding.Button) < MouseButtonCount
						? Result<void>()
						: PathError(path, "invalid mouse button");
				case InputBindingType::GamepadButton:
					return static_cast<size_t>(binding.PadButton) < GamepadButtonCount
						? Result<void>()
						: PathError(path, "invalid gamepad button");
				case InputBindingType::GamepadAxis:
					return static_cast<size_t>(binding.PadAxis) < GamepadAxisCount
						? Result<void>()
						: PathError(path, "invalid gamepad axis");
				case InputBindingType::MouseDelta:
				case InputBindingType::Scroll:
				case InputBindingType::GamepadStick:
					return {};
			}
			return PathError(path, "unknown binding type");
		}

		Result<void> ValidateActions(const std::vector<InputAction>& actions)
		{
			if (actions.size() > MaxActions)
			{
				return Error(std::format("Actions: {} actions (at most {})", actions.size(), MaxActions));
			}
			std::set<std::string_view> names;
			for (size_t a = 0; a < actions.size(); ++a)
			{
				const InputAction& action = actions[a];
				if (action.Name.empty() || action.Name.size() > MaxNameLength
				    || std::ranges::any_of(action.Name, [](char c) { return static_cast<unsigned char>(c) < 0x20; })
				    || !IsValidUtf8(action.Name))
				{
					return PathError(
						ActionPath(a) + ".Name",
						std::format("a name is 1 to {} bytes of UTF-8 without control characters", MaxNameLength));
				}
				if (!names.insert(action.Name).second)
				{
					return PathError(ActionPath(a) + ".Name", std::format("duplicate action name '{}'", action.Name));
				}
				if (static_cast<size_t>(action.Type) > static_cast<size_t>(InputActionType::Axis2D))
				{
					return PathError(ActionPath(a) + ".Type", "unknown action type");
				}
				if (action.Bindings.size() > MaxBindingsPerAction)
				{
					return PathError(
						ActionPath(a) + ".Bindings",
						std::format("{} bindings (at most {})", action.Bindings.size(), MaxBindingsPerAction));
				}
				for (size_t b = 0; b < action.Bindings.size(); ++b)
				{
					if (Result<void> valid = ValidateBinding(action.Bindings[b], action.Type, BindingPath(a, b));
					    !valid)
					{
						return valid;
					}
				}
			}
			return {};
		}

		// ---- JSON reading ----

		const char* TypeName(const Json& value) noexcept
		{
			return value.type_name();
		}

		Result<void>
		CheckKeys(const Json& object, const std::string& path, std::initializer_list<std::string_view> allowed)
		{
			for (const auto& [key, value] : object.items())
			{
				if (std::ranges::find(allowed, std::string_view(key)) == allowed.end())
				{
					return PathError(path, std::format("unknown property '{}'", key));
				}
			}
			return {};
		}

		Result<const Json*> RequireProperty(const Json& object, const std::string& path, const char* key)
		{
			const auto found = object.find(key);
			if (found == object.end())
			{
				return PathError(path, std::format("missing property '{}'", key));
			}
			return &*found;
		}

		Result<std::string> RequireString(const Json& object, const std::string& path, const char* key)
		{
			const Result<const Json*> property = RequireProperty(object, path, key);
			if (!property)
			{
				return property.GetError();
			}
			const Json& value = **property;
			if (!value.is_string())
			{
				return PathError(path + "." + key, std::format("expected a string, found {}", TypeName(value)));
			}
			return value.get<std::string>();
		}

		Result<float> OptionalNumber(const Json& object, const std::string& path, const char* key, float fallback)
		{
			const auto found = object.find(key);
			if (found == object.end())
			{
				return fallback;
			}
			if (!found->is_number())
			{
				return PathError(path + "." + key, std::format("expected a number, found {}", TypeName(*found)));
			}
			const double value = found->get<double>();
			if (!std::isfinite(value) || std::abs(value) > 1.0e30)
			{
				return PathError(path + "." + key, "the number is out of range");
			}
			return static_cast<float>(value);
		}

		Result<bool> OptionalBool(const Json& object, const std::string& path, const char* key, bool fallback)
		{
			const auto found = object.find(key);
			if (found == object.end())
			{
				return fallback;
			}
			if (!found->is_boolean())
			{
				return PathError(path + "." + key, std::format("expected true or false, found {}", TypeName(*found)));
			}
			return found->get<bool>();
		}

		Result<int> OptionalGamepad(const Json& object, const std::string& path)
		{
			const auto found = object.find("Gamepad");
			if (found == object.end())
			{
				return -1;
			}
			if (!found->is_number_integer() || found->get<int64_t>() < 0
			    || found->get<int64_t>() >= static_cast<int64_t>(MaxGamepads))
			{
				return PathError(path + ".Gamepad", std::format("expected an integer from 0 to {}", MaxGamepads - 1));
			}
			return found->get<int>();
		}

		template<typename Enum, typename ParseFunction>
		Result<Enum>
		RequireEnum(const Json& object, const std::string& path, const char* key, const char* what, ParseFunction parse)
		{
			const Result<std::string> name = RequireString(object, path, key);
			if (!name)
			{
				return name.GetError();
			}
			const std::optional<Enum> value = parse(*name);
			if (!value)
			{
				return PathError(path + "." + key, std::format("unknown {} '{}'", what, *name));
			}
			return *value;
		}

		Result<InputBinding> ParseBinding(const Json& json, InputActionType actionType, const std::string& path)
		{
			if (!json.is_object())
			{
				return PathError(path, std::format("expected an object, found {}", TypeName(json)));
			}
			const Result<std::string> typeName = RequireString(json, path, "Type");
			if (!typeName)
			{
				return typeName.GetError();
			}
			const auto typeEntry = std::ranges::find(BindingTypeNames, *typeName, &BindingTypeName::Name);
			if (typeEntry == BindingTypeNames.end())
			{
				return PathError(path + ".Type", std::format("unknown binding type '{}'", *typeName));
			}

			InputBinding binding;
			binding.Type = typeEntry->Type;
			if (!IsBindingAllowed(actionType, binding.Type))
			{
				return PathError(
					path + ".Type",
					std::format("a {} binding cannot drive a {} action", *typeName, ToString(actionType)));
			}

			const auto scale = [&]() -> Result<void> {
				const Result<float> value = OptionalNumber(json, path, "Scale", 1.0f);
				if (!value)
				{
					return value.GetError();
				}
				binding.Scale = *value;
				return {};
			};
			const auto deadzone = [&]() -> Result<void> {
				const Result<float> value = OptionalNumber(json, path, "Deadzone", 0.0f);
				if (!value)
				{
					return value.GetError();
				}
				binding.Deadzone = *value;
				return {};
			};
			const auto flag = [&](const char* key, bool& target) -> Result<void> {
				const Result<bool> value = OptionalBool(json, path, key, false);
				if (!value)
				{
					return value.GetError();
				}
				target = *value;
				return {};
			};
			const auto gamepad = [&]() -> Result<void> {
				const Result<int> value = OptionalGamepad(json, path);
				if (!value)
				{
					return value.GetError();
				}
				binding.Gamepad = *value;
				return {};
			};
			const auto key = [&](const char* name, KeyCode& target) -> Result<void> {
				const Result<KeyCode> value = RequireEnum<KeyCode>(json, path, name, "key", ParseKeyCode);
				if (!value)
				{
					return value.GetError();
				}
				target = *value;
				return {};
			};
			// Runs the readers in order and stops at the first problem.
			const auto all = [](std::initializer_list<std::function<Result<void>()>> steps) -> Result<void> {
				for (const auto& step : steps)
				{
					if (Result<void> result = step(); !result)
					{
						return result;
					}
				}
				return {};
			};

			Result<void> result;
			switch (binding.Type)
			{
				case InputBindingType::Key:
					result = CheckKeys(json, path, { "Type", "Key", "Scale" });
					if (result)
					{
						result = all({ [&] { return key("Key", binding.Key); }, scale });
					}
					break;
				case InputBindingType::MouseButton:
				{
					result = CheckKeys(json, path, { "Type", "Button", "Scale" });
					if (result)
					{
						const Result<MouseButton> button =
							RequireEnum<MouseButton>(json, path, "Button", "mouse button", ParseMouseButton);
						if (!button)
						{
							return button.GetError();
						}
						binding.Button = *button;
						result = scale();
					}
					break;
				}
				case InputBindingType::MouseDelta:
				case InputBindingType::Scroll:
					if (actionType == InputActionType::Axis1D)
					{
						result = CheckKeys(json, path, { "Type", "Axis", "Scale", "Invert" });
						if (result)
						{
							const Result<std::string> axis = RequireString(json, path, "Axis");
							if (!axis)
							{
								return axis.GetError();
							}
							if (*axis != "X" && *axis != "Y")
							{
								return PathError(
									path + ".Axis", std::format("expected \"X\" or \"Y\", found '{}'", *axis));
							}
							binding.Axis = *axis == "X" ? InputAxis::X : InputAxis::Y;
							result = all({ scale, [&] { return flag("Invert", binding.Invert); } });
						}
					}
					else
					{
						result = CheckKeys(json, path, { "Type", "Scale", "InvertX", "InvertY" });
						if (result)
						{
							result = all({ scale, [&] { return flag("InvertX", binding.InvertX); },
							               [&] { return flag("InvertY", binding.InvertY); } });
						}
					}
					break;
				case InputBindingType::GamepadButton:
				{
					result = CheckKeys(json, path, { "Type", "Button", "Gamepad", "Scale" });
					if (result)
					{
						const Result<GamepadButton> button =
							RequireEnum<GamepadButton>(json, path, "Button", "gamepad button", ParseGamepadButton);
						if (!button)
						{
							return button.GetError();
						}
						binding.PadButton = *button;
						result = all({ gamepad, scale });
					}
					break;
				}
				case InputBindingType::GamepadAxis:
				{
					result = CheckKeys(
						json, path, { "Type", "Axis", "Gamepad", "Deadzone", "Scale", "Invert", "Threshold" });
					if (result)
					{
						const Result<GamepadAxis> axis =
							RequireEnum<GamepadAxis>(json, path, "Axis", "gamepad axis", ParseGamepadAxis);
						if (!axis)
						{
							return axis.GetError();
						}
						binding.PadAxis = *axis;
						result = all({ gamepad, deadzone, scale, [&] { return flag("Invert", binding.Invert); },
						               [&]() -> Result<void> {
										   const Result<float> value = OptionalNumber(json, path, "Threshold", 0.5f);
										   if (!value)
										   {
											   return value.GetError();
										   }
										   binding.Threshold = *value;
										   return {};
									   } });
					}
					break;
				}
				case InputBindingType::GamepadStick:
				{
					result = CheckKeys(
						json, path, { "Type", "Stick", "Gamepad", "Deadzone", "Scale", "InvertX", "InvertY" });
					if (result)
					{
						const Result<std::string> stick = RequireString(json, path, "Stick");
						if (!stick)
						{
							return stick.GetError();
						}
						if (*stick != "Left" && *stick != "Right")
						{
							return PathError(
								path + ".Stick", std::format("expected \"Left\" or \"Right\", found '{}'", *stick));
						}
						binding.Stick = *stick == "Left" ? GamepadStick::Left : GamepadStick::Right;
						result = all({ gamepad, deadzone, scale, [&] { return flag("InvertX", binding.InvertX); },
						               [&] { return flag("InvertY", binding.InvertY); } });
					}
					break;
				}
				case InputBindingType::Composite1D:
					result = CheckKeys(json, path, { "Type", "Positive", "Negative", "Scale" });
					if (result)
					{
						result = all({ [&] { return key("Positive", binding.Key); },
						               [&] { return key("Negative", binding.NegativeKey); }, scale });
					}
					break;
				case InputBindingType::Composite2D:
					result = CheckKeys(json, path, { "Type", "Up", "Down", "Left", "Right", "Scale" });
					if (result)
					{
						result = all({ [&] { return key("Up", binding.Up); }, [&] { return key("Down", binding.Down); },
						               [&] { return key("Left", binding.Left); },
						               [&] { return key("Right", binding.Right); }, scale });
					}
					break;
			}
			if (!result)
			{
				return result.GetError();
			}
			return binding;
		}

		Result<InputAction> ParseAction(const Json& json, size_t index)
		{
			const std::string path = ActionPath(index);
			if (!json.is_object())
			{
				return PathError(path, std::format("expected an object, found {}", TypeName(json)));
			}
			if (Result<void> keys = CheckKeys(json, path, { "Name", "Type", "Bindings" }); !keys)
			{
				return keys.GetError();
			}

			InputAction action;
			const Result<std::string> name = RequireString(json, path, "Name");
			if (!name)
			{
				return name.GetError();
			}
			action.Name = *name;

			const Result<std::string> typeName = RequireString(json, path, "Type");
			if (!typeName)
			{
				return typeName.GetError();
			}
			if (*typeName == "Button")
			{
				action.Type = InputActionType::Button;
			}
			else if (*typeName == "Axis1D")
			{
				action.Type = InputActionType::Axis1D;
			}
			else if (*typeName == "Axis2D")
			{
				action.Type = InputActionType::Axis2D;
			}
			else
			{
				return PathError(
					path + ".Type", std::format("unknown action type '{}' (Button, Axis1D or Axis2D)", *typeName));
			}

			const Result<const Json*> bindingsProperty = RequireProperty(json, path, "Bindings");
			if (!bindingsProperty)
			{
				return bindingsProperty.GetError();
			}
			const Json& bindings = **bindingsProperty;
			if (!bindings.is_array())
			{
				return PathError(path + ".Bindings", std::format("expected an array, found {}", TypeName(bindings)));
			}
			if (bindings.size() > MaxBindingsPerAction)
			{
				return PathError(
					path + ".Bindings", std::format("{} bindings (at most {})", bindings.size(), MaxBindingsPerAction));
			}
			size_t bindingIndex = 0;
			for (const Json& bindingJson : bindings)
			{
				Result<InputBinding> binding = ParseBinding(bindingJson, action.Type, BindingPath(index, bindingIndex));
				if (!binding)
				{
					return binding.GetError();
				}
				action.Bindings.push_back(*binding);
				++bindingIndex;
			}
			return action;
		}

		// ---- JSON writing ----

		// The shortest decimal that reads back as the same float, so files show 0.15 and not 0.15000000596046448.
		OrderedJson JsonNumber(float value)
		{
			std::array<char, 32> text{};
			const auto [end, error] = std::to_chars(text.data(), text.data() + text.size(), value);
			double parsed = static_cast<double>(value);
			if (error == std::errc{})
			{
				std::from_chars(text.data(), end, parsed);
			}
			return parsed;
		}

		OrderedJson WriteBinding(const InputBinding& binding, InputActionType actionType)
		{
			OrderedJson json = OrderedJson::object();
			json["Type"] = std::string(ToString(binding.Type));
			const auto writeScale = [&] {
				if (binding.Scale != 1.0f)
				{
					json["Scale"] = JsonNumber(binding.Scale);
				}
			};
			const auto writeDeadzone = [&] {
				if (binding.Deadzone != 0.0f)
				{
					json["Deadzone"] = JsonNumber(binding.Deadzone);
				}
			};
			const auto writeGamepad = [&] {
				if (binding.Gamepad >= 0)
				{
					json["Gamepad"] = binding.Gamepad;
				}
			};
			const auto writeFlag = [&](const char* key, bool value) {
				if (value)
				{
					json[key] = true;
				}
			};
			switch (binding.Type)
			{
				case InputBindingType::Key:
					json["Key"] = std::string(ToString(binding.Key));
					writeScale();
					break;
				case InputBindingType::MouseButton:
					json["Button"] = std::string(ToString(binding.Button));
					writeScale();
					break;
				case InputBindingType::MouseDelta:
				case InputBindingType::Scroll:
					if (actionType == InputActionType::Axis1D)
					{
						json["Axis"] = binding.Axis == InputAxis::X ? "X" : "Y";
						writeScale();
						writeFlag("Invert", binding.Invert);
					}
					else
					{
						writeScale();
						writeFlag("InvertX", binding.InvertX);
						writeFlag("InvertY", binding.InvertY);
					}
					break;
				case InputBindingType::GamepadButton:
					json["Button"] = std::string(ToString(binding.PadButton));
					writeGamepad();
					writeScale();
					break;
				case InputBindingType::GamepadAxis:
					json["Axis"] = std::string(ToString(binding.PadAxis));
					writeGamepad();
					writeDeadzone();
					writeScale();
					writeFlag("Invert", binding.Invert);
					if (binding.Threshold != 0.5f)
					{
						json["Threshold"] = JsonNumber(binding.Threshold);
					}
					break;
				case InputBindingType::GamepadStick:
					json["Stick"] = binding.Stick == GamepadStick::Left ? "Left" : "Right";
					writeGamepad();
					writeDeadzone();
					writeScale();
					writeFlag("InvertX", binding.InvertX);
					writeFlag("InvertY", binding.InvertY);
					break;
				case InputBindingType::Composite1D:
					json["Positive"] = std::string(ToString(binding.Key));
					json["Negative"] = std::string(ToString(binding.NegativeKey));
					writeScale();
					break;
				case InputBindingType::Composite2D:
					json["Up"] = std::string(ToString(binding.Up));
					json["Down"] = std::string(ToString(binding.Down));
					json["Left"] = std::string(ToString(binding.Left));
					json["Right"] = std::string(ToString(binding.Right));
					writeScale();
					break;
			}
			return json;
		}
	}

	InputActions::InputActions(std::vector<InputAction> actions)
		: m_Actions(std::move(actions))
	{
		for (size_t index = 0; index < m_Actions.size(); ++index)
		{
			m_Index.emplace(m_Actions[index].Name, static_cast<InputActionId>(index));
		}
	}

	Result<InputActions> InputActions::Create(std::vector<InputAction> actions)
	{
		if (Result<void> valid = ValidateActions(actions); !valid)
		{
			return valid.GetError();
		}
		return InputActions(std::move(actions));
	}

	Result<InputActions> InputActions::Parse(std::string_view text)
	{
		Json document;
		try
		{
			document = Json::parse(text.begin(), text.end());
		}
		catch (const std::exception& exception)
		{
			return Error(std::format("input actions: invalid JSON: {}", exception.what()));
		}

		const std::string root = "InputActions";
		if (!document.is_object())
		{
			return PathError(root, std::format("expected an object, found {}", TypeName(document)));
		}
		if (Result<void> keys = CheckKeys(document, root, { "Version", "Actions" }); !keys)
		{
			return keys.GetError();
		}
		const Result<const Json*> versionProperty = RequireProperty(document, root, "Version");
		if (!versionProperty)
		{
			return versionProperty.GetError();
		}
		const Json& version = **versionProperty;
		if (!version.is_number_integer() || version.get<int64_t>() != InputActionsFormatVersion)
		{
			return PathError(
				"Version",
				std::format(
					"unsupported version {} (this build reads version {})", version.dump(), InputActionsFormatVersion));
		}
		const Result<const Json*> actionsProperty = RequireProperty(document, root, "Actions");
		if (!actionsProperty)
		{
			return actionsProperty.GetError();
		}
		const Json& actionsJson = **actionsProperty;
		if (!actionsJson.is_array())
		{
			return PathError("Actions", std::format("expected an array, found {}", TypeName(actionsJson)));
		}
		if (actionsJson.size() > MaxActions)
		{
			return PathError("Actions", std::format("{} actions (at most {})", actionsJson.size(), MaxActions));
		}

		std::vector<InputAction> actions;
		size_t index = 0;
		for (const Json& actionJson : actionsJson)
		{
			Result<InputAction> action = ParseAction(actionJson, index++);
			if (!action)
			{
				return action.GetError();
			}
			actions.push_back(std::move(*action));
		}
		return Create(std::move(actions));
	}

	Result<InputActions> InputActions::Load(const VFS& vfs, std::string_view path)
	{
		const Result<std::string> text = vfs.ReadText(path);
		if (!text)
		{
			return Error(std::format("input actions: cannot read '{}': {}", path, text.GetError().Message));
		}
		Result<InputActions> actions = Parse(*text);
		if (!actions)
		{
			return Error(std::format("{}: {}", path, actions.GetError().Message));
		}
		return actions;
	}

	std::string InputActions::ToJson() const
	{
		OrderedJson document = OrderedJson::object();
		document["Version"] = InputActionsFormatVersion;
		OrderedJson actions = OrderedJson::array();
		for (const InputAction& action : m_Actions)
		{
			OrderedJson entry = OrderedJson::object();
			entry["Name"] = action.Name;
			entry["Type"] = std::string(ToString(action.Type));
			OrderedJson bindings = OrderedJson::array();
			for (const InputBinding& binding : action.Bindings)
			{
				bindings.push_back(WriteBinding(binding, action.Type));
			}
			entry["Bindings"] = std::move(bindings);
			actions.push_back(std::move(entry));
		}
		document["Actions"] = std::move(actions);
		return document.dump(2) + "\n";
	}

	InputActionId InputActions::Find(std::string_view name) const noexcept
	{
		const auto found = m_Index.find(std::string(name));
		return found == m_Index.end() ? InvalidInputAction : found->second;
	}
}
