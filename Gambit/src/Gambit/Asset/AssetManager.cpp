#include "gbpch.h"

#include "Gambit/Asset/AssetManager.h"

#include <set>
#include <span>

namespace Gambit
{
	namespace
	{
		// A project-relative asset path: normalized, relative and not empty.
		Result<std::string> NormalizeAssetPath(std::string_view path)
		{
			Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized)
			{
				return normalized;
			}
			if (normalized->empty() || FileSystem::IsAbsolutePath(*normalized))
			{
				return Error::Format("'{}' is not a project-relative file path", path);
			}
			return normalized;
		}
	}

	AssetManager::AssetManager(Ref<VFS> vfs, std::vector<std::string> assetDirectories, TypeRegistry& types)
		: m_Vfs(std::move(vfs)), m_AssetDirectories(std::move(assetDirectories)), m_Types(types)
	{
		GB_CORE_VERIFY(m_Vfs != nullptr, "an AssetManager needs a VFS");
		RegisterEngineAssets(*this);
	}

	void AssetManager::RegisterImporter(Scope<IAssetImporter> importer)
	{
		GB_CORE_VERIFY(importer != nullptr, "cannot register a null importer");
		const AssetType type = importer->GetType();
		GB_CORE_VERIFY(type != AssetType::None, "an importer needs an asset type");
		m_Importers[type] = std::move(importer);
	}

	void AssetManager::RegisterBuiltinAsset(
		AssetHandle handle, AssetType type, std::string name, std::function<Ref<Asset>()> create)
	{
		GB_CORE_VERIFY(
			handle.IsValid() && type != AssetType::None && !name.empty() && create != nullptr,
			"a built-in asset needs a handle, a type, a name and a function that creates it");
		GB_CORE_VERIFY(!m_Builtins.contains(handle), "the built-in handle {} is registered twice", handle.ToString());
		GB_CORE_VERIFY(
			std::ranges::none_of(m_Builtins, [&](const auto& entry) { return entry.second.Name == name; }),
			"the built-in asset name '{}' is registered twice", name);
		m_Builtins.emplace(handle, BuiltinAsset{ type, std::move(name), std::move(create) });
	}

	std::vector<AssetHandle> AssetManager::ListBuiltinAssets(AssetType type) const
	{
		std::vector<std::pair<std::string_view, AssetHandle>> named;
		for (const auto& [handle, builtin] : m_Builtins)
		{
			if (type == AssetType::None || builtin.Type == type)
			{
				named.emplace_back(builtin.Name, handle);
			}
		}
		std::ranges::sort(named);
		std::vector<AssetHandle> handles;
		handles.reserve(named.size());
		for (const auto& entry : named)
		{
			handles.push_back(entry.second);
		}
		return handles;
	}

	AssetHandle AssetManager::GenerateHandle() const
	{
		for (;;)
		{
			const AssetHandle handle = m_Registry.GenerateHandle();
			if (!m_Builtins.contains(handle))
			{
				return handle;
			}
		}
	}

	ScanReport AssetManager::Scan(const ScanOptions& options)
	{
		ScanReport report = m_Registry.Scan(*m_Vfs, m_AssetDirectories, options);
		// Built-in handles take precedence (D-039): a project asset that claims one is a duplicate like any other and
		// stays unregistered (a file with its sub-assets) until AssignNewHandle gives it a handle of its own.
		for (const auto& [handle, builtin] : m_Builtins)
		{
			const AssetRecord* claimant = m_Registry.Find(handle);
			if (claimant == nullptr)
			{
				continue;
			}
			const std::string path = claimant->Path;
			std::string message = claimant->IsSubAsset()
				? std::format(
					  "the sub-asset '{}' has the handle {} of the built-in asset '{}'; it is not registered",
					  claimant->SubAssetKey, handle.ToString(), builtin.Name)
				: std::format(
					  "handle {} belongs to the built-in asset '{}'; this asset is not registered until it gets a new "
					  "handle",
					  handle.ToString(), builtin.Name);
			GB_CORE_WARN("Asset scan: {}: {}", path, message);
			report.Issues.push_back({ ScanIssueKind::DuplicateHandle, path, std::move(message), false });
			m_Registry.Remove(handle);
		}
		report.AssetCount = m_Registry.GetCount();
		// Cached assets stay only when their file is still where they were loaded from (built-in assets have no file);
		// failures get another chance.
		std::erase_if(m_Cache, [this](const auto& entry) {
			if (entry.second.Loaded != nullptr && m_Builtins.contains(entry.first))
			{
				return false;
			}
			const AssetRecord* record = m_Registry.Find(entry.first);
			return entry.second.Loaded == nullptr || record == nullptr
				|| record->Path != entry.second.Loaded->GetPath();
		});
		return report;
	}

	const AssetRecord* AssetManager::FindByPath(std::string_view path) const
	{
		const Result<std::string> normalized = NormalizeAssetPath(path);
		return normalized ? m_Registry.FindByPath(*normalized) : nullptr;
	}

	AssetHandle AssetManager::GetHandle(std::string_view path) const
	{
		const AssetRecord* record = FindByPath(path);
		return record != nullptr ? record->Handle : AssetHandle();
	}

	std::string AssetManager::GetPath(AssetHandle handle) const
	{
		if (const auto builtin = m_Builtins.find(handle); builtin != m_Builtins.end())
		{
			return std::format("builtin:{}", builtin->second.Name);
		}
		const AssetRecord* record = m_Registry.Find(handle);
		return record != nullptr ? record->Path : std::string();
	}

	AssetType AssetManager::GetType(AssetHandle handle) const
	{
		if (const auto builtin = m_Builtins.find(handle); builtin != m_Builtins.end())
		{
			return builtin->second.Type;
		}
		const AssetRecord* record = m_Registry.Find(handle);
		return record != nullptr ? record->Type : AssetType::None;
	}

	Result<AssetMetadata> AssetManager::ReadMetadataOf(const AssetRecord& record) const
	{
		const std::string metaPath = AssetMetadata::GetMetaPath(record.Path);
		Result<std::string> text = m_Vfs->ReadText(metaPath);
		if (!text)
		{
			return AddSource(metaPath, text.GetError());
		}
		LoadReport report;
		Result<AssetMetadata> metadata = AssetMetadata::Parse(*text, metaPath, report);
		report.Log(metaPath);
		return metadata;
	}

	Result<void> AssetManager::WriteMetadataOf(const AssetRecord& record, const AssetMetadata& metadata)
	{
		Result<std::string> text = metadata.Serialize();
		if (!text)
		{
			return text.GetError();
		}
		return m_Vfs->WriteText(AssetMetadata::GetMetaPath(record.Path), *text);
	}

	Result<AssetMetadata> AssetManager::ReadMetadata(AssetHandle handle) const
	{
		const AssetRecord* record = m_Registry.Find(handle);
		if (record == nullptr)
		{
			return Error::Format("there is no asset with handle {}", handle.ToString());
		}
		return ReadMetadataOf(*record);
	}

	Result<void> AssetManager::SetImportSettings(AssetHandle handle, Json settings)
	{
		const AssetRecord* record = m_Registry.Find(handle);
		if (record == nullptr)
		{
			return Error::Format("there is no asset with handle {}", handle.ToString());
		}
		if (record->IsSubAsset())
		{
			return Error::Format(
				"the sub-asset '{}' of '{}' has no import settings of its own; set those of its file",
				record->SubAssetKey, record->Path);
		}
		if (!settings.is_object())
		{
			return Error("import settings must be a JSON object");
		}
		Result<AssetMetadata> metadata = ReadMetadataOf(*record);
		if (!metadata)
		{
			return metadata.GetError();
		}
		metadata->ImportSettings = std::move(settings);
		metadata->Handle = record->Handle;
		metadata->Type = record->Type;
		return WriteMetadataOf(*record, *metadata);
	}

	Result<AssetHandle> AssetManager::RegisterFile(std::string_view path, Json importSettings)
	{
		Result<std::string> normalized = NormalizeAssetPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		if (const AssetRecord* known = m_Registry.FindByPath(*normalized); known != nullptr)
		{
			return known->Handle;
		}
		const AssetType type = DetectAssetType(*normalized);
		if (type == AssetType::None)
		{
			return Error::Format("'{}' is not a known kind of asset file", *normalized);
		}
		if (!importSettings.is_object())
		{
			return Error("import settings must be a JSON object");
		}
		if (!m_Vfs->Exists(*normalized))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist in the project", *normalized));
		}

		AssetRecord record{ AssetHandle(), *normalized, type };
		const std::string metaPath = AssetMetadata::GetMetaPath(*normalized);
		AssetMetadata metadata;
		metadata.Type = type;
		metadata.ImportSettings = std::move(importSettings);
		if (m_Vfs->Exists(metaPath))
		{
			Result<AssetMetadata> existing = ReadMetadataOf(record);
			if (!existing)
			{
				return existing.GetError();
			}
			if (existing->Handle.IsValid())
			{
				record.Handle = existing->Handle;
				if (const auto builtin = m_Builtins.find(record.Handle); builtin != m_Builtins.end())
				{
					return Error::Format(
						"the handle {} in '{}' belongs to the built-in asset '{}'; use AssignNewHandle",
						record.Handle.ToString(), metaPath, builtin->second.Name);
				}
				if (const AssetRecord* owner = m_Registry.Find(record.Handle); owner != nullptr)
				{
					return Error::Format(
						"the handle {} in '{}' is already used by '{}'; use AssignNewHandle", record.Handle.ToString(),
						metaPath, owner->Path);
				}
				if (Result<void> added = m_Registry.Add(record); !added)
				{
					return added.GetError();
				}
				RegisterListedSubAssets(record, existing->SubAssets);
				return record.Handle;
			}
			// A .meta file without a handle gets one. The handles of its sub-assets are references and stay, and so
			// do its import settings unless the caller gives some.
			if (metadata.ImportSettings.empty())
			{
				metadata.ImportSettings = std::move(existing->ImportSettings);
			}
			metadata.SubAssets = std::move(existing->SubAssets);
		}

		metadata.Handle = GenerateHandle();
		record.Handle = metadata.Handle;
		if (Result<void> written = WriteMetadataOf(record, metadata); !written)
		{
			return AddSource(metaPath, written.GetError());
		}
		if (Result<void> added = m_Registry.Add(record); !added)
		{
			return added.GetError();
		}
		RegisterListedSubAssets(record, metadata.SubAssets);
		return record.Handle;
	}

	void AssetManager::RegisterListedSubAssets(const AssetRecord& file, std::span<const SubAssetEntry> entries)
	{
		for (const SubAssetEntry& entry : entries)
		{
			if (const auto builtin = m_Builtins.find(entry.Handle); builtin != m_Builtins.end())
			{
				GB_CORE_WARN(
					"'{}': the sub-asset '{}' is not registered: its handle {} belongs to the built-in asset '{}'",
					file.Path, entry.Key, entry.Handle.ToString(), builtin->second.Name);
				continue;
			}
			if (Result<void> added = m_Registry.AddSubAsset({ entry.Handle, {}, entry.Type, file.Handle, entry.Key });
			    !added)
			{
				GB_CORE_WARN(
					"'{}': the sub-asset '{}' is not registered: {}", file.Path, entry.Key, added.GetError().Message);
			}
		}
	}

	Result<AssetHandle> AssetManager::AssignNewHandle(std::string_view path)
	{
		Result<std::string> normalized = NormalizeAssetPath(path);
		if (!normalized)
		{
			return normalized.GetError();
		}
		const AssetType type = DetectAssetType(*normalized);
		if (type == AssetType::None)
		{
			return Error::Format("'{}' is not a known kind of asset file", *normalized);
		}
		if (!m_Vfs->Exists(*normalized))
		{
			return MakeFileError(FileError::NotFound, std::format("'{}' does not exist in the project", *normalized));
		}

		AssetRecord record{ AssetHandle(), *normalized, type };
		AssetMetadata metadata;
		metadata.Type = type;
		if (m_Vfs->Exists(AssetMetadata::GetMetaPath(*normalized)))
		{
			Result<AssetMetadata> existing = ReadMetadataOf(record);
			if (!existing)
			{
				return existing.GetError();
			}
			metadata = std::move(*existing);
			metadata.Type = type;
		}
		metadata.Handle = GenerateHandle();
		// The sub-assets of a copy are new assets too; the old handles stay with the original's.
		std::set<AssetHandle> renewed;
		for (SubAssetEntry& entry : metadata.SubAssets)
		{
			do
			{
				entry.Handle = GenerateHandle();
			} while (entry.Handle == metadata.Handle || !renewed.insert(entry.Handle).second);
		}
		record.Handle = metadata.Handle;
		if (Result<void> written = WriteMetadataOf(record, metadata); !written)
		{
			return AddSource(AssetMetadata::GetMetaPath(*normalized), written.GetError());
		}
		if (const AssetRecord* old = m_Registry.FindByPath(*normalized); old != nullptr)
		{
			UnloadAsset(old->Handle);
			m_Registry.Remove(old->Handle);
		}
		if (Result<void> added = m_Registry.Add(record); !added)
		{
			return added.GetError();
		}
		RegisterListedSubAssets(record, metadata.SubAssets);
		return record.Handle;
	}

	Result<Ref<Asset>> AssetManager::LoadAsset(AssetHandle handle)
	{
		if (const auto cached = m_Cache.find(handle); cached != m_Cache.end())
		{
			if (cached->second.Loaded != nullptr)
			{
				return cached->second.Loaded;
			}
			return Error(cached->second.Failure);
		}
		if (const auto builtin = m_Builtins.find(handle); builtin != m_Builtins.end())
		{
			return LoadBuiltin(handle, builtin->second);
		}
		const AssetRecord* record = m_Registry.Find(handle);
		if (record == nullptr)
		{
			return Error::Format("there is no asset with handle {}", handle.ToString());
		}
		return record->IsSubAsset() ? LoadSubAsset(*record) : LoadFile(*record);
	}

	Result<Ref<Asset>> AssetManager::LoadBuiltin(AssetHandle handle, const BuiltinAsset& builtin)
	{
		Ref<Asset> asset = builtin.Create();
		GB_CORE_VERIFY(
			asset != nullptr && asset->GetType() == builtin.Type, "the built-in asset '{}' was not created as a {}",
			builtin.Name, ToString(builtin.Type));
		asset->m_Handle = handle;
		asset->m_Path = std::format("builtin:{}", builtin.Name);
		m_Cache[handle] = CacheEntry{ asset, {} };
		return asset;
	}

	Result<Ref<Asset>> AssetManager::LoadFile(const AssetRecord& record)
	{
		const AssetHandle handle = record.Handle;
		const auto importer = m_Importers.find(record.Type);
		if (importer == m_Importers.end())
		{
			return Error::Format(
				"'{}' cannot be loaded: there is no importer for {} assets", record.Path, ToString(record.Type));
		}
		const auto fail = [&](const Error& error) -> Result<Ref<Asset>> {
			GB_CORE_ERROR("Asset {} failed to load: {}", handle.ToString(), error.Message);
			m_Cache[handle] = CacheEntry{ nullptr, error.Message };
			return error;
		};

		Result<AssetMetadata> metadata = ReadMetadataOf(record);
		if (!metadata)
		{
			return fail(metadata.GetError());
		}
		LoadReport report;
		// A listed handle that another asset has (a .meta file merged from another one, a handle the scan refused)
		// must not reach the importer, or the model would refer to that asset. Its key gets a new handle, and the
		// .meta file is rewritten without the old one.
		std::vector<SubAssetEntry> known;
		bool droppedHandles = false;
		for (const SubAssetEntry& entry : metadata->SubAssets)
		{
			const AssetRecord* owner = m_Registry.Find(entry.Handle);
			const bool builtin = m_Builtins.contains(entry.Handle);
			if (builtin || (owner != nullptr && (owner->Parent != handle || owner->SubAssetKey != entry.Key)))
			{
				GB_CORE_WARN(
					"'{}': the sub-asset '{}' lists the handle {} of {}; it gets a new handle", record.Path, entry.Key,
					entry.Handle.ToString(),
					builtin ? std::string("a built-in asset") : std::format("'{}'", owner->Path));
				droppedHandles = true;
				continue;
			}
			known.push_back(entry);
		}
		SubAssetHandles subAssets(std::move(known), [this] { return GenerateHandle(); });
		const AssetLoadContext context{ record, *metadata, *m_Vfs, m_Types, report, subAssets };
		Result<Ref<Asset>> loaded = importer->second->Load(context);
		if (!loaded)
		{
			return fail(AddSource(record.Path, loaded.GetError()));
		}
		if (*loaded == nullptr)
		{
			return fail(Error::Format("the importer for {} assets returned nothing", ToString(record.Type)));
		}
		report.Log(record.Path);
		Ref<Asset> asset = std::move(*loaded);
		asset->m_Handle = handle;
		asset->m_Path = record.Path;
		m_Cache[handle] = CacheEntry{ asset, {} };

		if (droppedHandles || subAssets.HasChanged())
		{
			// New sub-assets keep their handles across re-imports only through the .meta file. Read-only content (an
			// exported pack) has every handle listed already; failing to write elsewhere costs only that stability.
			metadata->Handle = handle;
			metadata->Type = record.Type;
			metadata->SubAssets = subAssets.GetEntries();
			if (Result<void> written = WriteMetadataOf(record, *metadata); !written)
			{
				GB_CORE_WARN(
					"'{}': cannot record the handles of new sub-assets in the .meta file ({}); they change at the next "
					"import",
					record.Path, written.GetError().Message);
			}
		}
		AddSubAssets(record, *asset, subAssets);
		return asset;
	}

	void AssetManager::AddSubAssets(const AssetRecord& file, const Asset& asset, const SubAssetHandles& handles)
	{
		for (const SubAssetRef& subAsset : asset.GetSubAssets())
		{
			GB_CORE_VERIFY(subAsset.Object != nullptr, "'{}' lists a null sub-asset", file.Path);
			const AssetHandle handle = subAsset.Handle;
			const AssetRecord* record = m_Registry.Find(handle);
			if (record == nullptr)
			{
				const auto entry = std::ranges::find(handles.GetEntries(), handle, &SubAssetEntry::Handle);
				GB_CORE_VERIFY(
					entry != handles.GetEntries().end(), "'{}': the sub-asset handle {} was not acquired", file.Path,
					handle.ToString());
				if (Result<void> added = m_Registry.AddSubAsset(
						{ handle, file.Path, subAsset.Object->GetType(), file.Handle, entry->Key });
				    !added)
				{
					GB_CORE_ERROR(
						"'{}': the sub-asset '{}' cannot be registered: {}", file.Path, entry->Key,
						added.GetError().Message);
					continue;
				}
				record = m_Registry.Find(handle);
			}
			if (record->Parent != file.Handle || record->Type != subAsset.Object->GetType())
			{
				GB_CORE_ERROR(
					"'{}': the sub-asset handle {} belongs to '{}' as a {}", file.Path, handle.ToString(), record->Path,
					ToString(record->Type));
				continue;
			}
			subAsset.Object->m_Handle = handle;
			subAsset.Object->m_Path = file.Path;
			m_Cache[handle] = CacheEntry{ subAsset.Object, {} };
		}
	}

	Result<Ref<Asset>> AssetManager::LoadSubAsset(const AssetRecord& record)
	{
		const AssetHandle handle = record.Handle;
		const std::string key = record.SubAssetKey;
		const std::string path = record.Path;
		Result<Ref<Asset>> parent = LoadAsset(record.Parent);
		if (!parent)
		{
			// The file logged its failure already.
			const Error error = Error::Format("its file '{}' failed to load: {}", path, parent.GetError().Message);
			m_Cache[handle] = CacheEntry{ nullptr, error.Message };
			return error;
		}
		if (const auto cached = m_Cache.find(handle); cached != m_Cache.end() && cached->second.Loaded != nullptr)
		{
			return cached->second.Loaded;
		}
		// The file was cached while this sub-asset was unloaded alone: it still holds it.
		for (const SubAssetRef& subAsset : (*parent)->GetSubAssets())
		{
			if (subAsset.Handle == handle)
			{
				m_Cache[handle] = CacheEntry{ subAsset.Object, {} };
				return subAsset.Object;
			}
		}
		const Error error =
			Error::Format("'{}' contains no sub-asset '{}' (any more); re-import it or fix the reference", path, key);
		GB_CORE_ERROR("Asset {} failed to load: {}", handle.ToString(), error.Message);
		m_Cache[handle] = CacheEntry{ nullptr, error.Message };
		return error;
	}

	void AssetManager::EraseFromCache(const AssetRecord& file)
	{
		m_Cache.erase(file.Handle);
		for (const AssetRecord& subAsset : m_Registry.ListSubAssets(file.Handle))
		{
			m_Cache.erase(subAsset.Handle);
		}
	}

	Result<Ref<Asset>> AssetManager::ReloadAsset(AssetHandle handle)
	{
		if (m_Builtins.contains(handle))
		{
			m_Cache.erase(handle);
			return LoadAsset(handle);
		}
		const AssetRecord* record = m_Registry.Find(handle);
		if (record == nullptr)
		{
			return Error::Format("there is no asset with handle {}", handle.ToString());
		}
		const AssetRecord* file = record->IsSubAsset() ? m_Registry.Find(record->Parent) : record;
		GB_CORE_ASSERT(file != nullptr, "a sub-asset without its file");
		EraseFromCache(*file);
		return LoadAsset(handle);
	}

	AssetState AssetManager::GetState(AssetHandle handle) const
	{
		const auto cached = m_Cache.find(handle);
		if (cached == m_Cache.end())
		{
			return AssetState::Unloaded;
		}
		return cached->second.Loaded != nullptr ? AssetState::Loaded : AssetState::Failed;
	}

	void AssetManager::UnloadAsset(AssetHandle handle)
	{
		const AssetRecord* record = m_Registry.Find(handle);
		if (record != nullptr && !record->IsSubAsset())
		{
			EraseFromCache(*record);
			return;
		}
		m_Cache.erase(handle);
	}

	size_t AssetManager::UnloadUnused()
	{
		// A file holds its sub-assets: they become unused only once the file is gone, hence the repetition.
		size_t total = 0;
		for (;;)
		{
			const size_t removed = std::erase_if(m_Cache, [](const auto& entry) {
				return entry.second.Loaded != nullptr && entry.second.Loaded.use_count() == 1;
			});
			if (removed == 0)
			{
				return total;
			}
			total += removed;
		}
	}

	Result<std::vector<AssetHandle>> AssetManager::GetDependencies(AssetHandle handle)
	{
		Result<Ref<Asset>> asset = LoadAsset(handle);
		if (!asset)
		{
			return asset.GetError();
		}
		return (*asset)->GetDependencies();
	}
}
