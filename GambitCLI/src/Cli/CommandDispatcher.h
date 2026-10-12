#pragma once

#include "Gambit/Core/Result.h"

#include <functional>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit::Cli
{
	inline constexpr int ExitSuccess = 0;
	inline constexpr int ExitFailure = 1; // the command ran and failed
	inline constexpr int ExitUsage = 2;   // the command line itself was wrong

	// Streams a command writes to. Injected so that commands can be unit tested without spawning a process.
	struct CommandContext
	{
		std::ostream& Out; // results, intended to be piped
		std::ostream& Err; // diagnostics and usage errors
	};

	// `args` are the arguments after the command name. Returns the process exit code.
	using CommandHandler = std::function<int(const CommandContext& context, std::span<const std::string> args)>;

	struct Command
	{
		// The initializers keep designated-initializer construction free of -Wmissing-field-initializers.
		std::string Name;                   // lower-case letters, digits and '-', starting with a letter
		std::string Summary{};              // one line, shown in the command list
		std::string Usage{};                // text after "Usage: <program> <name> ", e.g. "[--config <name>] <path>"
		std::vector<std::string> Aliases{}; // alternative spellings, e.g. "--version"
		CommandHandler Handler{};
	};

	// Maps `gambit <command> ...` to handlers and provides the built-in `help` command. Later tasks add their
	// commands in RegisterBuiltinCommands; the dispatcher itself does not change.
	class CommandDispatcher
	{
	public:
		explicit CommandDispatcher(std::string programName);
		// The help command captures `this`.
		CommandDispatcher(const CommandDispatcher&) = delete;
		CommandDispatcher& operator=(const CommandDispatcher&) = delete;

		[[nodiscard]] const std::string& GetProgramName() const noexcept { return m_ProgramName; }

		// Fails when the name is malformed, the handler is empty, or a name/alias is already taken.
		[[nodiscard]] Result<void> Register(Command command);

		// Finds a command by name or alias; null when there is none.
		[[nodiscard]] const Command* Find(std::string_view nameOrAlias) const;

		// Runs `args[0]` with the remaining arguments. Without arguments prints the command list to Err and returns
		// ExitUsage; `<command> --help` prints that command's usage.
		[[nodiscard]] int Run(std::span<const std::string> args, const CommandContext& context) const;

		void PrintHelp(std::ostream& out) const;
		void PrintCommandHelp(std::ostream& out, const Command& command) const;

	private:
		std::string m_ProgramName;
		std::vector<Command> m_Commands; // sorted by name
	};

	// Registers every command the gambit tool ships with (currently `version`).
	void RegisterBuiltinCommands(CommandDispatcher& dispatcher);
}
