#pragma once

#include "Gambit/Automation/CommandLine.h"
#include "Gambit/Core/Result.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

// The process side of a `gambit` command line (D-051): the global options that apply to the run as a whole. The command
// words and their options are read by Gambit/Automation/CommandLine.h, which the editor console shares. Errors are
// usage errors (exit code 2).

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
}
