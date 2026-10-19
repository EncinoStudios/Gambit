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

#include <functional>
#include <map>
#include <span>
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
	// them. Everything is addressed by AssetHandle; paths are project-relative with forward slashes. Assets inside
	// files (the meshes of a model, D-038) are sub-assets with handles of their own, kept in the file's .meta, and load
	// by handle like files (loading one loads its file). Built-in assets (the engine's primitive meshes, default
	// textures, D-039) have fixed handles and no file.
	//
	// Loading is synchronous. The API is built on handles and states, so that asynchronous loading can be added without
	// changing callers: GetAsset returns what is there now, LoadAsset is the blocking form. Hold handles, not
	// references: ReloadAsset replaces the cached object (references already handed out stay valid but stale), so code
	// that wants current data asks the manager again. Not thread safe.
	class AssetManager
	{
	public:
		// `assetDirectories` are the project-relative directories the scan covers. `vfs` and `types` must outlive the
		// manager (the types are used to read scenes, prefabs and materials). The engine's importers and built-in
		// assets are registered already (RegisterEngineAssets).
		AssetManager(
			Ref<VFS> vfs, std::vector<std::string> assetDirectories, TypeRegistry& types = TypeRegistry::Get());
		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		[[nodiscard]] VFS& GetVFS() const noexcept { return *m_Vfs; }
		[[nodiscard]] TypeRegistry& GetTypeRegistry() const noexcept { return m_Types; }

		// Replaces the importer of the same type.
		void RegisterImporter(Scope<IAssetImporter> importer);

		// ---- Built-in assets ----

		// Registers an asset that the engine provides without a file: `create` makes it on first load. The handle is
		// fixed (built-in handles are small numbers that random handles practically never are) and takes precedence
		// over a project asset that claims it; the name is unique among built-ins ("Meshes/Cube"), and the asset's
		// path is "builtin:<name>". Registering a handle or name twice is a programmer error (verified).
		void
		RegisterBuiltinAsset(AssetHandle handle, AssetType type, std::string name, std::function<Ref<Asset>()> create);
		[[nodiscard]] bool IsBuiltinAsset(AssetHandle handle) const { return m_Builtins.contains(handle); }
		// The handles of the built-in assets (of `type` unless it is None), sorted by name.
		[[nodiscard]] std::vector<AssetHandle> ListBuiltinAssets(AssetType type = AssetType::None) const;

		// ---- Finding assets ----

		// The project-relative directories Scan covers. Call Scan afterwards.
		void SetAssetDirectories(std::vector<std::string> directories) { m_AssetDirectories = std::move(directories); }
		// Rebuilds the registry from the files (see AssetRegistry::Scan) and drops loaded assets that are gone or
		// moved. A project asset whose .meta file claims a built-in handle is reported as a DuplicateHandle and not
		// registered (built-ins take precedence, D-039).
		ScanReport Scan(const ScanOptions& options = {});
		[[nodiscard]] const AssetRegistry& GetRegistry() const noexcept { return m_Registry; }

		// nullptr when there is no such asset. Paths from tools are normalized first, so "Assets\\Crate.png" and
		// "./Assets/Crate.png" find "Assets/Crate.png".
		[[nodiscard]] const AssetRecord* Find(AssetHandle handle) const { return m_Registry.Find(handle); }
		[[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
		// The null handle for an unknown path (normalized like FindByPath).
		[[nodiscard]] AssetHandle GetHandle(std::string_view path) const;
		// The file's path (also for its sub-assets), "builtin:<name>" for built-in assets, empty for an unknown handle.
		[[nodiscard]] std::string GetPath(AssetHandle handle) const;
		// AssetType::None for an unknown handle.
		[[nodiscard]] AssetType GetType(AssetHandle handle) const;

		// ---- Metadata ----

		// Reads the .meta file of a registered asset (of its file, for a sub-asset).
		[[nodiscard]] Result<AssetMetadata> ReadMetadata(AssetHandle handle) const;
		// Replaces the import settings in the .meta file. Loaded assets are not reloaded.
		[[nodiscard]] Result<void> SetImportSettings(AssetHandle handle, Json settings);
		// Makes an existing file of the project an asset (importing it): creates its .meta file with the settings,
		// unless the file has a valid one, and registers it with the sub-assets its .meta file lists (those whose
		// handle another asset has are left out, with a warning). A .meta file without a handle gets one and keeps its
		// sub-assets, and its import settings unless `importSettings` holds some. Fails when the .meta file's handle
		// belongs to another asset, built-ins included (AssignNewHandle is the way out). Returns the handle of an
		// asset that is registered already.
		[[nodiscard]] Result<AssetHandle> RegisterFile(std::string_view path, Json importSettings = Json::object());
		// Gives the file a new handle in its .meta file and in the registry (the way out of a DuplicateHandle after a
		// file was copied), and its sub-assets new handles too. References to the old handles stay with the original.
		[[nodiscard]] Result<AssetHandle> AssignNewHandle(std::string_view path);

		// ---- Loading ----

		// Loads the asset unless it is loaded, and returns it. Failures are logged once, remembered, and returned by
		// later calls until ReloadAsset. Loading a file registers and caches its sub-assets; when the import produced
		// sub-assets the .meta file did not list yet, their handles are added to it (and listed handles that belong to
		// other assets are replaced). Loading a sub-asset loads its file.
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
		// Loads the file again and replaces the cached asset and its sub-assets, also after a failure. For a sub-asset,
		// reloads its file and returns the new sub-asset.
		[[nodiscard]] Result<Ref<Asset>> ReloadAsset(AssetHandle handle);

		[[nodiscard]] AssetState GetState(AssetHandle handle) const;
		[[nodiscard]] bool IsLoaded(AssetHandle handle) const { return GetState(handle) == AssetState::Loaded; }
		// Forgets a loaded asset (holders of the Ref keep theirs) or a remembered failure; a file's sub-assets too.
		void UnloadAsset(AssetHandle handle);
		// Forgets the loaded assets nobody else holds (a file's sub-assets once the file is gone); returns how many.
		size_t UnloadUnused();

		// The assets `handle` refers to (see Asset::GetDependencies); loads it if necessary.
		[[nodiscard]] Result<std::vector<AssetHandle>> GetDependencies(AssetHandle handle);

	private:
		struct CacheEntry
		{
			Ref<Asset> Loaded; // null for failures
			std::string Failure;
		};

		struct BuiltinAsset
		{
			AssetType Type = AssetType::None;
			std::string Name;
			std::function<Ref<Asset>()> Create;
		};

		[[nodiscard]] Result<AssetMetadata> ReadMetadataOf(const AssetRecord& record) const;
		[[nodiscard]] Result<void> WriteMetadataOf(const AssetRecord& record, const AssetMetadata& metadata);
		// A random handle that neither a registered asset nor a built-in one has.
		[[nodiscard]] AssetHandle GenerateHandle() const;
		[[nodiscard]] Result<Ref<Asset>> LoadBuiltin(AssetHandle handle, const BuiltinAsset& builtin);
		[[nodiscard]] Result<Ref<Asset>> LoadFile(const AssetRecord& record);
		[[nodiscard]] Result<Ref<Asset>> LoadSubAsset(const AssetRecord& record);
		// Registers the sub-assets a file's .meta file lists, warning about (and skipping) handles that are taken.
		void RegisterListedSubAssets(const AssetRecord& file, std::span<const SubAssetEntry> entries);
		// Registers (when new) and caches the sub-assets of a loaded file.
		void AddSubAssets(const AssetRecord& file, const Asset& asset, const SubAssetHandles& handles);
		// Forgets the cached file and its sub-assets.
		void EraseFromCache(const AssetRecord& file);

		Ref<VFS> m_Vfs;
		std::vector<std::string> m_AssetDirectories;
		TypeRegistry& m_Types;
		AssetRegistry m_Registry;
		std::map<AssetType, Scope<IAssetImporter>> m_Importers;
		std::map<AssetHandle, BuiltinAsset> m_Builtins;
		std::unordered_map<AssetHandle, CacheEntry> m_Cache; // lookups only
	};

	// Registers the engine's importers and built-in assets in `manager` (every module that owns asset types adds its
	// own here, like RegisterEngineTypes does for reflection). The AssetManager constructor calls it.
	void RegisterEngineAssets(AssetManager& manager);
}
