#include "Cli/CommandLine.h"

namespace Gambit::Cli
{
	namespace
	{
		Error UsageError(std::string message)
		{
			return Error(std::move(message));
		}
	}

	Result<SplitCommandLine> SplitGlobalOptions(std::span<const std::string> args)
	{
		SplitCommandLine split;
		for (size_t index = 0; index < args.size(); ++index)
		{
			const std::string& token = args[index];
			const auto takeValue = [&](std::optional<std::string>& destination) -> Result<void> {
				if (index + 1 >= args.size())
				{
					return UsageError(std::format("option {} needs a value", token));
				}
				destination = args[++index];
				return {};
			};
			// --project=value is accepted for the options that take a value.
			const size_t equals = token.find('=');
			const std::string_view name = std::string_view(token).substr(0, equals);
			if (equals != std::string::npos && (name == "--project" || name == "--scene" || name == "--args"))
			{
				std::optional<std::string>& destination = name == "--project"
					? split.Options.Project
					: (name == "--scene" ? split.Options.Scene : split.Options.ArgsJson);
				destination = token.substr(equals + 1);
				continue;
			}
			if (token == "--project" || token == "--scene" || token == "--args")
			{
				std::optional<std::string>& destination = token == "--project"
					? split.Options.Project
					: (token == "--scene" ? split.Options.Scene : split.Options.ArgsJson);
				if (Result<void> taken = takeValue(destination); !taken)
				{
					return taken.GetError();
				}
			}
			else if (token == "--json")
			{
				split.Options.AsJson = true;
			}
			else if (token == "--dry-run")
			{
				split.Options.DryRun = true;
			}
			else if (token == "--keep-going")
			{
				split.Options.KeepGoing = true;
			}
			else if (token == "--help" || token == "-h")
			{
				split.Options.Help = true;
			}
			else
			{
				split.Rest.push_back(token);
			}
		}
		return split;
	}
}
