#pragma once

#include "Gambit/Core/Result.h"
#include "Gambit/Serialization/Json.h"

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class EditorContext;

	// Why a command failed, in terms an agent can act on. The CLI maps InvalidArguments and UnknownCommand to its
	// usage error (exit code 2) and every other code to a command error (exit code 1).
	enum class CommandErrorCode : uint8_t
	{
		None,             // success
		UnknownCommand,   // no command of that name
		InvalidArguments, // the arguments do not match the command's schema, or have invalid values
		NoProject,        // the command needs an open project
		NoScene,          // the command needs an active scene
		NotFound,         // an entity, component, asset, table or thread does not exist
		AlreadyExists,    // something of that name exists already
		Conflict,         // the state forbids the command (unsaved changes, play mode, a cycle in the hierarchy)
		Unsupported,      // not available here: editor-only commands in a headless host, file writes in a dry run
		Failed,           // the operation failed (file input and output, content that does not fit)
		Internal          // a bug: a handler threw or an invariant broke
	};

	// "unknown-command", "invalid-arguments", ...: the names in JSON results.
	[[nodiscard]] std::string_view ToString(CommandErrorCode code) noexcept;

	// Errors of the context and the operations carry their CommandErrorCode in Error::Code (offset, so that it cannot
	// be mistaken for a FileError); CommandResult::FromError turns them into results.
	[[nodiscard]] Error MakeCommandError(CommandErrorCode code, std::string message);
	// The code of an Error: the command code it carries, NotFound for a missing file, else `fallback`.
	[[nodiscard]] CommandErrorCode
	GetCommandErrorCode(const Error& error, CommandErrorCode fallback = CommandErrorCode::Failed);

	// What executing a command returns (D-051): success with a JSON payload and a one-line summary for humans, or
	// failure with a code and a message that says what is wrong and what would work.
	struct CommandResult
	{
		bool Ok = false;
		CommandErrorCode Code = CommandErrorCode::Internal;
		std::string Message;               // success: a summary line (may be empty); failure: the explanation
		Json Data = Json::object();        // success: the payload
		std::vector<std::string> Warnings; // problems that did not stop the command (unknown members in a loaded file)

		[[nodiscard]] static CommandResult Success(Json data = Json::object(), std::string message = {});
		[[nodiscard]] static CommandResult Failure(CommandErrorCode code, std::string message);
		// A failure for an Error (see GetCommandErrorCode).
		[[nodiscard]] static CommandResult
		FromError(const Error& error, CommandErrorCode fallback = CommandErrorCode::Failed);

		// { "ok": true, "message": "...", "data": {...}, "warnings": [...] } or
		// { "ok": false, "error": { "code": "not-found", "message": "..." } }; empty members are left out.
		[[nodiscard]] Json ToJson() const;
	};

	// Runs a command. Receives the arguments already validated against the schema, with defaults applied.
	using CommandHandler = std::function<CommandResult(EditorContext& context, const Json& arguments)>;
}
