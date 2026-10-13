#pragma once

#include "Gambit/Automation/CommandInfo.h"
#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Core/Result.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Reading a `gambit` command line (D-051): global options, the words that name a registry command, and the
// `--name value` options that become the command's JSON arguments. Errors are usage errors (exit code 2).

namespace Gambit::Cli
{
	// The options that apply to the run as a whole, wherever they stand on the command line.
	struct GlobalOptions
	{
		std::optional<std::string> Project;  // --project <dir|file.gproject>
		std::optional<std::string> Scene;    // --scene <path>: opened and made active before the command runs
		std::optional<std::string> ArgsJson; // --args '<json object>': arguments as JSON; --name options override them
		bool AsJson = false;                 // --json: print the result as JSON
		bool DryRun = false;                 // --dry-run: save nothing
		bool KeepGoing = false;              // --keep-going: batch continues after a failure
		bool Help = false;                   // --help, -h
	};

	struct SplitCommandLine
	{
		GlobalOptions Options;
		std::vector<std::string> Rest; // the other tokens in their order: command words, then options
	};

	// Takes the global options out of the arguments. Fails when one lacks its value.
	[[nodiscard]] Result<SplitCommandLine> SplitGlobalOptions(std::span<const std::string> args);

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

	// Reads the options after the command words as arguments of `command`, starting from `baseArguments` (the --args
	// object, if any):
	//   --name value, --name=value; boolean options alone mean true (--dry-run style), --no-name means false;
	//   names may be camelCase or kebab-case; values are converted by the argument's type (numbers, booleans, JSON for
	//   objects and arrays); an array option may be repeated.
	// Does not check required arguments or enums: CommandInfo::ParseArguments does that.
	[[nodiscard]] Result<Json> ParseCommandOptions(
		const CommandInfo& command, std::span<const std::string> tokens,
		const std::optional<std::string>& baseArguments);
}
