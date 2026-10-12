#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetRegistry.h"
#include "Gambit/Asset/IAssetImporter.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/Json.h"

#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	// Where an asset is in its life: Failed assets stay failed (without logging again on every request) until they are
	// reloaded.
	enum class AssetState
	{
		Unloaded,
		Loaded,
		Failed
	};

	// The asset system of a project (D-014): finds the assets below the project's asset directories through the VFS,
	// gives each a stable handle (its .meta file), loads them on demand through the importer of their type and caches
	// them. Everything is addressed by AssetHandle; paths are project-relative with forward slashes.
	//
	// Loading is synchronous. The API is built on handles and states, so that asynchronous loading can be added without
	// changing callers: GetAsset returns what is there now, LoadAsset is the blocking form. Hold handles, not
	// references: ReloadAsset replaces the cached object (references already handed out stay valid but stale), so code
	// that wants current data asks the manager again. Not thread safe.
	class AssetManager
	{
	public:
		// `assetDirectories` are the project-relative directories the scan covers. `vfs` and `types` must outlive the
		// manager (the types are used to read scenes and prefabs). The importers for scenes, prefabs and data tables
		// are registered already.
		AssetManager(
			Ref<VFS> vfs, std::vector<std::string> assetDirectories, TypeRegistry& types = TypeRegistry::Get());
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		[[nodiscard]] VFS& GetVFS() const noexcept { return *m_Vfs; }
		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept { return m_Types; }

		// Replaces the importer of the same type.
		void RegisterImporter(Scope<IAssetImporter> importer);

		// ---- Finding assets ----

		// The project-relative directories Scan covers. Call Scan afterwards.
		void SetAssetDirectories(std::vector<std::string> directories) { m_AssetDirectories = std::move(directories); }
		// Rebuilds the registry from the files (see AssetRegistry::Scan) and drops loaded assets that are gone or
		// moved.
		ScanReport Scan(const ScanOptions& options = {});
		[[nodiscard]] const AssetRegistry& GetRegistry() const noexcept { return m_Registry; }

		// nullptr when there is no such asset.
		[[nodiscard]] const AssetRecord* Find(AssetHandle handle) const { return m_Registry.Find(handle); }
		[[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const { return m_Registry.FindByPath(path); }
		// The null handle for an unknown path.
		[[nodiscard]] AssetHandle GetHandle(std::string_view path) const;
		// Empty for an unknown handle.
		[[nodiscard]] std::string GetPath(AssetHandle handle) const;
		// AssetType::None for an unknown handle.
		[[nodiscard]] AssetType GetType(AssetHandle handle) const;

		// ---- Metadata ----

		// Reads the .meta file of a registered asset.
		[[nodiscard]] Result<AssetMetadata> ReadMetadata(AssetHandle handle) const;
		// Replaces the import settings in the .meta file. Loaded assets are not reloaded.
		[[nodiscard]] Result<void> SetImportSettings(AssetHandle handle, Json settings);
		// Makes an existing file of the project an asset (importing it): creates its .meta file with the settings,
		// unless the file has a valid one, and registers it. Returns the handle of an asset that is registered already.
		[[nodiscard]] Result<AssetHandle> RegisterFile(std::string_view path, Json importSettings = Json::object());
		// Gives the file a new handle in its .meta file and in the registry (the way out of a DuplicateHandle after a
		// file was copied). References to the old handle stay with the original asset.
		[[nodiscard]] Result<AssetHandle> AssignNewHandle(std::string_view path);

		// ---- Loading ----

		// Loads the asset unless it is loaded, and returns it. Failures are logged once, remembered, and returned by
		// later calls until ReloadAsset.
		[[nodiscard]] Result<Ref<Asset>> LoadAsset(AssetHandle handle);
		// The loaded asset as T, nullptr (and an error in the log) when it cannot be loaded or is not a T.
		template<typename T>
		[[nodiscard]] Ref<T> GetAsset(AssetHandle handle)
		{
			static_assert(std::is_base_of_v<Asset, T>, "GetAsset needs an Asset type");
			Result<Ref<Asset>> asset = LoadAsset(handle);
			if (!asset)
			{
				return nullptr;
			}
			if ((*asset)->GetType() != T::StaticType)
			{
				GB_CORE_ERROR(
					"Asset {} ('{}') is a {}, not a {}", handle.ToString(), (*asset)->GetPath(),
					ToString((*asset)->GetType()), ToString(T::StaticType));
				return nullptr;
			}
			return std::static_pointer_cast<T>(*asset);
		}
		// Loads the file again and replaces the cached asset, also after a failure.
		[[nodiscard]] Result<Ref<Asset>> ReloadAsset(AssetHandle handle);

		[[nodiscard]] AssetState GetState(AssetHandle handle) const;
		[[nodiscard]] bool IsLoaded(AssetHandle handle) const { return GetState(handle) == AssetState::Loaded; }
		// Forgets a loaded asset (holders of the Ref keep theirs) or a remembered failure.
		void UnloadAsset(AssetHandle handle);
		// Forgets the loaded assets nobody else holds; returns how many.
		size_t UnloadUnused();

		// The assets `handle` refers to (see Asset::GetDependencies); loads it if necessary.
		[[nodiscard]] Result<std::vector<AssetHandle>> GetDependencies(AssetHandle handle);

	private:
		struct CacheEntry
		{
			Ref<Asset> Loaded; // null for failures
			std::string Failure;
		};

		[[nodiscard]] Result<AssetMetadata> ReadMetadataOf(const AssetRecord& record) const;
		[[nodiscard]] Result<void> WriteMetadataOf(const AssetRecord& record, const AssetMetadata& metadata);

		Ref<VFS> m_Vfs;
		std::vector<std::string> m_AssetDirectories;
		TypeRegistry& m_Types;
		AssetRegistry m_Registry;
		std::map<AssetType, Scope<IAssetImporter>> m_Importers;
		std::unordered_map<AssetHandle, CacheEntry> m_Cache; // lookups only
	};
}
