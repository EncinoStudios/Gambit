#pragma once

#include <chrono>
#include <filesystem>
#include <span>
#include <string>

namespace Gambit::Tests
{
	struct ProcessOptions
	{
		std::filesystem::path WorkingDirectory; // empty: the directory of the test process
		std::string Input;                      // standard input; empty: nothing to read
		std::chrono::seconds Timeout{ 60 };     // the process is killed after this long
	};

	struct ProcessResult
	{
		bool Started = false;  // false: the program could not be started (StartError says why)
		bool TimedOut = false; // killed after the timeout
		int ExitCode = -1;     // the exit code; 128 + signal number when the process was killed by a signal
		std::string Output;    // standard output
		std::string Errors;    // standard error
		std::string StartError;
	};

	// Runs a program to completion and captures its output, for tests of the real binaries (`gambit`). No shell is
	// involved: every argument reaches the program exactly as given, whatever characters it contains.
	[[nodiscard]] ProcessResult RunProcess(
		const std::filesystem::path& executable, std::span<const std::string> arguments,
		const ProcessOptions& options = {});
}
