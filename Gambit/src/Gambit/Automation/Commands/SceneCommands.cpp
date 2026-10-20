#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		Json DescribeScene(const EditorContext& context, const SceneDocument& scene)
		{
			Json json = Json::object();
			json["path"] = scene.Path;
			json["name"] = scene.Metadata.Name;
			json["seed"] = scene.Scene->GetSeed();
			json["entityCount"] = scene.Scene->GetEntityCount();
			json["active"] = context.GetActiveScene() == &scene;
			json["dirty"] = context.GetUndoStack().IsDirty(scene.Path);
			return json;
		}

		CommandResult SceneNew(EditorContext& context, const Json& arguments)
		{
			const uint64_t seed = GetUnsigned(arguments, "seed", context.GetProject()->GetSettings().DefaultSeed);
			Result<SceneDocument*> scene =
				context.CreateScene(GetString(arguments, "path"), GetString(arguments, "name"), seed);
			if (!scene)
			{
				return Fail(scene.GetError(), CommandErrorCode::InvalidArguments);
			}
			if (GetBool(arguments, "activate", true))
			{
				if (Result<void> active = context.SetActiveScene((*scene)->Path); !active)
				{
					return Fail(active.GetError());
				}
			}
			return CommandResult::Success(
				DescribeScene(context, **scene), std::format("Created scene '{}' (not saved yet)", (*scene)->Path));
		}

		CommandResult SceneOpen(EditorContext& context, const Json& arguments)
		{
			const std::string path = GetString(arguments, "path");
			const bool wasOpen = context.FindScene(path) != nullptr;
			LoadReport report;
			Result<SceneDocument*> scene = context.LoadScene(path, report);
			if (!scene)
			{
				return Fail(scene.GetError());
			}
			if (GetBool(arguments, "activate", true))
			{
				if (Result<void> active = context.SetActiveScene((*scene)->Path); !active)
				{
					return Fail(active.GetError());
				}
			}
			Json data = DescribeScene(context, **scene);
			data["alreadyOpen"] = wasOpen;
			CommandResult result = CommandResult::Success(
				std::move(data),
				std::format(
					"{} scene '{}' ({} entities)", wasOpen ? "Scene already open:" : "Opened", (*scene)->Path,
					(*scene)->Scene->GetEntityCount()));
			// Also for a scene that was open already: saving it still drops what its load left out.
			result.Warnings = (*scene)->DroppedOnLoad;
			return result;
		}

		CommandResult SceneSave(EditorContext& context, const Json& arguments)
		{
			const bool discardUnknown = GetBool(arguments, "discardUnknown", false);
			Json saved = Json::array();
			if (GetBool(arguments, "all", false))
			{
				Result<std::vector<std::string>> paths = context.SaveAll(discardUnknown);
				if (!paths)
				{
					return Fail(paths.GetError());
				}
				for (const std::string& path : *paths)
				{
					saved.push_back(path);
				}
			}
			else
			{
				SceneDocument* scene =
					Has(arguments, "path") ? context.FindScene(GetString(arguments, "path")) : context.GetActiveScene();
				if (scene == nullptr)
				{
					return Fail(
						Has(arguments, "path") ? CommandErrorCode::NotFound : CommandErrorCode::NoScene,
						Has(arguments, "path") ? std::format("the scene '{}' is not open", GetString(arguments, "path"))
											   : std::string("no scene is open"));
				}
				if (Result<void> result = context.SaveScene(*scene, discardUnknown); !result)
				{
					return Fail(AddSource(scene->Path, result.GetError()));
				}
				saved.push_back(scene->Path);
			}
			const size_t count = saved.size();
			Json data = Json::object();
			data["saved"] = std::move(saved);
			return CommandResult::Success(
				std::move(data), std::format("Saved {} {}", count, count == 1 ? "scene" : "scenes"));
		}

		CommandResult SceneClose(EditorContext& context, const Json& arguments)
		{
			const SceneDocument* scene =
				Has(arguments, "path") ? context.FindScene(GetString(arguments, "path")) : context.GetActiveScene();
			if (scene == nullptr)
			{
				return Fail(
					Has(arguments, "path") ? CommandErrorCode::NotFound : CommandErrorCode::NoScene,
					Has(arguments, "path") ? std::format("the scene '{}' is not open", GetString(arguments, "path"))
										   : std::string("no scene is open"));
			}
			const std::string path = scene->Path;
			if (Result<void> closed = context.CloseScene(path, GetBool(arguments, "discard", false)); !closed)
			{
				return Fail(closed.GetError());
			}
			const SceneDocument* active = context.GetActiveScene();
			Json data = Json::object();
			data["closed"] = path;
			data["activeScene"] = active != nullptr ? Json(active->Path) : Json(nullptr);
			return CommandResult::Success(std::move(data), std::format("Closed scene '{}'", path));
		}

		CommandResult SceneList(EditorContext& context, const Json&)
		{
			Json scenes = Json::array();
			std::vector<std::string> shown;
			for (const SceneDocument* scene : context.GetScenes())
			{
				Json entry = DescribeScene(context, *scene);
				entry["open"] = true;
				scenes.push_back(std::move(entry));
				shown.push_back(scene->Path);
			}
			for (const AssetRecord& record : context.GetProject()->GetAssets().GetRegistry().List(AssetType::Scene))
			{
				if (std::ranges::find(shown, record.Path) != shown.end())
				{
					continue;
				}
				Json entry = Json::object();
				entry["path"] = record.Path;
				entry["handle"] = record.Handle.ToString();
				entry["open"] = false;
				entry["active"] = false;
				entry["dirty"] = false;
				scenes.push_back(std::move(entry));
			}
			Json data = Json::object();
			data["count"] = scenes.size();
			data["scenes"] = std::move(scenes);
			return CommandResult::Success(std::move(data));
		}

		CommandResult SceneSetActive(EditorContext& context, const Json& arguments)
		{
			if (Result<void> active = context.SetActiveScene(GetString(arguments, "path")); !active)
			{
				return Fail(active.GetError());
			}
			return CommandResult::Success(
				DescribeScene(context, *context.GetActiveScene()),
				std::format("Active scene: '{}'", context.GetActiveScene()->Path));
		}
	}

	void RegisterSceneCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.new",
				.Summary = "Create an empty scene in memory",
				.Description =
					"The scene is written when it is saved (scene.save; the CLI saves it at the end of a run).",
				.Arguments = { StringArg(
								   "path",
								   "Project-relative .gscene path below an asset directory, e.g. Scenes/Level1.gscene")
		                           .Require(),
		                       StringArg("name", "Display name stored in the file (default: the file name)"),
		                       UnsignedArg(
								   "seed",
								   "Seed of the scene's random generator (default: the project's default seed)"),
		                       BooleanArg("activate", "Make it the active scene").WithDefault(true) },
				.ResultDescription = "{ path, name, seed, entityCount, active, dirty }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject,
			},
			SceneNew);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.open",
				.Summary = "Open a scene file for editing",
				.Arguments = { StringArg("path", "Project-relative .gscene path").Require(),
		                       BooleanArg("activate", "Make it the active scene").WithDefault(true) },
				.ResultDescription = "{ path, name, seed, entityCount, active, dirty, alreadyOpen }; warnings list "
									 "what the file contained that this build cannot read (scene.save then refuses "
									 "unless discardUnknown)",
				.Flags = CommandFlags::RequiresProject,
			},
			SceneOpen);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.save",
				.Summary = "Write a scene (or all scenes with changes) to its file",
				.Description =
					"A scene that was opened with data this build cannot read (unknown components or fields, listed "
					"as warnings by scene.open) is refused, because the file would lose that data, unless "
					"discardUnknown is true.",
				.Arguments = { StringArg("path", "The open scene to save (default: the active scene)"),
		                       BooleanArg("all", "Save every document with unsaved changes instead").WithDefault(false),
		                       BooleanArg(
								   "discardUnknown",
								   "Save even if the file then loses the data its load could not read")
		                           .WithDefault(false) },
				.ResultDescription = "{ saved: [paths] }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject | CommandFlags::WritesFiles,
			},
			SceneSave);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.close",
				.Summary = "Close an open scene",
				.Arguments = { StringArg("path", "The open scene to close (default: the active scene)"),
		                       BooleanArg("discard", "Close even if it has unsaved changes").WithDefault(false) },
				.ResultDescription = "{ closed, activeScene }",
				.Flags = CommandFlags::Mutating | CommandFlags::RequiresProject,
			},
			SceneClose);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.list",
				.Summary = "List the open scenes and the scene files of the project",
				.ResultDescription = "{ count, scenes: [{ path, name, open, active, dirty, entityCount }] }",
				.Flags = CommandFlags::RequiresProject,
			},
			SceneList);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "scene.set-active",
				.Summary = "Make an open scene the one that entity and component commands work on",
				.Arguments = { StringArg("path", "The open scene").Require() },
				.ResultDescription = "{ path, name, seed, entityCount, active, dirty }",
				.Flags = CommandFlags::RequiresProject,
			},
			SceneSetActive);
	}
}
