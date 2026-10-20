#include "gbpch.h"

#include "Gambit/Automation/CommandInfo.h"

#include "Gambit/Serialization/FieldSerializer.h"

#include <cctype>
#include <cmath>
#include <set>

namespace Gambit
{
	namespace
	{
		bool IsLowerAlpha(char c) noexcept
		{
			return c >= 'a' && c <= 'z';
		}

		bool IsAlnum(char c) noexcept
		{
			return std::isalnum(static_cast<unsigned char>(c)) != 0;
		}

		bool IsCommandNameCharacter(char c) noexcept
		{
			return IsLowerAlpha(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '-';
		}

		bool IsValidCommandName(std::string_view name) noexcept
		{
			size_t start = 0;
			for (;;)
			{
				const size_t end = name.find('.', start);
				const std::string_view part = name.substr(start, end == std::string_view::npos ? end : end - start);
				if (part.empty() || !IsLowerAlpha(part.front()) || part.back() == '-'
				    || !std::ranges::all_of(part, IsCommandNameCharacter))
				{
					return false;
				}
				if (end == std::string_view::npos)
				{
					return true;
				}
				start = end + 1;
			}
		}

		bool IsValidArgumentName(std::string_view name) noexcept
		{
			return !name.empty() && IsLowerAlpha(name.front()) && std::ranges::all_of(name, IsAlnum);
		}

		std::string_view Describe(ArgumentType type) noexcept
		{
			switch (type)
			{
				case ArgumentType::String:
					return "a string";
				case ArgumentType::Integer:
					return "an integer";
				case ArgumentType::Number:
					return "a number";
				case ArgumentType::Boolean:
					return "true or false";
				case ArgumentType::Array:
					return "an array";
				case ArgumentType::Object:
					return "an object";
				case ArgumentType::Any:
					return "any value";
			}
			return "a value";
		}

		std::string_view DescribeFound(const Json& json) noexcept
		{
			switch (json.type())
			{
				case Json::value_t::null:
					return "null";
				case Json::value_t::object:
					return "an object";
				case Json::value_t::array:
					return "an array";
				case Json::value_t::string:
					return "a string";
				case Json::value_t::boolean:
					return "a boolean";
				case Json::value_t::number_integer:
				case Json::value_t::number_unsigned:
					return "an integer";
				case Json::value_t::number_float:
					return "a number";
				default:
					return "unexpected data";
			}
		}

		constexpr double TwoToThe63 = 9223372036854775808.0;

		bool IsWholeNumber(double value) noexcept
		{
			return std::isfinite(value) && value == std::floor(value);
		}

		std::string FormatJsonNumber(const Json& value)
		{
			if (value.is_number_unsigned())
			{
				return std::format("{}", value.get<uint64_t>());
			}
			if (value.is_number_integer())
			{
				return std::format("{}", value.get<int64_t>());
			}
			const double number = value.get<double>();
			// Integers given as floats beyond 2^63 are exact as written by "{:.0f}".
			return IsWholeNumber(number) && std::fabs(number) < 1.0e21 ? std::format("{:.0f}", number)
																	   : FormatNumber(number);
		}

		std::string JoinNames(const std::vector<std::string>& names)
		{
			std::string joined;
			for (const std::string& name : names)
			{
				joined += joined.empty() ? name : ", " + name;
			}
			return joined;
		}

		// The value normalized (whole floats become integers), or why it does not fit the type.
		Result<Json>
		CheckValue(const ArgumentInfo& argument, ArgumentType type, const Json& value, const std::string& label)
		{
			const auto mismatch = [&] {
				return Error::Format(
					"argument '{}': expected {}, found {}", label, Describe(type), DescribeFound(value));
			};
			const auto checkRange = [&](double number) -> Result<void> {
				if (argument.Minimum.has_value() && number < *argument.Minimum)
				{
					return Error::Format(
						"argument '{}': {} is below the minimum {}", label, FormatNumber(number),
						FormatNumber(*argument.Minimum));
				}
				if (argument.Maximum.has_value() && number > *argument.Maximum)
				{
					return Error::Format(
						"argument '{}': {} is above the maximum {}", label, FormatNumber(number),
						FormatNumber(*argument.Maximum));
				}
				return {};
			};
			switch (type)
			{
				case ArgumentType::String:
					if (!value.is_string())
					{
						return mismatch();
					}
					if (!argument.Enum.empty()
					    && std::ranges::find(argument.Enum, value.get_ref<const std::string&>()) == argument.Enum.end())
					{
						return Error::Format(
							"argument '{}': '{}' is not one of: {}", label, value.get_ref<const std::string&>(),
							JoinNames(argument.Enum));
					}
					return value;
				case ArgumentType::Integer:
				{
					// Integers are int64 values; those above INT64_MAX (64-bit seeds) only when the maximum of the
					// argument allows them (UnsignedArg), since handlers read the others as int64.
					if (const Result<int64_t> integer = ReadJsonInteger<int64_t>(value, {}); integer)
					{
						if (Result<void> inRange = checkRange(static_cast<double>(*integer)); !inRange)
						{
							return inRange.GetError();
						}
						return Json(*integer);
					}
					const Result<uint64_t> large = ReadJsonInteger<uint64_t>(value, {});
					const bool allowsLarge = argument.Maximum.has_value() && *argument.Maximum >= TwoToThe63;
					if (large && allowsLarge)
					{
						if (Result<void> inRange = checkRange(static_cast<double>(*large)); !inRange)
						{
							return inRange.GetError();
						}
						return Json(*large);
					}
					if (value.is_number_integer() || (value.is_number_float() && IsWholeNumber(value.get<double>())))
					{
						return Error::Format(
							"argument '{}': {} is out of range{}", label, FormatJsonNumber(value),
							argument.Maximum.has_value()
								? std::format(" (the maximum is {})", FormatNumber(*argument.Maximum))
								: std::string(" for a 64-bit integer"));
					}
					return mismatch();
				}
				case ArgumentType::Number:
					if (!value.is_number())
					{
						return mismatch();
					}
					if (Result<void> inRange = checkRange(value.get<double>()); !inRange)
					{
						return inRange.GetError();
					}
					return value;
				case ArgumentType::Boolean:
					return value.is_boolean() ? Result<Json>(value) : Result<Json>(mismatch());
				case ArgumentType::Object:
					return value.is_object() ? Result<Json>(value) : Result<Json>(mismatch());
				case ArgumentType::Array:
				{
					if (!value.is_array())
					{
						return mismatch();
					}
					if (argument.ItemType == ArgumentType::Any)
					{
						return value;
					}
					const ArgumentInfo itemArgument{ .Name = argument.Name,
						                             .Type = argument.ItemType,
						                             .Enum = argument.Enum };
					Json checked = Json::array();
					for (size_t index = 0; index < value.size(); ++index)
					{
						Result<Json> item = CheckValue(
							itemArgument, argument.ItemType, value[index], std::format("{}[{}]", label, index));
						if (!item)
						{
							return item;
						}
						checked.push_back(std::move(*item));
					}
					return checked;
				}
				case ArgumentType::Any:
					return value;
			}
			return value;
		}

		const char* JsonSchemaType(ArgumentType type) noexcept
		{
			switch (type)
			{
				case ArgumentType::String:
					return "string";
				case ArgumentType::Integer:
					return "integer";
				case ArgumentType::Number:
					return "number";
				case ArgumentType::Boolean:
					return "boolean";
				case ArgumentType::Array:
					return "array";
				case ArgumentType::Object:
					return "object";
				case ArgumentType::Any:
					return nullptr;
			}
			return nullptr;
		}

		size_t EditDistance(std::string_view a, std::string_view b)
		{
			std::vector<size_t> previous(b.size() + 1);
			std::vector<size_t> current(b.size() + 1);
			for (size_t j = 0; j <= b.size(); ++j)
			{
				previous[j] = j;
			}
			for (size_t i = 1; i <= a.size(); ++i)
			{
				current[0] = i;
				for (size_t j = 1; j <= b.size(); ++j)
				{
					const size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
					current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, substitution });
				}
				std::swap(previous, current);
			}
			return previous[b.size()];
		}
	}

	std::string_view ToString(ArgumentType type) noexcept
	{
		const char* name = JsonSchemaType(type);
		return name != nullptr ? name : "any";
	}

	ArgumentInfo StringArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::String,
			                 .Description = std::move(description) };
	}

	ArgumentInfo IntegerArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Integer,
			                 .Description = std::move(description) };
	}

	ArgumentInfo UnsignedArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Integer,
			                 .Description = std::move(description),
			                 .Minimum = 0.0,
			                 .Maximum = MaxUnsignedArgument };
	}

	ArgumentInfo NumberArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Number,
			                 .Description = std::move(description) };
	}

	ArgumentInfo BooleanArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Boolean,
			                 .Description = std::move(description) };
	}

	ArgumentInfo ObjectArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Object,
			                 .Description = std::move(description) };
	}

	ArgumentInfo AnyArg(std::string name, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Any,
			                 .Description = std::move(description) };
	}

	ArgumentInfo ArrayArg(std::string name, ArgumentType itemType, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::Array,
			                 .Description = std::move(description),
			                 .ItemType = itemType };
	}

	ArgumentInfo EnumArg(std::string name, std::vector<std::string> values, std::string description)
	{
		return ArgumentInfo{ .Name = std::move(name),
			                 .Type = ArgumentType::String,
			                 .Description = std::move(description),
			                 .Enum = std::move(values) };
	}

	std::vector<std::string> GetFlagNames(CommandFlags flags)
	{
		std::vector<std::string> names;
		const std::pair<CommandFlags, const char*> known[] = {
			{ CommandFlags::Mutating, "mutating" },
			{ CommandFlags::Undoable, "undoable" },
			{ CommandFlags::RequiresProject, "requires-project" },
			{ CommandFlags::RequiresScene, "requires-scene" },
			{ CommandFlags::EditorOnly, "editor-only" },
			{ CommandFlags::WritesFiles, "writes-files" },
		};
		for (const auto& [flag, name] : known)
		{
			if (HasFlag(flags, flag))
			{
				names.emplace_back(name);
			}
		}
		return names;
	}

	std::string FormatNumber(double value)
	{
		if (value == MaxUnsignedArgument)
		{
			return "18446744073709551615"; // the limit of UnsignedArg, 2^64 - 1, which is 2^64 as a double
		}
		if (std::isfinite(value) && value == std::floor(value) && std::fabs(value) < 1.0e15)
		{
			return std::format("{}", static_cast<int64_t>(value));
		}
		return std::format("{}", value);
	}

	bool IsReservedArgumentName(std::string_view name) noexcept
	{
		return name == "project" || name == "scene" || name == "json" || name == "args" || name == "dryRun"
			|| name == "help" || name == "keepGoing";
	}

	std::vector<std::string>
	FindClosestNames(std::string_view text, const std::vector<std::string>& candidates, size_t limit)
	{
		const size_t threshold = std::max<size_t>(2, text.size() / 3);
		std::vector<std::pair<size_t, std::string>> scored;
		for (const std::string& candidate : candidates)
		{
			const size_t distance = EditDistance(text, candidate);
			// A name that merely extends or abbreviates the text is also a good guess.
			const bool related = !text.empty() && (candidate.starts_with(text) || text.starts_with(candidate));
			if (distance <= threshold || related)
			{
				scored.emplace_back(related ? std::min<size_t>(distance, 1) : distance, candidate);
			}
		}
		std::ranges::stable_sort(scored, {}, &std::pair<size_t, std::string>::first);
		std::vector<std::string> names;
		for (size_t index = 0; index < scored.size() && index < limit; ++index)
		{
			names.push_back(std::move(scored[index].second));
		}
		return names;
	}

	const ArgumentInfo* CommandInfo::FindArgument(std::string_view name) const noexcept
	{
		const auto found = std::ranges::find(Arguments, name, &ArgumentInfo::Name);
		return found == Arguments.end() ? nullptr : &*found;
	}

	Result<Json> CommandInfo::ParseArguments(const Json& arguments) const
	{
		if (!arguments.is_null() && !arguments.is_object())
		{
			return Error::Format(
				"the arguments of '{}' must be a JSON object, found {}", Name, DescribeFound(arguments));
		}

		std::vector<std::string> names;
		names.reserve(Arguments.size());
		for (const ArgumentInfo& argument : Arguments)
		{
			names.push_back(argument.Name);
		}
		if (arguments.is_object())
		{
			for (const auto& [key, value] : arguments.items())
			{
				if (FindArgument(key) != nullptr)
				{
					continue;
				}
				std::string message = std::format("'{}' has no argument '{}'", Name, key);
				if (const std::vector<std::string> close = FindClosestNames(key, names, 1); !close.empty())
				{
					message += std::format(" (did you mean '{}'?)", close.front());
				}
				message += names.empty() ? "; it takes no arguments"
										 : std::format("; its arguments are: {}", JoinNames(names));
				return Error(std::move(message));
			}
		}

		Json completed = Json::object();
		for (const ArgumentInfo& argument : Arguments)
		{
			const Json* given =
				arguments.is_object() && arguments.contains(argument.Name) ? &arguments.at(argument.Name) : nullptr;
			if (given == nullptr || given->is_null())
			{
				if (argument.IsRequired)
				{
					return Error::Format(
						"missing required argument '{}'{}", argument.Name,
						argument.Description.empty() ? std::string() : std::format(" ({})", argument.Description));
				}
				if (argument.Default.has_value())
				{
					completed[argument.Name] = *argument.Default;
				}
				continue;
			}
			Result<Json> checked = CheckValue(argument, argument.Type, *given, argument.Name);
			if (!checked)
			{
				return checked;
			}
			completed[argument.Name] = std::move(*checked);
		}
		return completed;
	}

	Json CommandInfo::GetInputSchema() const
	{
		Json properties = Json::object();
		Json required = Json::array();
		for (const ArgumentInfo& argument : Arguments)
		{
			Json property = Json::object();
			if (const char* type = JsonSchemaType(argument.Type); type != nullptr)
			{
				property["type"] = type;
			}
			if (!argument.Description.empty())
			{
				property["description"] = argument.Description;
			}
			// The enum of an array of strings restricts its elements, so it belongs to "items".
			if (!argument.Enum.empty() && argument.Type != ArgumentType::Array)
			{
				property["enum"] = argument.Enum;
			}
			if (argument.Default.has_value())
			{
				property["default"] = *argument.Default;
			}
			if (argument.Type == ArgumentType::Array && JsonSchemaType(argument.ItemType) != nullptr)
			{
				Json items = Json::object();
				items["type"] = JsonSchemaType(argument.ItemType);
				if (!argument.Enum.empty())
				{
					items["enum"] = argument.Enum;
				}
				property["items"] = std::move(items);
			}
			if (argument.Minimum.has_value())
			{
				property["minimum"] = *argument.Minimum;
			}
			if (argument.Maximum.has_value())
			{
				property["maximum"] = *argument.Maximum;
			}
			properties[argument.Name] = std::move(property);
			if (argument.IsRequired)
			{
				required.push_back(argument.Name);
			}
		}
		Json schema = Json::object();
		schema["type"] = "object";
		schema["properties"] = std::move(properties);
		if (!required.empty())
		{
			schema["required"] = std::move(required);
		}
		schema["additionalProperties"] = false;
		return schema;
	}

	Json CommandInfo::ToJson() const
	{
		Json json = Json::object();
		json["name"] = Name;
		json["summary"] = Summary;
		if (!Description.empty())
		{
			json["description"] = Description;
		}
		json["arguments"] = GetInputSchema();
		if (!ResultDescription.empty())
		{
			json["result"] = ResultDescription;
		}
		json["flags"] = GetFlagNames(Flags);
		return json;
	}

	Result<void> CommandInfo::Validate() const
	{
		if (!IsValidCommandName(Name))
		{
			return Error::Format(
				"invalid command name '{}': use dotted lower-case parts such as 'entity.create' or 'data.add-row'",
				Name);
		}
		if (Summary.empty())
		{
			return Error::Format("command '{}' has no summary", Name);
		}
		if (HasFlag(CommandFlags::Undoable) && !HasFlag(CommandFlags::Mutating))
		{
			return Error::Format("command '{}' is undoable but not mutating", Name);
		}
		if (HasFlag(CommandFlags::Undoable) && !HasFlag(CommandFlags::RequiresProject)
		    && !HasFlag(CommandFlags::RequiresScene))
		{
			return Error::Format("command '{}' is undoable, so it needs a project or a scene", Name);
		}
		std::set<std::string_view> seen;
		for (const ArgumentInfo& argument : Arguments)
		{
			if (!IsValidArgumentName(argument.Name))
			{
				return Error::Format("command '{}': invalid argument name '{}' (use camelCase)", Name, argument.Name);
			}
			if (IsReservedArgumentName(argument.Name))
			{
				return Error::Format("command '{}': '{}' is reserved by the command line", Name, argument.Name);
			}
			if (!seen.insert(argument.Name).second)
			{
				return Error::Format("command '{}': argument '{}' is declared twice", Name, argument.Name);
			}
			if (argument.IsRequired && argument.Default.has_value())
			{
				return Error::Format("command '{}': required argument '{}' cannot have a default", Name, argument.Name);
			}
			const bool stringLike = argument.Type == ArgumentType::String
				|| (argument.Type == ArgumentType::Array && argument.ItemType == ArgumentType::String);
			if (!argument.Enum.empty() && !stringLike)
			{
				return Error::Format(
					"command '{}': only strings and arrays of strings have an enum ('{}')", Name, argument.Name);
			}
			if (argument.Default.has_value())
			{
				const Result<Json> checked = CheckValue(argument, argument.Type, *argument.Default, argument.Name);
				if (!checked)
				{
					return Error::Format(
						"command '{}': the default of '{}' is invalid: {}", Name, argument.Name,
						checked.GetError().Message);
				}
			}
		}
		return {};
	}
}
