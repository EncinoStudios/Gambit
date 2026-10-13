#pragma once

#include "Gambit/Automation/CommandInfo.h"
#include "Gambit/Automation/CommandResult.h"
#include "Gambit/Core/Result.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class EditorContext;

	// The one API of the engine's tools (D-051): named commands with JSON argument schemas that run against an
	// EditorContext. The editor, the `gambit` CLI, the MCP server and the editor's RPC endpoint execute commands
	// through Execute and describe them through the schemas, so they cannot disagree about what a command does.
	//
	// Execute checks the arguments against the schema before the handler runs and the command's preconditions (project,
	// scene, host), keeps undoable commands atomic (an undo group that is rolled back when the handler fails) and never
	// lets an exception escape. Not thread safe: hosts serialize their requests.
	class CommandRegistry
	{
	public:
		// Fails for an invalid description (CommandInfo::Validate), an empty handler or a name that is taken.
		[[nodiscard]] Result<void> Register(CommandInfo info, CommandHandler handler);

		// nullptr when there is no such command.
		[[nodiscard]] const CommandInfo* Find(std::string_view name) const;
		// The commands whose names start with `prefix` (all of them for an empty one), sorted by name. "entity." lists
		// the entity commands.
		[[nodiscard]] std::vector<const CommandInfo*> List(std::string_view prefix = {}) const;
		[[nodiscard]] size_t GetCount() const noexcept { return m_Commands.size(); }
		// Names that look like a misspelling of `name`.
		[[nodiscard]] std::vector<std::string> Suggest(std::string_view name, size_t limit = 3) const;

		// Runs the command. `arguments` is a JSON object (null for none).
		[[nodiscard]] CommandResult
		Execute(EditorContext& context, std::string_view name, const Json& arguments = Json::object()) const;

		// The descriptions (CommandInfo::ToJson) of the commands with the prefix as a JSON array: what `gambit commands
		// --json` prints and what MCP tool lists are made from.
		[[nodiscard]] Json ToJson(std::string_view prefix = {}) const;

	private:
		struct Entry
		{
			CommandInfo Info;
			CommandHandler Handler;
		};

		std::map<std::string, Entry, std::less<>> m_Commands; // by name
	};
}
