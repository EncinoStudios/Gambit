#include "Cli/CommandDispatcher.h"

#include "Gambit/Core/Assert.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <ostream>

namespace Gambit::Cli
{
	namespace
	{
		bool IsValidCommandName(std::string_view name)
		{
			if (name.empty() || !std::islower(static_cast<unsigned char>(name.front())))
			{
				return false;
			}
			return std::ranges::all_of(name, [](char c) {
				const auto u = static_cast<unsigned char>(c);
				return std::islower(u) || std::isdigit(u) || c == '-';
			});
		}

		bool IsValidAlias(std::string_view alias)
		{
			return !alias.empty()
				&& std::ranges::none_of(alias, [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
		}

		bool IsHelpFlag(std::string_view argument)
		{
			return argument == "--help" || argument == "-h";
		}
	}

	CommandDispatcher::CommandDispatcher(std::string programName)
		: m_ProgramName(std::move(programName))
	{
		const Result<void> registered = Register(Command{
			.Name = "help",
			.Summary = "Show help for " + m_ProgramName + " or for one of its commands",
			.Usage = "[<command>...]",
			.Aliases = { "--help", "-h" },
			.Handler =
				[this](const CommandContext& context, std::span<const std::string> args) {
					if (args.empty())
					{
						PrintHelp(context.Out);
						return ExitSuccess;
					}
					if (const Command* command = Find(args[0]); command != nullptr)
					{
						if (args.size() > 1)
						{
							context.Err << m_ProgramName << ": 'help' takes at most one command name\n";
							return ExitUsage;
						}
						PrintCommandHelp(context.Out, *command);
						return ExitSuccess;
					}
					for (const Ref<ICommandProvider>& provider : m_Providers)
					{
						if (provider->TryPrintHelp(args, context))
						{
							return ExitSuccess;
						}
					}
					context.Err << m_ProgramName << ": unknown command '" << args[0] << "'\n";
					return ExitUsage;
				},
		});
		GB_CORE_VERIFY(registered.HasValue(), "built-in help command rejected: {}", registered.GetError().Message);
	}

	Result<void> CommandDispatcher::Register(Command command)
	{
		if (!IsValidCommandName(command.Name))
		{
			return Error::Format("invalid command name '{}': use lower-case letters, digits and '-'", command.Name);
		}
		if (!command.Handler)
		{
			return Error::Format("command '{}' has no handler", command.Name);
		}

		for (const std::string& alias : command.Aliases)
		{
			if (!IsValidAlias(alias))
			{
				return Error::Format("command '{}' has an invalid alias '{}'", command.Name, alias);
			}
		}

		const auto isTaken = [this](std::string_view spelling) { return Find(spelling) != nullptr; };
		if (isTaken(command.Name))
		{
			return Error::Format("command '{}' is already registered", command.Name);
		}
		for (const std::string& alias : command.Aliases)
		{
			if (alias == command.Name || isTaken(alias))
			{
				return Error::Format("alias '{}' of command '{}' is already registered", alias, command.Name);
			}
		}

		const auto position = std::ranges::upper_bound(m_Commands, command.Name, {}, &Command::Name);
		m_Commands.insert(position, std::move(command));
		return {};
	}

	void CommandDispatcher::AddProvider(Ref<ICommandProvider> provider)
	{
		GB_CORE_ASSERT(provider != nullptr, "a null command provider");
		m_Providers.push_back(std::move(provider));
	}

	const Command* CommandDispatcher::Find(std::string_view nameOrAlias) const
	{
		for (const Command& command : m_Commands)
		{
			if (command.Name == nameOrAlias || std::ranges::find(command.Aliases, nameOrAlias) != command.Aliases.end())
			{
				return &command;
			}
		}
		return nullptr;
	}

	int CommandDispatcher::Run(std::span<const std::string> args, const CommandContext& context) const
	{
		if (args.empty())
		{
			PrintHelp(context.Err);
			return ExitUsage;
		}

		const Command* command = Find(args.front());
		if (command == nullptr)
		{
			for (const Ref<ICommandProvider>& provider : m_Providers)
			{
				if (const std::optional<int> exitCode = provider->TryRun(args, context); exitCode.has_value())
				{
					return *exitCode;
				}
			}
			context.Err << m_ProgramName << ": unknown command '" << args.front() << "'\n"
						<< "Run '" << m_ProgramName << " --help' for the list of commands.\n";
			return ExitUsage;
		}

		const std::span<const std::string> commandArgs = args.subspan(1);
		if (command->Name != "help" && !commandArgs.empty() && IsHelpFlag(commandArgs.front()))
		{
			PrintCommandHelp(context.Out, *command);
			return ExitSuccess;
		}
		return command->Handler(context, commandArgs);
	}

	void CommandDispatcher::PrintHelp(std::ostream& out) const
	{
		out << "Usage: " << m_ProgramName << " <command> [options]\n\nCommands:\n";

		size_t width = 0;
		for (const Command& command : m_Commands)
		{
			width = std::max(width, command.Name.size());
		}
		for (const Command& command : m_Commands)
		{
			out << "  " << std::left << std::setw(static_cast<int>(width)) << command.Name << "  " << command.Summary
				<< '\n';
		}
		for (const Ref<ICommandProvider>& provider : m_Providers)
		{
			provider->PrintCommandList(out);
		}
		out << "\nRun '" << m_ProgramName << " help <command>' for details on a command.\n";
	}

	void CommandDispatcher::PrintCommandHelp(std::ostream& out, const Command& command) const
	{
		out << "Usage: " << m_ProgramName << ' ' << command.Name;
		if (!command.Usage.empty())
		{
			out << ' ' << command.Usage;
		}
		out << "\n\n" << command.Summary << '\n';
		if (!command.Aliases.empty())
		{
			out << "\nAliases:";
			for (const std::string& alias : command.Aliases)
			{
				out << ' ' << alias;
			}
			out << '\n';
		}
	}
}
