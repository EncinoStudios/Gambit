#pragma once

#include "Gambit/Automation/CommandInfo.h"
#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Core/Result.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

// Reading a command line into a registry command (D-051): the words that name the command and the `--name value`
// options that become its JSON arguments. Every host that takes typed commands (the gambit CLI, the editor console)
// uses this parser, so a command line means the same everywhere. Errors are usage errors.

namespace Gambit
{
	// "keep-world-transform" to "keepWorldTransform" (names without '-' are unchanged) and back.
	[[nodiscard]] std::string ToCamelCase(std::string_view kebab);
	[[nodiscard]] std::string ToKebabCase(std::string_view camel);

	// The registry command that the leading words of `tokens` (those that do not start with '-') name, in the dotted
	// form ("entity.create") or as separate words ("entity create"); the longest match wins, up to three words.
	struct ResolvedCommand
	{
		const CommandInfo* Info = nullptr;
		size_t WordCount = 0; // how many leading tokens the name used
	};
	[[nodiscard]] std::optional<ResolvedCommand>
	ResolveCommand(const CommandRegistry& registry, std::span<const std::string> tokens);

	// Reads the options after the command words as arguments of `command`, starting from `baseArguments` (a JSON object
	// as text, if any):
	//   --name value, --name=value; boolean options alone mean true (--dry-run style), --no-name means false;
	//   names may be camelCase or kebab-case; values are converted by the argument's type (numbers, booleans, JSON for
	//   objects and arrays); an array option may be repeated.
	// Does not check required arguments, ranges or enums: CommandInfo::ParseArguments does that.
	[[nodiscard]] Result<Json> ParseCommandOptions(
		const CommandInfo& command, std::span<const std::string> tokens,
		const std::optional<std::string>& baseArguments);
}
