#pragma once

#include "Gambit/Automation/CommandRegistry.h"
#include "Gambit/Automation/CommandResult.h"
#include "Gambit/Automation/EditorContext.h"
#include "Gambit/Core/Result.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Batches of commands (D-053): JSON Lines, one {"command": "entity.create", "args": {...}} object per line, executed in
// one session (one EditorContext) so that the selection, the undo history and unsaved changes carry from line to line.
// The gambit CLI's `batch` and the editor's headless script mode run them the same way.

namespace Gambit
{
	struct BatchOptions
	{
		// Run the remaining lines after a failure. Without it the batch stops at the first failure and saves nothing,
		// so it happens as a whole or not at all (apart from files that commands write right away).
		bool KeepGoing = false;
	};

	// One executed line.
	struct BatchStep
	{
		size_t Line = 0;     // from 1
		std::string Command; // empty when the line is not a request
		CommandResult Result;
	};

	// What happened to the changed documents at the end of a batch.
	enum class BatchSaving : uint8_t
	{
		Saved,   // every dirty document was saved (BatchSummary::Saved lists them; there may be none)
		Stopped, // a line failed and the batch stopped: nothing was saved
		DryRun,  // the context is a dry run: nothing was saved
		Failed   // saving failed (BatchSummary::SaveError)
	};

	struct BatchSummary
	{
		size_t Executed = 0; // non-blank lines run
		size_t Failed = 0;   // of those, the ones that failed
		BatchSaving Saving = BatchSaving::Saved;
		std::vector<std::string> Saved;
		std::optional<Error> SaveError;
	};

	// Executes one line of a batch: a JSON object with a "command" string and an optional "args" object. A line that
	// is not such an object fails with InvalidArguments. `command` receives the command name when the line has one.
	[[nodiscard]] CommandResult ExecuteBatchLine(
		const CommandRegistry& registry, EditorContext& context, std::string_view line, size_t lineNumber,
		std::string& command);

	// Runs the non-blank lines of `text` in order, calling `onStep` after each, then saves the changed documents
	// (EditorContext::SaveAll) unless a line stopped the batch or the context is a dry run.
	[[nodiscard]] BatchSummary RunCommandBatch(
		const CommandRegistry& registry, EditorContext& context, std::string_view text, const BatchOptions& options,
		const std::function<void(const BatchStep& step)>& onStep = {});
}
