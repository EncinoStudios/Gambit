#include "gbpch.h"

#include "Gambit/Automation/CommandRegistry.h"

#include "Gambit/Automation/EditorContext.h"

namespace Gambit
{
	Result<void> CommandRegistry::Register(CommandInfo info, CommandHandler handler)
	{
		if (Result<void> valid = info.Validate(); !valid)
		{
			return valid;
		}
		if (!handler)
		{
			return Error::Format("command '{}' has no handler", info.Name);
		}
		if (m_Commands.contains(info.Name))
		{
			return Error::Format("command '{}' is already registered", info.Name);
		}
		std::string name = info.Name;
		m_Commands.emplace(std::move(name), Entry{ std::move(info), std::move(handler) });
		return {};
	}

	const CommandInfo* CommandRegistry::Find(std::string_view name) const
	{
		const auto found = m_Commands.find(name);
		return found == m_Commands.end() ? nullptr : &found->second.Info;
	}

	std::vector<const CommandInfo*> CommandRegistry::List(std::string_view prefix) const
	{
		std::vector<const CommandInfo*> commands;
		for (const auto& [name, entry] : m_Commands)
		{
			if (name.starts_with(prefix))
			{
				commands.push_back(&entry.Info);
			}
		}
		return commands;
	}

	std::vector<std::string> CommandRegistry::Suggest(std::string_view name, size_t limit) const
	{
		std::vector<std::string> names;
		names.reserve(m_Commands.size());
		for (const auto& [commandName, entry] : m_Commands)
		{
			names.push_back(commandName);
		}
		return FindClosestNames(name, names, limit);
	}

	CommandResult CommandRegistry::Execute(EditorContext& context, std::string_view name, const Json& arguments) const
	{
		const auto found = m_Commands.find(name);
		if (found == m_Commands.end())
		{
			std::string message = std::format("unknown command '{}'", name);
			if (const std::vector<std::string> close = Suggest(name, 3); !close.empty())
			{
				message += "; did you mean ";
				for (size_t index = 0; index < close.size(); ++index)
				{
					message += std::format(
						"{}'{}'", index == 0 ? "" : (index + 1 == close.size() ? " or " : ", "), close[index]);
				}
				message += "?";
			}
			return CommandResult::Failure(CommandErrorCode::UnknownCommand, std::move(message));
		}
		const Entry& entry = found->second;
		const CommandInfo& info = entry.Info;

		const Result<Json> parsed = info.ParseArguments(arguments);
		if (!parsed)
		{
			return CommandResult::Failure(CommandErrorCode::InvalidArguments, parsed.GetError().Message);
		}

		if (info.HasFlag(CommandFlags::EditorOnly) && context.GetHost() != HostKind::Editor)
		{
			return CommandResult::Failure(
				CommandErrorCode::Unsupported,
				std::format("'{}' needs the running editor; this host is headless", info.Name));
		}
		if (info.HasFlag(CommandFlags::WritesFiles) && context.IsDryRun())
		{
			return CommandResult::Failure(
				CommandErrorCode::Unsupported,
				std::format("'{}' writes project files right away, which a dry run does not do", info.Name));
		}
		if ((info.HasFlag(CommandFlags::RequiresProject) || info.HasFlag(CommandFlags::RequiresScene))
		    && context.GetProject() == nullptr)
		{
			return CommandResult::Failure(
				CommandErrorCode::NoProject,
				std::format(
					"'{}' needs an open project (the CLI takes --project <dir|.gproject>; project.new creates one)",
					info.Name));
		}
		if (info.HasFlag(CommandFlags::RequiresScene) && context.GetActiveScene() == nullptr)
		{
			return CommandResult::Failure(
				CommandErrorCode::NoScene,
				std::format(
					"'{}' needs an active scene: open one with scene.open (the CLI takes --scene <path>) or create "
					"one with scene.new",
					info.Name));
		}

		UndoStack& undo = context.GetUndoStack();
		const bool grouped = info.HasFlag(CommandFlags::Undoable);
		if (grouped)
		{
			undo.BeginGroup(info.Summary);
		}
		CommandResult result;
		try
		{
			result = entry.Handler(context, *parsed);
		}
		catch (const std::exception& exception)
		{
			GB_CORE_ERROR("command '{}' threw an exception: {}", info.Name, exception.what());
			result = CommandResult::Failure(
				CommandErrorCode::Internal, std::format("'{}' failed unexpectedly: {}", info.Name, exception.what()));
		}
		catch (...)
		{
			GB_CORE_ERROR("command '{}' threw an unknown exception", info.Name);
			result =
				CommandResult::Failure(CommandErrorCode::Internal, std::format("'{}' failed unexpectedly", info.Name));
		}

		if (grouped)
		{
			if (result.Ok)
			{
				undo.EndGroup();
			}
			else if (Result<void> rolledBack = undo.CancelGroup(context); !rolledBack)
			{
				result.Message += std::format(" ({})", rolledBack.GetError().Message);
			}
		}
		return result;
	}

	Json CommandRegistry::ToJson(std::string_view prefix) const
	{
		Json commands = Json::array();
		for (const CommandInfo* info : List(prefix))
		{
			commands.push_back(info->ToJson());
		}
		return commands;
	}
}
