#pragma once

#include "Cli/CommandDispatcher.h"
#include "Cli/CommandLine.h"
#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Automation/EditorContext.h"
#include "Gambit/Reflection/TypeRegistry.h"

#include <string>
#include <string_view>
#include <vector>

namespace Gambit::Cli
{
	// The automation commands of the command registry (D-051) as `gambit` commands:
	//   gambit <command> [--project <dir|.gproject>] [--scene <path>] [--args '<json>' | --<arg> <value>...]
	//                    [--json] [--dry-run]
	// where <command> is `entity.create` or `entity create`. It opens the project (and scene), executes the command,
	// saves the scenes and tables that changed (unless --dry-run) and prints a summary or, with --json, the result.
	// Exit codes: 0 ok, 1 the command failed, 2 usage error (unknown command, bad options or arguments).
	//
	// It also provides `gambit batch <file.jsonl>` (many commands in one process), `gambit commands` (the command list
	// and, with --json, all schemas) and the generated help of the commands.
	class AutomationProvider final : public ICommandProvider
	{
	public:
		// `types` is the registry of the components the commands work on and must outlive the provider.
		AutomationProvider(std::string programName, TypeRegistry& types);

		[[nodiscard]] CommandRegistry& GetRegistry() noexcept { return m_Registry; }
		[[nodiscard]] const CommandRegistry& GetRegistry() const noexcept { return m_Registry; }

		[[nodiscard]] std::optional<int>
		TryRun(std::span<const std::string> args, const CommandContext& context) override;
		[[nodiscard]] bool TryPrintHelp(std::span<const std::string> words, const CommandContext& context) override;
		void PrintCommandList(std::ostream& out) const override;

		// `gambit batch <file.jsonl|-> [--project P] [--keep-going] [--dry-run] [--json]`: one command per line,
		// {"command": "entity.create", "args": {...}}. Stops at the first failure unless --keep-going; without
		// --keep-going a failed batch saves nothing.
		[[nodiscard]] int RunBatch(std::span<const std::string> args, const CommandContext& context);
		// `gambit commands [<prefix>] [--json]`.
		[[nodiscard]] int RunCommands(std::span<const std::string> args, const CommandContext& context) const;

	private:
		[[nodiscard]] int RunCommand(
			const ResolvedCommand& command, const SplitCommandLine& line, std::span<const std::string> options,
			const CommandContext& context);
		// Opens the project and scene that the options and the commands to run call for. What the loads of scenes
		// could not read goes to `warnings`.
		[[nodiscard]] Result<void> PrepareContext(
			EditorContext& context, const GlobalOptions& options, bool needsProject, bool needsScene,
			std::vector<std::string>& warnings) const;

		std::string m_ProgramName;
		TypeRegistry& m_Types;
		CommandRegistry m_Registry;
	};

	// Adds the automation commands, `batch` and `commands` to the dispatcher.
	void RegisterAutomationCommands(CommandDispatcher& dispatcher, TypeRegistry& types = TypeRegistry::Get());

	// The help text of a command generated from its schema: usage, arguments with types, defaults and descriptions,
	// result and flags.
	[[nodiscard]] std::string FormatCommandHelp(const CommandInfo& command, std::string_view programName);
	// A command result for people: lists and tables of the data instead of JSON.
	[[nodiscard]] std::string FormatHuman(const Json& data);
	// The exit code for a result (ExitSuccess, ExitFailure or ExitUsage).
	[[nodiscard]] int GetExitCode(const CommandResult& result) noexcept;
}
