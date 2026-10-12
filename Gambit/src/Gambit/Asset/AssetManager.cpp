#include "gbpch.h"

#include "Gambit/Asset/AssetManager.h"

#include "Gambit/Asset/BuiltinImporters.h"

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
		RegisterImporter(CreateScope<SceneImporter>());
		RegisterImporter(CreateScope<PrefabImporter>());
		RegisterImporter(CreateScope<DataTableImporter>());
	}

	void AssetManager::RegisterImporter(Scope<IAssetImporter> importer)
	{
		GB_CORE_VERIFY(importer != nullptr, "cannot register a null importer");
		const AssetType type = importer->GetType();
		GB_CORE_VERIFY(type != AssetType::None, "an importer needs an asset type");
		m_Importers[type] = std::move(importer);
	}

	ScanReport AssetManager::Scan(const ScanOptions& options)
	{
		ScanReport report = m_Registry.Scan(*m_Vfs, m_AssetDirectories, options);
		// Cached assets stay only when their file is still where they were loaded from; failures get another chance.
		std::erase_if(m_Cache, [this](const auto& entry) {
			const AssetRecord* record = m_Registry.Find(entry.first);
			return entry.second.Loaded == nullptr || record == nullptr
				|| record->Path != entry.second.Loaded->GetPath();
		});
		return report;
	}

	AssetHandle AssetManager::GetHandle(std::string_view path) const
	{
		const AssetRecord* record = m_Registry.FindByPath(path);
		return record != nullptr ? record->Handle : AssetHandle();
	}

	std::string AssetManager::GetPath(AssetHandle handle) const
	{
		const AssetRecord* record = m_Registry.Find(handle);
		return record != nullptr ? record->Path : std::string();
	}

	AssetType AssetManager::GetType(AssetHandle handle) const
	{
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
				return record.Handle;
			}
		}

		AssetMetadata metadata;
		metadata.Handle = m_Registry.GenerateHandle();
		metadata.Type = type;
		metadata.ImportSettings = std::move(importSettings);
		record.Handle = metadata.Handle;
		if (Result<void> written = WriteMetadataOf(record, metadata); !written)
		{
			return AddSource(metaPath, written.GetError());
		}
		if (Result<void> added = m_Registry.Add(record); !added)
		{
			return added.GetError();
		}
		return record.Handle;
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
		metadata.Handle = m_Registry.GenerateHandle();
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
		return record.Handle;
	}

	Result<Ref<Asset>> AssetManager::LoadAsset(AssetHandle handle)
	{
		const AssetRecord* record = m_Registry.Find(handle);
		if (record == nullptr)
		{
			return Error::Format("there is no asset with handle {}", handle.ToString());
		}
		if (const auto cached = m_Cache.find(handle); cached != m_Cache.end())
		{
			if (cached->second.Loaded != nullptr)
			{
				return cached->second.Loaded;
			}
			return Error(cached->second.Failure);
		}

		const auto importer = m_Importers.find(record->Type);
		if (importer == m_Importers.end())
		{
			return Error::Format(
				"'{}' cannot be loaded: there is no importer for {} assets", record->Path, ToString(record->Type));
		}
		const auto fail = [&](const Error& error) -> Result<Ref<Asset>> {
			GB_CORE_ERROR("Asset {} failed to load: {}", handle.ToString(), error.Message);
			m_Cache[handle] = CacheEntry{ nullptr, error.Message };
			return error;
		};

		Result<AssetMetadata> metadata = ReadMetadataOf(*record);
		if (!metadata)
		{
			return fail(metadata.GetError());
		}
		LoadReport report;
		const AssetLoadContext context{ *record, *metadata, *m_Vfs, m_Types, report };
		Result<Ref<Asset>> loaded = importer->second->Load(context);
		if (!loaded)
		{
			return fail(AddSource(record->Path, loaded.GetError()));
		}
		if (*loaded == nullptr)
		{
			return fail(Error::Format("the importer for {} assets returned nothing", ToString(record->Type)));
		}
		report.Log(record->Path);
		Ref<Asset> asset = std::move(*loaded);
		asset->m_Handle = handle;
		asset->m_Path = record->Path;
		m_Cache[handle] = CacheEntry{ asset, {} };
		return asset;
	}

	Result<Ref<Asset>> AssetManager::ReloadAsset(AssetHandle handle)
	{
		m_Cache.erase(handle);
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
		m_Cache.erase(handle);
	}

	size_t AssetManager::UnloadUnused()
	{
		return std::erase_if(m_Cache, [](const auto& entry) {
			return entry.second.Loaded != nullptr && entry.second.Loaded.use_count() == 1;
		});
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
