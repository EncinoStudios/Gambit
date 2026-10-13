#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		Json DescribeSelection(EditorContext& context)
		{
			Json entities = Json::array();
			World* world = context.GetActiveScene() != nullptr ? context.GetActiveScene()->Scene.get() : nullptr;
			for (const UUID uuid : context.GetSelection())
			{
				Json entry = Json::object();
				entry["uuid"] = uuid.ToString();
				entry["name"] = world != nullptr ? world->TryGetEntity(uuid).GetName() : std::string();
				entities.push_back(std::move(entry));
			}
			Json data = Json::object();
			data["count"] = entities.size();
			data["entities"] = std::move(entities);
			return data;
		}

		CommandResult SelectionGet(EditorContext& context, const Json&)
		{
			return CommandResult::Success(
				DescribeSelection(context), std::format("{} selected", context.GetSelection().size()));
		}

		CommandResult SelectionSet(EditorContext& context, const Json& arguments)
		{
			if (context.GetActiveScene() == nullptr)
			{
				return Fail(CommandErrorCode::NoScene, "no scene is open");
			}
			World& world = *context.GetActiveScene()->Scene;
			std::vector<UUID> given;
			if (Has(arguments, "entities"))
			{
				for (const Json& item : arguments.at("entities"))
				{
					Result<Entity> entity = ResolveEntity(world, item.get<std::string>());
					if (!entity)
					{
						return Fail(entity.GetError());
					}
					given.push_back(entity->GetUUID());
				}
			}

			const std::string mode = GetString(arguments, "mode");
			std::vector<UUID> selection = context.GetSelection();
			if (mode == "replace")
			{
				selection = std::move(given);
			}
			else if (mode == "add")
			{
				selection.insert(selection.end(), given.begin(), given.end());
			}
			else
			{
				for (const UUID uuid : given)
				{
					std::erase(selection, uuid);
				}
			}
			if (Result<void> set = context.SetSelection(std::move(selection)); !set)
			{
				return Fail(set.GetError());
			}
			return CommandResult::Success(
				DescribeSelection(context), std::format("{} selected", context.GetSelection().size()));
		}

		CommandResult Step(EditorContext& context, const Json& arguments, bool undo)
		{
			UndoStack& stack = context.GetUndoStack();
			const char* verb = undo ? "undo" : "redo";
			const auto steps = static_cast<size_t>(GetInteger(arguments, "steps", 1));
			Json done = Json::array();
			for (size_t step = 0; step < steps; ++step)
			{
				if (undo ? !stack.CanUndo() : !stack.CanRedo())
				{
					if (step == 0)
					{
						return Fail(CommandErrorCode::Conflict, std::format("there is nothing to {}", verb));
					}
					break;
				}
				const std::string description = undo ? stack.GetUndoDescription() : stack.GetRedoDescription();
				if (Result<void> result = undo ? stack.Undo(context) : stack.Redo(context); !result)
				{
					return Fail(result.GetError(), CommandErrorCode::Conflict);
				}
				done.push_back(description);
			}
			PruneSelection(context);

			Json data = Json::object();
			data["steps"] = done.size();
			data[undo ? "undone" : "redone"] = done;
			data["canUndo"] = stack.CanUndo();
			data["canRedo"] = stack.CanRedo();
			data["nextUndo"] = stack.GetUndoDescription();
			data["nextRedo"] = stack.GetRedoDescription();
			data["dirty"] = stack.GetDirtyDocuments();
			std::string message = std::format("{} {}", undo ? "Undid" : "Redid", done.front().get<std::string>());
			if (done.size() > 1)
			{
				message += std::format(" and {} more", done.size() - 1);
			}
			return CommandResult::Success(std::move(data), std::move(message));
		}
	}

	void RegisterEditCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "selection.get",
				.Summary = "Show the selected entities of the active scene",
				.ResultDescription = "{ count, entities: [{ uuid, name }] } in selection order",
				.Flags = CommandFlags::RequiresScene,
			},
			SelectionGet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "selection.set",
				.Summary = "Select entities",
				.Arguments = { ArrayArg(
								   "entities", ArgumentType::String,
								   "Entities (UUIDs or unique names); empty clears the selection"),
		                       EnumArg(
								   "mode", { "replace", "add", "remove" },
								   "Replace the selection, add to it or remove from it")
		                           .WithDefault("replace") },
				.ResultDescription = "{ count, entities: [{ uuid, name }] }",
				.Flags = CommandFlags::RequiresScene,
			},
			SelectionSet);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "undo",
				.Summary = "Undo the last change to a scene or data table",
				.Description = "Each command that changes a scene is one undo step; bulk edits are one step.",
				.Arguments = { IntegerArg("steps", "How many steps to undo").WithRange(1, 1000).WithDefault(1) },
				.ResultDescription = "{ steps, undone: [descriptions], canUndo, canRedo, nextUndo, nextRedo, dirty }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject,
			},
			[](EditorContext& context, const Json& arguments) { return Step(context, arguments, true); });

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "redo",
				.Summary = "Redo the last undone change",
				.Arguments = { IntegerArg("steps", "How many steps to redo").WithRange(1, 1000).WithDefault(1) },
				.ResultDescription = "{ steps, redone: [descriptions], canUndo, canRedo, nextUndo, nextRedo, dirty }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject,
			},
			[](EditorContext& context, const Json& arguments) { return Step(context, arguments, false); });
	}
}
