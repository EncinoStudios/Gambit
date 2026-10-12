#pragma once

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Input/InputActions.h"
#include "Gambit/Project/ProjectSettings.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// A Gambit project (D-014, D-025): a directory with a .gproject file, the content folders and a VFS that mounts
	// it, so that all file access is by project-relative paths ("Assets/Meshes/Crate.glb"). Tools keep several
	// projects open at once (each its own instance); the running game and the editor use the active project.
	//
	// The .gproject file is the reflected struct ProjectSettings as JSON, after "Version":
	//   { "Version": 1, "Name": "My Game", "StartScene": "Scenes/Main.gscene",
	//     "Directories": { "Assets": "Assets", "Scripts": "Scripts", "Data": "Data", "Scenes": "Scenes",
	//                      "Comments": "Comments" },
	//     "SimulationHz": 60, "DefaultSeed": 1, "InputActions": "InputActions.json",
	//     "Rendering": { "Environment": "0x0000000000000000", "Tonemapper": "ACES", "Exposure": 1.0 },
	//     "Export": { "OutputName": "My Game", "Compress": true } }
	class Project
	{
		struct ConstructionKey
		{
			explicit ConstructionKey() = default;
		};

	public:
		static constexpr uint32_t FormatVersion = 1;
		static constexpr std::string_view FileExtension = ".gproject";

		Project(
			ConstructionKey, std::filesystem::path directory, std::filesystem::path file, ProjectSettings settings,
			TypeRegistry& types);
		Project(const Project&) = delete;
		Project& operator=(const Project&) = delete;

		// Creates the project `name` in `directory` (created when missing): the folders of ProjectDirectories, the
		// file `<name>.gproject`, a default InputActions.json and an empty scene Scenes/Main.gscene. Existing files are
		// kept; a directory that already has a .gproject file is refused. The assets are not scanned yet.
		[[nodiscard]] static Result<Ref<Project>>
		New(const std::filesystem::path& directory, std::string_view name, TypeRegistry& types = TypeRegistry::Get());
		// Opens the project of a .gproject file, or of the directory that contains exactly one. Mounts the directory
		// in the project's VFS. Unknown settings are warnings in the log. The assets are not scanned yet: call
		// GetAssets().Scan().
		[[nodiscard]] static Result<Ref<Project>>
		Load(const std::filesystem::path& path, TypeRegistry& types = TypeRegistry::Get());

		// The project the editor or the player works on; null when there is none. Tools that handle several projects
		// hold their own instances instead.
		[[nodiscard]] static Ref<Project> GetActive();
		static void SetActive(Ref<Project> project);

		// ---- The file format ----

		[[nodiscard]] static const MigrationTable& GetMigrations();
		// The settings document. Unknown members are warnings in `report`; settings that fail
		// ValidateProjectSettings are errors.
		[[nodiscard]] static Result<ProjectSettings> ParseSettings(
			const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report,
			const MigrationTable& migrations = GetMigrations());
		[[nodiscard]] static Result<std::string>
		SerializeSettings(const TypeRegistry& types, const ProjectSettings& settings);

		// ---- The project ----

		// The project root, absolute.
		[[nodiscard]] const std::filesystem::path& GetDirectory() const noexcept { return m_Directory; }
		[[nodiscard]] const std::filesystem::path& GetFilePath() const noexcept { return m_File; }
		[[nodiscard]] const ProjectSettings& GetSettings() const noexcept { return m_Settings; }
		// Checks the settings (ValidateProjectSettings) and replaces them in memory; Save writes them.
		[[nodiscard]] Result<void> SetSettings(ProjectSettings settings);
		// Writes the .gproject file.
		[[nodiscard]] Result<void> Save() const;

		// The project directory mounted at the root: virtual paths are project-relative.
		[[nodiscard]] VFS& GetVFS() const noexcept { return *m_Vfs; }
		[[nodiscard]] AssetManager& GetAssets() const noexcept { return *m_Assets; }
		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept { return m_Types; }

		// The directories scanned for assets: assets, scenes, data and scripts.
		[[nodiscard]] std::vector<std::string> GetAssetDirectories() const;
		[[nodiscard]] Result<InputActions> LoadInputActions() const;

	private:
		std::filesystem::path m_Directory;
		std::filesystem::path m_File;
		ProjectSettings m_Settings;
		TypeRegistry& m_Types;
		Ref<VFS> m_Vfs;
		Scope<AssetManager> m_Assets;
	};
}
