#include "gbpch.h"

#include "Gambit/Automation/EditorContext.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/Core/LogSinks.h"
#include "Gambit/ECS/Systems/TransformSystem.h"

#include <cctype>

namespace Gambit
{
	namespace
	{
		constexpr std::string_view SceneExtension = ".gscene";

		bool HasExtension(std::string_view path, std::string_view extension)
		{
			if (path.size() < extension.size())
			{
				return false;
			}
			const std::string_view tail = path.substr(path.size() - extension.size());
			return std::ranges::equal(tail, extension, [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
		}

		std::string StemOf(std::string_view path)
		{
			const size_t slash = path.find_last_of('/');
			std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			return std::string(dot == std::string_view::npos || dot == 0 ? name : name.substr(0, dot));
		}

		// The warnings of a load as "<source><pointer>: <message>" lines.
		std::vector<std::string> DescribeWarnings(std::string_view source, const LoadReport& report)
		{
			std::vector<std::string> lines;
			for (const Diagnostic& warning : report.GetWarnings())
			{
				lines.push_back(std::format("{}{}: {}", source, warning.Path, warning.Message));
			}
			if (report.GetSuppressedCount() > 0)
			{
				lines.push_back(std::format("... and {} more warnings", report.GetSuppressedCount()));
			}
			return lines;
		}

		constexpr size_t MaxListedDrops = 3;
	}

	Result<std::string> NormalizeProjectPath(std::string_view path)
	{
		if (path.empty() || FileSystem::IsAbsolutePath(path))
		{
			return MakeCommandError(
				CommandErrorCode::InvalidArguments,
				std::format(
					"'{}' is not a path inside the project: use a project-relative path such as 'Scenes/Main.gscene'",
					path));
		}
		Result<std::string> normalized = FileSystem::NormalizePath(path);
		if (!normalized || normalized->empty())
		{
			return MakeCommandError(
				CommandErrorCode::InvalidArguments,
				std::format(
					"'{}' is not a valid project path{}", path,
					normalized ? std::string() : std::format(": {}", normalized.GetError().Message)));
		}
		return normalized;
	}

	EditorContext::EditorContext(TypeRegistry& types)
		: m_Types(types)
	{}

	EditorContext::~EditorContext() = default;

	void EditorContext::SetDryRun(bool dryRun) noexcept
	{
		m_DryRun = dryRun;
		if (m_Comments != nullptr)
		{
			m_Comments->SetWritesEnabled(!dryRun);
		}
	}

	// ---- Project ----

	Result<void> EditorContext::OpenProject(const std::filesystem::path& path)
	{
		Result<Ref<Project>> project = Project::Load(path, m_Types);
		if (!project)
		{
			return project.GetError();
		}
		ScanOptions options;
		options.WriteMetas = !m_DryRun; // a dry run must not even create .meta files
		static_cast<void>((*project)->GetAssets().Scan(options));
		SetProject(std::move(*project));
		return {};
	}

	void EditorContext::SetProject(Ref<Project> project)
	{
		CloseProject();
		m_Project = std::move(project);
		if (m_Project == nullptr)
		{
			return;
		}
		m_Comments = CreateScope<CommentStore>(m_Project->GetVFS(), m_Project->GetSettings().Directories.Comments);
		m_Comments->SetWritesEnabled(!m_DryRun);
		LoadReport report;
		if (const Result<void> loaded = m_Comments->Load(report); !loaded)
		{
			GB_CORE_WARN("cannot read the comments: {}", loaded.GetError().Message);
		}
		report.Log(m_Project->GetSettings().Directories.Comments);
	}

	void EditorContext::CloseProject()
	{
		m_ActiveScene = nullptr;
		m_Selection.clear();
		m_Scenes.clear();
		m_Tables.clear();
		m_Undo.Reset();
		m_Comments.reset();
		m_Project.reset();
	}

	CommentStore& EditorContext::GetComments()
	{
		GB_CORE_ASSERT(m_Comments != nullptr, "GetComments needs an open project");
		return *m_Comments;
	}

	// ---- Scenes ----

	Result<std::string> EditorContext::ResolveAssetPath(std::string_view path, std::string_view extension) const
	{
		if (m_Project == nullptr)
		{
			return MakeCommandError(CommandErrorCode::NoProject, "no project is open");
		}
		Result<std::string> normalized = NormalizeProjectPath(path);
		if (!normalized)
		{
			return normalized;
		}
		if (!extension.empty() && !HasExtension(*normalized, extension))
		{
			return MakeCommandError(
				CommandErrorCode::InvalidArguments, std::format("'{}' must end in '{}'", *normalized, extension));
		}
		const std::vector<std::string> directories = m_Project->GetAssetDirectories();
		const bool inside = std::ranges::any_of(
			directories, [&](const std::string& directory) { return normalized->starts_with(directory + "/"); });
		if (!inside)
		{
			std::string list;
			for (const std::string& directory : directories)
			{
				list += list.empty() ? directory : ", " + directory;
			}
			return MakeCommandError(
				CommandErrorCode::InvalidArguments,
				std::format("'{}' is outside the asset directories of the project ({})", *normalized, list));
		}
		return normalized;
	}

	Result<SceneDocument*> EditorContext::CreateScene(std::string_view path, std::string_view name, uint64_t seed)
	{
		Result<std::string> normalized = ResolveAssetPath(path, SceneExtension);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (FindScene(*normalized) != nullptr || m_Project->GetVFS().Exists(*normalized))
		{
			return MakeCommandError(
				CommandErrorCode::AlreadyExists,
				std::format("the scene '{}' exists already: open it with scene.open", *normalized));
		}

		auto document = CreateScope<SceneDocument>();
		document->Path = std::move(*normalized);
		document->Metadata.Name = name.empty() ? StemOf(document->Path) : std::string(name);
		document->Scene = CreateScope<World>(m_Types);
		document->Scene->SetSeed(seed);
		m_Undo.MarkDirty(document->Path); // nothing on disk yet
		m_Scenes.push_back(std::move(document));
		return m_Scenes.back().get();
	}

	Result<SceneDocument*> EditorContext::LoadScene(std::string_view path, LoadReport& report)
	{
		Result<std::string> normalized = ResolveAssetPath(path, SceneExtension);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (SceneDocument* open = FindScene(*normalized); open != nullptr)
		{
			return open;
		}

		auto document = CreateScope<SceneDocument>();
		document->Path = std::move(*normalized);
		document->Scene = CreateScope<World>(m_Types);
		Result<SceneMetadata> metadata =
			SceneSerializer::Load(*document->Scene, m_Project->GetVFS(), document->Path, report);
		if (!metadata)
		{
			return metadata.GetError();
		}
		document->Metadata = std::move(*metadata);
		document->DroppedOnLoad = DescribeWarnings(document->Path, report);
		m_Scenes.push_back(std::move(document));
		return m_Scenes.back().get();
	}

	SceneDocument* EditorContext::FindScene(std::string_view path) const
	{
		const Result<std::string> normalized = NormalizeProjectPath(path);
		if (!normalized)
		{
			return nullptr;
		}
		const auto found = std::ranges::find_if(
			m_Scenes, [&](const Scope<SceneDocument>& scene) { return scene->Path == *normalized; });
		return found == m_Scenes.end() ? nullptr : found->get();
	}

	Result<void> EditorContext::SetActiveScene(std::string_view path)
	{
		SceneDocument* scene = FindScene(path);
		if (scene == nullptr)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound, std::format("the scene '{}' is not open: open it with scene.open", path));
		}
		if (scene != m_ActiveScene)
		{
			m_Selection.clear(); // the selection belongs to the active scene
		}
		m_ActiveScene = scene;
		return {};
	}

	std::vector<SceneDocument*> EditorContext::GetScenes() const
	{
		std::vector<SceneDocument*> scenes;
		scenes.reserve(m_Scenes.size());
		for (const Scope<SceneDocument>& scene : m_Scenes)
		{
			scenes.push_back(scene.get());
		}
		return scenes;
	}

	Result<void> EditorContext::CloseScene(std::string_view path, bool discardChanges)
	{
		SceneDocument* scene = FindScene(path);
		if (scene == nullptr)
		{
			return MakeCommandError(CommandErrorCode::NotFound, std::format("the scene '{}' is not open", path));
		}
		if (m_Undo.IsDirty(scene->Path) && !discardChanges)
		{
			return MakeCommandError(
				CommandErrorCode::Conflict,
				std::format("the scene '{}' has unsaved changes: save it first or discard them", scene->Path));
		}
		RemoveScene(scene);
		return {};
	}

	Result<bool> EditorContext::CloseReplacedDocument(std::string_view path)
	{
		const Result<std::string> normalized = NormalizeProjectPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		SceneDocument* scene = FindScene(*normalized);
		const bool tableOpen = IsTableOpen(*normalized);
		if (scene == nullptr && !tableOpen)
		{
			return false;
		}
		if (m_Undo.IsDirty(*normalized))
		{
			return MakeCommandError(
				CommandErrorCode::Conflict,
				std::format(
					"'{}' is open with unsaved changes, which replacing its file would lose: save or close it first",
					*normalized));
		}
		if (scene != nullptr)
		{
			RemoveScene(scene);
		}
		else
		{
			m_Tables.erase(*normalized);
			m_Undo.DiscardDocument(*normalized);
		}
		return true;
	}

	void EditorContext::RemoveScene(SceneDocument* scene)
	{
		const std::string path = scene->Path;
		m_Undo.DiscardDocument(path);
		if (scene == m_ActiveScene)
		{
			m_Selection.clear();
			m_ActiveScene = nullptr;
		}
		std::erase_if(m_Scenes, [&](const Scope<SceneDocument>& open) { return open.get() == scene; });
		if (m_ActiveScene == nullptr && !m_Scenes.empty())
		{
			m_ActiveScene = m_Scenes.back().get();
		}
	}

	// ---- Documents ----

	World* EditorContext::FindWorld(std::string_view document) const
	{
		SceneDocument* scene = FindScene(document);
		return scene == nullptr ? nullptr : scene->Scene.get();
	}

	Result<Ref<DataTable>> EditorContext::GetTable(std::string_view path)
	{
		if (m_Project == nullptr)
		{
			return MakeCommandError(CommandErrorCode::NoProject, "no project is open");
		}
		AssetManager& assets = m_Project->GetAssets();
		const AssetRecord* record = assets.FindByPath(path);
		if (record == nullptr || record->Type != AssetType::DataTable)
		{
			return MakeCommandError(
				CommandErrorCode::NotFound, std::format("there is no data table '{}' (data.tables lists them)", path));
		}
		if (const auto open = m_Tables.find(record->Path); open != m_Tables.end())
		{
			return open->second;
		}
		Result<Ref<Asset>> asset = assets.LoadAsset(record->Handle);
		if (!asset)
		{
			return asset.GetError();
		}
		Ref<DataTable> table = std::static_pointer_cast<DataTable>(*asset);
		m_Tables.emplace(record->Path, table);
		return table;
	}

	bool EditorContext::IsTableOpen(std::string_view path) const
	{
		const Result<std::string> normalized = NormalizeProjectPath(path);
		return normalized && m_Tables.contains(*normalized);
	}

	Result<void> EditorContext::RegisterSaved(std::string_view path)
	{
		AssetManager& assets = m_Project->GetAssets();
		if (assets.FindByPath(path) != nullptr)
		{
			return {};
		}
		Result<AssetHandle> registered = assets.RegisterFile(path);
		if (!registered)
		{
			return registered.GetError();
		}
		return {};
	}

	Result<void> EditorContext::SaveScene(SceneDocument& scene, bool discardUnknown)
	{
		if (m_DryRun)
		{
			return MakeCommandError(CommandErrorCode::Unsupported, "nothing is written in a dry run");
		}
		if (m_Project == nullptr)
		{
			return MakeCommandError(CommandErrorCode::NoProject, "no project is open");
		}
		if (!scene.DroppedOnLoad.empty() && !discardUnknown)
		{
			std::string dropped;
			for (size_t index = 0; index < scene.DroppedOnLoad.size() && index < MaxListedDrops; ++index)
			{
				dropped += (index == 0 ? "" : "; ") + scene.DroppedOnLoad[index];
			}
			if (scene.DroppedOnLoad.size() > MaxListedDrops)
			{
				dropped += std::format("; {} more", scene.DroppedOnLoad.size() - MaxListedDrops);
			}
			return MakeCommandError(
				CommandErrorCode::Conflict,
				std::format(
					"'{}' was opened without data this build cannot read ({}); saving it would delete that data from "
					"the file. Register the missing types, or save with scene.save --discard-unknown to drop it",
					scene.Path, dropped));
		}
		if (Result<void> saved = SceneSerializer::Save(*scene.Scene, scene.Metadata, m_Project->GetVFS(), scene.Path);
		    !saved)
		{
			return saved;
		}
		if (Result<void> registered = RegisterSaved(scene.Path); !registered)
		{
			return registered;
		}
		scene.DroppedOnLoad.clear(); // the file now holds what the world holds
		m_Undo.MarkClean(scene.Path);
		return {};
	}

	Result<void> EditorContext::SaveDocument(std::string_view document, bool discardUnknown)
	{
		if (SceneDocument* scene = FindScene(document); scene != nullptr)
		{
			return SaveScene(*scene, discardUnknown);
		}
		if (m_DryRun)
		{
			return MakeCommandError(CommandErrorCode::Unsupported, "nothing is written in a dry run");
		}
		Result<Ref<DataTable>> table = GetTable(document);
		if (!table)
		{
			return table.GetError();
		}
		if (Result<void> saved = (*table)->Save(m_Project->GetVFS(), document); !saved)
		{
			return saved;
		}
		m_Undo.MarkClean(document);
		return {};
	}

	Result<std::vector<std::string>> EditorContext::SaveAll(bool discardUnknown)
	{
		std::vector<std::string> saved;
		for (const std::string& document : m_Undo.GetDirtyDocuments())
		{
			if (Result<void> result = SaveDocument(document, discardUnknown); !result)
			{
				return AddSource(document, result.GetError());
			}
			saved.push_back(document);
		}
		return saved;
	}

	// ---- Selection ----

	Result<void> EditorContext::SetSelection(std::vector<UUID> entities)
	{
		if (m_ActiveScene == nullptr)
		{
			return MakeCommandError(CommandErrorCode::NoScene, "no scene is open");
		}
		std::vector<UUID> selection;
		selection.reserve(entities.size());
		for (const UUID entity : entities)
		{
			if (!m_ActiveScene->Scene->ContainsEntity(entity))
			{
				return MakeCommandError(
					CommandErrorCode::NotFound,
					std::format("the scene '{}' has no entity {}", m_ActiveScene->Path, entity.ToString()));
			}
			if (std::ranges::find(selection, entity) == selection.end())
			{
				selection.push_back(entity);
			}
		}
		m_Selection = std::move(selection);
		return {};
	}

	void EditorContext::RemoveFromSelection(UUID entity)
	{
		std::erase(m_Selection, entity);
	}

	// ---- Undo ----

	Result<void> EditorContext::Do(Scope<IUndoableOperation> operation, std::string_view mergeKey)
	{
		return m_Undo.Do(*this, std::move(operation), mergeKey);
	}

	// ---- The host ----

	Ref<RingBufferSink> EditorContext::GetLogBuffer() const
	{
		return m_LogBuffer != nullptr ? m_LogBuffer : Log::GetRingBuffer();
	}

	Result<Scope<World>> EditorContext::CreateSimulationWorld(const World& scene, std::optional<uint64_t> seed) const
	{
		Scope<World> world = World::Copy(scene);
		// Before any system exists: a loaded scene, too, is started before its systems are created (D-020).
		world->BeginSimulation(seed.value_or(scene.GetSeed()));
		if (Result<void> added = world->GetScheduler().AddSystem(CreateScope<TransformSystem>()); !added)
		{
			return added.GetError();
		}
		if (m_SimulationSetup)
		{
			if (Result<void> configured = m_SimulationSetup(*world); !configured)
			{
				return configured.GetError();
			}
		}
		return world;
	}

	PlayState EditorContext::GetPlayState() const
	{
		return m_PlayController != nullptr ? m_PlayController->GetState() : PlayState::Edit;
	}
}
