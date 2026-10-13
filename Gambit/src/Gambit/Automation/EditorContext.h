#pragma once

#include "Gambit/Automation/CommandResult.h"
#include "Gambit/Automation/UndoStack.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/UUID.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Project/Comments.h"
#include "Gambit/Project/Project.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	class DataTable;
	class RingBufferSink;

	// What runs the commands: a headless tool (the CLI, the MCP server) or the editor application.
	enum class HostKind : uint8_t
	{
		Headless,
		Editor
	};

	enum class PlayState : uint8_t
	{
		Edit,    // the scenes are being edited
		Playing, // a simulation of a copy of the active scene runs
		Paused
	};

	// Implemented by the editor (T20): play mode simulates a copy of the active scene made by
	// EditorContext::CreateSimulationWorld. Headless hosts have no controller and are always in the Edit state.
	class IPlayController
	{
	public:
		virtual ~IPlayController() = default;

		[[nodiscard]] virtual PlayState GetState() const = 0;
		[[nodiscard]] virtual Result<void> Play(EditorContext& context) = 0;
		[[nodiscard]] virtual Result<void> Pause(EditorContext& context) = 0;
		[[nodiscard]] virtual Result<void> Stop(EditorContext& context) = 0;
		// Runs `ticks` fixed ticks while paused.
		[[nodiscard]] virtual Result<void> Step(EditorContext& context, uint32_t ticks) = 0;
	};

	// A scene that is open for editing: its project-relative path (which names it everywhere), the metadata the file
	// stores besides the entities, and the world with the entities.
	struct SceneDocument
	{
		std::string Path;
		SceneMetadata Metadata;
		Scope<World> Scene;
	};

	// Normalizes a path given to a command: forward slashes, no "." or ".." segments. Fails (with the reason) for
	// absolute paths, paths that leave the project and names that are not portable (D-014).
	[[nodiscard]] Result<std::string> NormalizeProjectPath(std::string_view path);

	// The state every command works on (D-051): the active project, the open scenes with their undo history, the
	// selection, the comments and the connection to the host (log, play mode). The editor, the CLI, the MCP server and
	// the RPC endpoint each hold one and run CommandRegistry commands against it, so they all do the same things the
	// same way. Not thread safe: hosts serialize their requests.
	class EditorContext
	{
	public:
		using WorldSetup = std::function<Result<void>(World& world)>;

		// `types` must outlive the context and everything that it opens.
		explicit EditorContext(TypeRegistry& types = TypeRegistry::Get());
		~EditorContext();
		EditorContext(const EditorContext&) = delete;
		EditorContext& operator=(const EditorContext&) = delete;

		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept { return m_Types; }

		[[nodiscard]] HostKind GetHost() const noexcept { return m_Host; }
		void SetHost(HostKind host) noexcept { m_Host = host; }
		// A dry run changes nothing on disk: commands that write files are refused and dirty documents are not saved.
		[[nodiscard]] bool IsDryRun() const noexcept { return m_DryRun; }
		void SetDryRun(bool dryRun) noexcept;
		// Who comments are written as when a command does not say.
		[[nodiscard]] const std::string& GetAuthorName() const noexcept { return m_AuthorName; }
		void SetAuthorName(std::string name) { m_AuthorName = std::move(name); }

		// ---- Project ----

		// Opens the project of a .gproject file or of the directory with exactly one, and scans its assets (without
		// writing .meta files in a dry run). Replaces the current project (see SetProject).
		[[nodiscard]] Result<void> OpenProject(const std::filesystem::path& path);
		// Makes `project` the current one. The open scenes of the old project are closed without saving, the
		// selection and the undo history are dropped, and the comments of the new project are loaded.
		void SetProject(Ref<Project> project);
		void CloseProject();
		[[nodiscard]] const Ref<Project>& GetProject() const noexcept { return m_Project; }
		// The comments of the project; call with a project open.
		[[nodiscard]] CommentStore& GetComments();

		// ---- Scenes ----

		// A new, empty scene that exists only in memory (and is dirty) until it is saved. `path` is a project-relative
		// .gscene path below one of the asset directories; fails when a file or an open scene has it.
		[[nodiscard]] Result<SceneDocument*> CreateScene(std::string_view path, std::string_view name, uint64_t seed);
		// Loads the scene file and keeps it open; returns the open scene when it is open already. Warnings of the load
		// go to `report`.
		[[nodiscard]] Result<SceneDocument*> LoadScene(std::string_view path, LoadReport& report);
		[[nodiscard]] SceneDocument* FindScene(std::string_view path) const;
		[[nodiscard]] SceneDocument* GetActiveScene() const noexcept { return m_ActiveScene; }
		[[nodiscard]] Result<void> SetActiveScene(std::string_view path);
		// The open scenes in the order they were opened.
		[[nodiscard]] std::vector<SceneDocument*> GetScenes() const;
		// Fails with a Conflict error when the scene has unsaved changes and `discardChanges` is false. Drops the
		// scene's undo history and takes its entities out of the selection.
		[[nodiscard]] Result<void> CloseScene(std::string_view path, bool discardChanges);

		// A path given to a command for a file the project keeps as an asset: normalized (NormalizeProjectPath), below
		// one of the project's asset directories and, when `extension` is not empty (".gscene"), ending in it (ignoring
		// case). Needs a project.
		[[nodiscard]] Result<std::string>
		ResolveAssetPath(std::string_view path, std::string_view extension = {}) const;

		// ---- Documents: scenes and data tables ----

		// The world of the open scene with this path; nullptr when there is none.
		[[nodiscard]] World* FindWorld(std::string_view document) const;
		// The data table asset with this path, loaded (and cached by the asset manager).
		[[nodiscard]] Result<Ref<DataTable>> GetTable(std::string_view path);
		// Writes the document and marks it clean. A new asset file is registered (its .meta file is created).
		[[nodiscard]] Result<void> SaveDocument(std::string_view document);
		[[nodiscard]] Result<void> SaveScene(SceneDocument& scene);
		// Saves every dirty document; returns their paths. Stops at the first failure.
		[[nodiscard]] Result<std::vector<std::string>> SaveAll();

		// ---- Selection ----

		// The selected entities of the active scene in selection order.
		[[nodiscard]] const std::vector<UUID>& GetSelection() const noexcept { return m_Selection; }
		// Fails when there is no active scene or an entity is not in it. Duplicates are dropped.
		[[nodiscard]] Result<void> SetSelection(std::vector<UUID> entities);
		void RemoveFromSelection(UUID entity);

		// ---- Undo ----

		[[nodiscard]] UndoStack& GetUndoStack() noexcept { return m_Undo; }
		[[nodiscard]] const UndoStack& GetUndoStack() const noexcept { return m_Undo; }
		// Performs an operation and records it (UndoStack::Do).
		[[nodiscard]] Result<void> Do(Scope<IUndoableOperation> operation, std::string_view mergeKey = {});

		// ---- The host ----

		// The in-memory log of the process (Log::GetRingBuffer unless one was set); null when logging keeps none.
		[[nodiscard]] Ref<RingBufferSink> GetLogBuffer() const;
		void SetLogBuffer(Ref<RingBufferSink> buffer) { m_LogBuffer = std::move(buffer); }

		// Adds the systems a simulation needs beyond the engine's own (scripts, physics) to every simulation world.
		void SetSimulationSetup(WorldSetup setup) { m_SimulationSetup = std::move(setup); }
		// The one way to start a simulation of an edited scene (editor play, sim.run): a copy of `scene` that
		// BeginSimulation has started from the scene's seed, with the TransformSystem and the configured systems added,
		// so that it runs exactly like the same scene loaded from its file (D-020 "Simulation start").
		[[nodiscard]] Result<Scope<World>> CreateSimulationWorld(const World& scene) const;

		void SetPlayController(Ref<IPlayController> controller) { m_PlayController = std::move(controller); }
		[[nodiscard]] IPlayController* GetPlayController() const noexcept { return m_PlayController.get(); }
		[[nodiscard]] PlayState GetPlayState() const;

	private:
		[[nodiscard]] Result<void> RegisterSaved(std::string_view path);
		void RemoveScene(SceneDocument* scene);

		TypeRegistry& m_Types;
		HostKind m_Host = HostKind::Headless;
		bool m_DryRun = false;
		std::string m_AuthorName = "user";
		Ref<Project> m_Project;
		Scope<CommentStore> m_Comments;
		std::vector<Scope<SceneDocument>> m_Scenes;
		SceneDocument* m_ActiveScene = nullptr;
		std::vector<UUID> m_Selection;
		UndoStack m_Undo;
		Ref<RingBufferSink> m_LogBuffer;
		WorldSetup m_SimulationSetup;
		Ref<IPlayController> m_PlayController;
	};
}
