#include "Cli/CommandDispatcher.h"

#include "Gambit/Core/Assert.h"
#include "Gambit/Core/Version.h"

#include <ostream>

namespace Gambit::Cli
{
	namespace
	{
		void RegisterOrDie(CommandDispatcher& dispatcher, Command command)
		{
			const std::string name = command.Name;
			const Result<void> registered = dispatcher.Register(std::move(command));
			GB_CORE_VERIFY(
				registered.HasValue(), "cannot register command '{}': {}", name, registered.GetError().Message);
		}

		int RunVersion(const std::string& programName, const CommandContext& context, std::span<const std::string> args)
		{
			if (!args.empty())
			{
				context.Err << programName << ": 'version' takes no arguments\n";
				return ExitUsage;
			}
			context.Out << programName << ' ' << VersionString << '\n';
			return ExitSuccess;
		}
	}

	void RegisterBuiltinCommands(CommandDispatcher& dispatcher)
	{
		RegisterOrDie(
			dispatcher,
			Command{
				.Name = "version",
				.Summary = "Print the engine version",
				.Usage = "",
				.Aliases = { "--version", "-V" },
				.Handler = [programName = dispatcher.GetProgramName()](
							   const CommandContext& context,
							   std::span<const std::string> args) { return RunVersion(programName, context, args); },
			});
	}
}
