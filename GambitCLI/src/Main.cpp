#include "Cli/CommandDispatcher.h"

#include "Gambit/Core/Log.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	using namespace Gambit;

	// stdout carries command results (and, for `gambit mcp`, the protocol), so diagnostics go to stderr.
	LogConfig logConfig;
	logConfig.Level = LogLevel::Warn;
	logConfig.ConsoleTarget = ConsoleStream::StdErr;
	logConfig.RingBufferCapacity = 0;
	Log::Init(logConfig);

	Cli::CommandDispatcher dispatcher("gambit");
	Cli::RegisterBuiltinCommands(dispatcher);

	int exitCode = Cli::ExitFailure;
	try
	{
		// argc can be 0 when a process is spawned without arguments, not even its own name.
		const std::vector<std::string> args =
			argc > 1 ? std::vector<std::string>(argv + 1, argv + argc) : std::vector<std::string>();
		exitCode = dispatcher.Run(args, Cli::CommandContext{ std::cout, std::cerr });
	}
	catch (const std::exception& exception)
	{
		GB_CRITICAL("unhandled exception: {}", exception.what());
	}

	Log::Shutdown();
	return exitCode;
}
