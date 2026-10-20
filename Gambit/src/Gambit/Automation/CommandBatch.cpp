#include "gbpch.h"

#include "Gambit/Automation/CommandBatch.h"

namespace Gambit
{
	namespace
	{
		std::string_view Trim(std::string_view text)
		{
			const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
			while (!text.empty() && isSpace(text.front()))
			{
				text.remove_prefix(1);
			}
			while (!text.empty() && isSpace(text.back()))
			{
				text.remove_suffix(1);
			}
			return text;
		}
	}

	CommandResult ExecuteBatchLine(
		const CommandRegistry& registry, EditorContext& context, std::string_view line, size_t lineNumber,
		std::string& command)
	{
		command.clear();
		Result<Json> request = ParseJson(line, std::format("line {}", lineNumber));
		if (!request)
		{
			return CommandResult::Failure(CommandErrorCode::InvalidArguments, request.GetError().Message);
		}
		if (!request->is_object() || !request->contains("command") || !request->at("command").is_string())
		{
			return CommandResult::Failure(
				CommandErrorCode::InvalidArguments,
				std::format("line {}: expected {{\"command\": \"<name>\", \"args\": {{...}}}}", lineNumber));
		}
		command = request->at("command").get<std::string>();
		return registry.Execute(context, command, request->contains("args") ? request->at("args") : Json::object());
	}

	BatchSummary RunCommandBatch(
		const CommandRegistry& registry, EditorContext& context, std::string_view text, const BatchOptions& options,
		const std::function<void(const BatchStep& step)>& onStep)
	{
		BatchSummary summary;
		size_t lineNumber = 0;
		bool stopped = false;
		while (!text.empty() && !stopped)
		{
			const size_t end = text.find('\n');
			const std::string_view raw = text.substr(0, end);
			text = end == std::string_view::npos ? std::string_view() : text.substr(end + 1);
			++lineNumber;
			const std::string_view line = Trim(raw);
			if (line.empty())
			{
				continue;
			}
			++summary.Executed;

			BatchStep step;
			step.Line = lineNumber;
			step.Result = ExecuteBatchLine(registry, context, line, lineNumber, step.Command);
			if (!step.Result.Ok)
			{
				++summary.Failed;
				stopped = !options.KeepGoing;
			}
			if (onStep)
			{
				onStep(step);
			}
		}

		if (stopped)
		{
			summary.Saving = BatchSaving::Stopped;
		}
		else if (context.IsDryRun())
		{
			summary.Saving = BatchSaving::DryRun;
		}
		else if (Result<std::vector<std::string>> saved = context.SaveAll(); saved)
		{
			summary.Saved = std::move(*saved);
		}
		else
		{
			summary.Saving = BatchSaving::Failed;
			summary.SaveError = saved.GetError();
		}
		return summary;
	}
}
