#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Serialization/Json.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// The JSON types of command arguments: the subset of JSON Schema that agents and MCP tool schemas understand.
	enum class ArgumentType : uint8_t
	{
		String,
		Integer, // a whole number that fits int64, or uint64 for UnsignedArg; 3.0 is accepted
		Number,
		Boolean,
		Array,
		Object,
		Any // no check
	};

	// "string", "integer", "number", "boolean", "array", "object", "any".
	[[nodiscard]] std::string_view ToString(ArgumentType type) noexcept;

	// One argument of a command. Names are camelCase identifiers; the CLI also accepts the kebab-case spelling
	// (--keep-world-transform).
	struct ArgumentInfo
	{
		std::string Name;
		ArgumentType Type = ArgumentType::String;
		std::string Description{};
		bool IsRequired = false;
		std::vector<std::string> Enum{};           // String arguments (and arrays of strings): the allowed values
		std::optional<Json> Default{};             // used when the argument is absent; not for required arguments
		ArgumentType ItemType = ArgumentType::Any; // Array arguments: the type of the elements
		std::optional<double> Minimum{};           // Integer and Number arguments
		std::optional<double> Maximum{};

		// Builders, so that a declaration reads as a sentence: StringArg("name", "...").Require().
		[[nodiscard]] ArgumentInfo Require() &&
		{
			IsRequired = true;
			return std::move(*this);
		}
		[[nodiscard]] ArgumentInfo WithDefault(Json value) &&
		{
			Default = std::move(value);
			return std::move(*this);
		}
		[[nodiscard]] ArgumentInfo WithEnum(std::vector<std::string> values) &&
		{
			Enum = std::move(values);
			return std::move(*this);
		}
		[[nodiscard]] ArgumentInfo WithItems(ArgumentType itemType) &&
		{
			ItemType = itemType;
			return std::move(*this);
		}
		[[nodiscard]] ArgumentInfo WithRange(double minimum, double maximum) &&
		{
			Minimum = minimum;
			Maximum = maximum;
			return std::move(*this);
		}
		[[nodiscard]] ArgumentInfo WithMinimum(double minimum) &&
		{
			Minimum = minimum;
			return std::move(*this);
		}
	};

	// The maximum of UnsignedArg: 2^64 - 1, which a double rounds to 2^64.
	inline constexpr double MaxUnsignedArgument = 18446744073709551615.0;

	[[nodiscard]] ArgumentInfo StringArg(std::string name, std::string description);
	// An int64 value (read with GetInteger).
	[[nodiscard]] ArgumentInfo IntegerArg(std::string name, std::string description);
	// A uint64 value from 0 to 2^64 - 1, such as a seed (an integer argument with that range; read with GetUnsigned).
	[[nodiscard]] ArgumentInfo UnsignedArg(std::string name, std::string description);
	[[nodiscard]] ArgumentInfo NumberArg(std::string name, std::string description);
	[[nodiscard]] ArgumentInfo BooleanArg(std::string name, std::string description);
	[[nodiscard]] ArgumentInfo ObjectArg(std::string name, std::string description);
	[[nodiscard]] ArgumentInfo AnyArg(std::string name, std::string description);
	[[nodiscard]] ArgumentInfo ArrayArg(std::string name, ArgumentType itemType, std::string description);
	// A string that must be one of `values`.
	[[nodiscard]] ArgumentInfo EnumArg(std::string name, std::vector<std::string> values, std::string description);

	// What a command is and does to the project (D-051).
	enum class CommandFlags : uint8_t
	{
		None = 0,
		Mutating = GB_BIT(0),        // changes a scene, an asset or the project
		Undoable = GB_BIT(1),        // the change is recorded on the undo stack (a scene or table edit)
		RequiresProject = GB_BIT(2), // needs an open project
		RequiresScene = GB_BIT(3),   // needs an active scene (and so a project)
		EditorOnly = GB_BIT(4),      // needs the running editor (viewport, play mode); refused in headless hosts
		WritesFiles = GB_BIT(5) // writes project files right away, not only when scenes are saved: not for dry runs
	};

	[[nodiscard]] constexpr CommandFlags operator|(CommandFlags a, CommandFlags b) noexcept
	{
		return static_cast<CommandFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
	}

	// Whether every bit of `flag` is set.
	[[nodiscard]] constexpr bool HasFlag(CommandFlags flags, CommandFlags flag) noexcept
	{
		return (static_cast<uint8_t>(flags) & static_cast<uint8_t>(flag)) == static_cast<uint8_t>(flag)
			&& flag != CommandFlags::None;
	}

	// The description of a command: enough for help texts, the CLI parser and MCP tool schemas.
	struct CommandInfo
	{
		std::string Name;          // dotted, lower-case with '-' inside the parts: "entity.create", "data.add-row"
		std::string Summary;       // one line
		std::string Description{}; // optional, more detail for help texts and tool descriptions
		std::vector<ArgumentInfo> Arguments{};
		std::string ResultDescription{}; // what the result data holds
		CommandFlags Flags = CommandFlags::None;

		[[nodiscard]] bool HasFlag(CommandFlags flag) const noexcept { return Gambit::HasFlag(Flags, flag); }
		[[nodiscard]] const ArgumentInfo* FindArgument(std::string_view name) const noexcept;

		// Checks `arguments` (a JSON object, or null for none) against the schema and returns them completed with the
		// defaults of the absent ones. Integers given as whole floats are normalized, and null stands for an absent
		// argument. Errors name the argument, say what was expected and what would be valid.
		[[nodiscard]] Result<Json> ParseArguments(const Json& arguments) const;

		// The arguments as a JSON Schema object (draft 2020-12 subset: type, properties, required, enum, default,
		// description, items, minimum, maximum, additionalProperties) - the input schema of an MCP tool.
		[[nodiscard]] Json GetInputSchema() const;

		// { "name", "summary", "description", "arguments": <schema>, "result", "flags": [ ... ] }
		[[nodiscard]] Json ToJson() const;

		// Checks the declaration: the name, the arguments (unique camelCase names, defaults of the right type, enums
		// only on strings, no default on required arguments) and flag combinations.
		[[nodiscard]] Result<void> Validate() const;
	};

	// Names of the flags set, in a stable order: "mutating", "undoable", "requires-project", ...
	[[nodiscard]] std::vector<std::string> GetFlagNames(CommandFlags flags);

	// Up to `limit` of the candidates that look like a misspelling of `text` (a small edit distance), closest first.
	[[nodiscard]] std::vector<std::string>
	FindClosestNames(std::string_view text, const std::vector<std::string>& candidates, size_t limit = 3);

	// Names the command line of the CLI reserves for itself and commands therefore cannot use as argument names.
	[[nodiscard]] bool IsReservedArgumentName(std::string_view name) noexcept;

	// A number for messages and help texts: whole numbers without a fraction or exponent (100000, not 1e+05).
	[[nodiscard]] std::string FormatNumber(double value);
}
