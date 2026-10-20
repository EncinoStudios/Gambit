#include "gbpch.h"

#include "Gambit/Automation/CommandLine.h"

#include "Gambit/Serialization/FieldSerializer.h"

#include <algorithm>
#include <cctype>

namespace Gambit
{
	namespace
	{
		bool IsOption(std::string_view token) noexcept
		{
			return token.starts_with("--") && token.size() > 2;
		}

		std::string Lower(std::string_view text)
		{
			std::string lower(text);
			std::ranges::transform(lower, lower.begin(), [](char c) {
				return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			});
			return lower;
		}

		std::optional<bool> ParseBoolean(std::string_view text)
		{
			const std::string lower = Lower(text);
			if (lower == "true" || lower == "yes" || lower == "1" || lower == "on")
			{
				return true;
			}
			if (lower == "false" || lower == "no" || lower == "0" || lower == "off")
			{
				return false;
			}
			return std::nullopt;
		}

		Error UsageError(std::string message)
		{
			return Error(std::move(message));
		}

		Result<Json> ConvertScalar(const std::string& name, ArgumentType type, std::string_view text)
		{
			switch (type)
			{
				case ArgumentType::String:
					return Json(std::string(text));
				case ArgumentType::Integer:
				{
					// Ranges (also uint64 values above INT64_MAX, for UnsignedArg) are CommandInfo::ParseArguments'
					// job.
					const Result<Json> parsed = ParseJson(text);
					if (parsed)
					{
						if (const Result<int64_t> integer = ReadJsonInteger<int64_t>(*parsed, {}); integer)
						{
							return Json(*integer);
						}
						if (const Result<uint64_t> large = ReadJsonInteger<uint64_t>(*parsed, {}); large)
						{
							return Json(*large);
						}
					}
					return UsageError(std::format("option --{} expects an integer, got '{}'", ToKebabCase(name), text));
				}
				case ArgumentType::Number:
				{
					const Result<Json> parsed = ParseJson(text);
					if (parsed && parsed->is_number())
					{
						return *parsed;
					}
					return UsageError(std::format("option --{} expects a number, got '{}'", ToKebabCase(name), text));
				}
				case ArgumentType::Boolean:
				{
					if (const std::optional<bool> value = ParseBoolean(text); value.has_value())
					{
						return Json(*value);
					}
					return UsageError(
						std::format("option --{} expects true or false, got '{}'", ToKebabCase(name), text));
				}
				case ArgumentType::Object:
				{
					const Result<Json> parsed = ParseJson(text);
					if (parsed && parsed->is_object())
					{
						return *parsed;
					}
					return UsageError(std::format(
						"option --{} expects a JSON object such as '{{\"Name\": 1}}'{}", ToKebabCase(name),
						parsed ? std::string() : std::format(" ({})", parsed.GetError().Message)));
				}
				case ArgumentType::Array:
					break;
				case ArgumentType::Any:
				{
					Result<Json> parsed = ParseJson(text);
					return parsed ? std::move(parsed) : Result<Json>(Json(std::string(text)));
				}
			}
			return UsageError(std::format("option --{} cannot be converted", ToKebabCase(name)));
		}

		Result<Json> ConvertValue(const ArgumentInfo& argument, std::string_view text)
		{
			if (argument.Type != ArgumentType::Array)
			{
				return ConvertScalar(argument.Name, argument.Type, text);
			}
			if (text.starts_with('['))
			{
				const Result<Json> parsed = ParseJson(text);
				if (parsed && parsed->is_array())
				{
					return *parsed;
				}
				return UsageError(std::format(
					"option --{} expects a JSON array{}", ToKebabCase(argument.Name),
					parsed ? std::string() : std::format(" ({})", parsed.GetError().Message)));
			}
			// One element; repeating the option adds more.
			Result<Json> element = ConvertScalar(argument.Name, argument.ItemType, text);
			if (!element)
			{
				return element.GetError();
			}
			return Json::array({ std::move(*element) });
		}
	}

	std::string ToCamelCase(std::string_view kebab)
	{
		std::string camel;
		bool upper = false;
		for (const char c : kebab)
		{
			if (c == '-')
			{
				upper = !camel.empty();
				continue;
			}
			camel += upper ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
			upper = false;
		}
		return camel;
	}

	std::string ToKebabCase(std::string_view camel)
	{
		std::string kebab;
		for (const char c : camel)
		{
			if (std::isupper(static_cast<unsigned char>(c)))
			{
				kebab += '-';
				kebab += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			else
			{
				kebab += c;
			}
		}
		return kebab;
	}

	std::optional<ResolvedCommand> ResolveCommand(const CommandRegistry& registry, std::span<const std::string> tokens)
	{
		size_t words = 0;
		while (words < tokens.size() && words < 3 && !tokens[words].starts_with('-'))
		{
			++words;
		}
		for (size_t count = words; count > 0; --count)
		{
			std::string name;
			for (size_t index = 0; index < count; ++index)
			{
				name += index == 0 ? tokens[index] : "." + tokens[index];
			}
			if (const CommandInfo* info = registry.Find(name); info != nullptr)
			{
				return ResolvedCommand{ info, count };
			}
		}
		return std::nullopt;
	}

	Result<Json> ParseCommandOptions(
		const CommandInfo& command, std::span<const std::string> tokens,
		const std::optional<std::string>& baseArguments)
	{
		Json arguments = Json::object();
		if (baseArguments.has_value())
		{
			Result<Json> parsed = ParseJson(*baseArguments, "--args");
			if (!parsed)
			{
				return UsageError(parsed.GetError().Message);
			}
			if (!parsed->is_object())
			{
				return UsageError("--args expects a JSON object");
			}
			arguments = std::move(*parsed);
		}

		std::vector<std::string> names;
		for (const ArgumentInfo& argument : command.Arguments)
		{
			names.push_back(ToKebabCase(argument.Name));
		}

		for (size_t index = 0; index < tokens.size(); ++index)
		{
			const std::string& token = tokens[index];
			if (!IsOption(token))
			{
				return UsageError(std::format("unexpected argument '{}': options look like --name value", token));
			}
			const size_t equals = token.find('=');
			std::string spelling = token.substr(2, equals == std::string::npos ? std::string::npos : equals - 2);
			std::optional<std::string> inlineValue;
			if (equals != std::string::npos)
			{
				inlineValue = token.substr(equals + 1);
			}

			bool negated = false;
			const ArgumentInfo* argument = command.FindArgument(ToCamelCase(spelling));
			if (argument == nullptr && spelling.starts_with("no-"))
			{
				const ArgumentInfo* positive = command.FindArgument(ToCamelCase(spelling.substr(3)));
				if (positive != nullptr && positive->Type == ArgumentType::Boolean && !inlineValue.has_value())
				{
					argument = positive;
					negated = true;
				}
			}
			if (argument == nullptr)
			{
				std::string message = std::format("'{}' has no option --{}", command.Name, spelling);
				if (const std::vector<std::string> close = FindClosestNames(spelling, names, 1); !close.empty())
				{
					message += std::format(" (did you mean --{}?)", close.front());
				}
				message += names.empty() ? "; it takes no options" : std::format("; its options are: --{}", [&] {
					std::string joined;
					for (const std::string& name : names)
					{
						joined += joined.empty() ? name : ", --" + name;
					}
					return joined;
				}());
				return UsageError(std::move(message));
			}

			std::string value;
			if (negated)
			{
				value = "false";
			}
			else if (inlineValue.has_value())
			{
				value = *inlineValue;
			}
			else if (argument->Type == ArgumentType::Boolean)
			{
				const bool hasLiteral = index + 1 < tokens.size() && !IsOption(tokens[index + 1])
					&& ParseBoolean(tokens[index + 1]).has_value();
				value = hasLiteral ? tokens[++index] : "true";
			}
			else
			{
				if (index + 1 >= tokens.size() || IsOption(tokens[index + 1]))
				{
					return UsageError(std::format("option --{} needs a value", ToKebabCase(argument->Name)));
				}
				value = tokens[++index];
			}

			Result<Json> converted = ConvertValue(*argument, value);
			if (!converted)
			{
				return converted.GetError();
			}
			if (argument->Type == ArgumentType::Array && !value.starts_with('[') && arguments.contains(argument->Name)
			    && arguments.at(argument->Name).is_array())
			{
				arguments[argument->Name].push_back(std::move(converted->front())); // a repeated option adds an element
			}
			else
			{
				arguments[argument->Name] = std::move(*converted);
			}
		}
		return arguments;
	}
}
