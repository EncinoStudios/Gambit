#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/Commands/CommandSupport.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Serialization/FieldSerializer.h"

namespace Gambit::Commands
{
	namespace
	{
		using namespace CommandSupport;

		CommandResult ProjectNew(EditorContext& context, const Json& arguments)
		{
			if (const std::vector<std::string> dirty = context.GetUndoStack().GetDirtyDocuments(); !dirty.empty())
			{
				return Fail(
					CommandErrorCode::Conflict,
					std::format(
						"the open project has unsaved changes ({}): save them first with scene.save",
						JoinNames(dirty)));
			}
			Result<Ref<Project>> created = Project::New(
				FileSystem::FromUtf8(GetString(arguments, "directory")), GetString(arguments, "name"),
				context.GetTypeRegistry());
			if (!created)
			{
				return Fail(created.GetError());
			}
			const Ref<Project> project = *created;
			static_cast<void>(project->GetAssets().Scan());
			context.SetProject(project);

			// The project starts with an empty scene: open it, so that entity commands work right away.
			std::string startScene = project->GetSettings().StartScene;
			LoadReport report;
			Result<SceneDocument*> scene = context.LoadScene(startScene, report);
			if (!scene)
			{
				return Fail(scene.GetError());
			}
			if (Result<void> active = context.SetActiveScene(startScene); !active)
			{
				return Fail(active.GetError());
			}

			Json data = Json::object();
			data["name"] = project->GetSettings().Name;
			data["directory"] = FileSystem::ToUtf8(project->GetDirectory());
			data["file"] = FileSystem::ToUtf8(project->GetFilePath());
			data["startScene"] = std::move(startScene);
			return CommandResult::Success(
				std::move(data),
				std::format(
					"Created project '{}' in {}", project->GetSettings().Name,
					FileSystem::ToUtf8(project->GetDirectory())));
		}

		CommandResult ProjectInfo(EditorContext& context, const Json&)
		{
			const Project& project = *context.GetProject();
			const ProjectSettings& settings = project.GetSettings();
			const Ref<const StructInfo> info = context.GetTypeRegistry().FindStruct("ProjectSettings");
			Json encoded = Json::object();
			if (info != nullptr)
			{
				Result<Json> members = StructToJson(*info, &settings);
				if (!members)
				{
					return Fail(members.GetError());
				}
				encoded = std::move(*members);
			}

			Json data = Json::object();
			data["name"] = settings.Name;
			data["directory"] = FileSystem::ToUtf8(project.GetDirectory());
			data["file"] = FileSystem::ToUtf8(project.GetFilePath());
			data["settings"] = std::move(encoded);
			data["startScene"] = settings.StartScene;
			data["assetCount"] = project.GetAssets().GetRegistry().GetCount();
			Json scenes = Json::array();
			for (const SceneDocument* scene : context.GetScenes())
			{
				scenes.push_back(scene->Path);
			}
			data["openScenes"] = std::move(scenes);
			const SceneDocument* active = context.GetActiveScene();
			data["activeScene"] = active != nullptr ? Json(active->Path) : Json(nullptr);
			data["dirty"] = context.GetUndoStack().GetDirtyDocuments();
			data["commentCount"] = context.GetComments().GetCount();
			return CommandResult::Success(
				std::move(data),
				std::format("Project '{}' in {}", settings.Name, FileSystem::ToUtf8(project.GetDirectory())));
		}
	}

	void RegisterProjectCommands(CommandRegistry& registry)
	{
		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "project.new",
				.Summary = "Create a project and make it the open one",
				.Description =
					"Creates the folders, the .gproject file, a default InputActions.json and an empty start "
					"scene, which is opened and made active.",
				.Arguments = { StringArg("directory", "The project directory (created when missing)").Require(),
		                       StringArg("name", "The project name; it also names the .gproject file").Require() },
				.ResultDescription = "{ name, directory, file, startScene }",
				.Flags = CommandFlags::Mutating | CommandFlags::WritesFiles,
			},
			ProjectNew);

		RegisterOrDie(
			registry,
			CommandInfo{
				.Name = "project.info",
				.Summary = "Show the open project: its settings, open scenes and unsaved changes",
				.ResultDescription =
					"{ name, directory, file, settings, startScene, assetCount, openScenes, activeScene, "
					"dirty, commentCount }",
				.Flags = CommandFlags::RequiresProject,
			},
			ProjectInfo);
	}
}
