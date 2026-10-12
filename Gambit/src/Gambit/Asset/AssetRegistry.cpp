#include "gbpch.h"

#include "Gambit/Asset/AssetRegistry.h"

#include <set>

namespace Gambit
{
	namespace
	{
		// An asset file found by the scan, and what is known about its metadata.
		struct Candidate
		{
			std::string Path;
			AssetType Type = AssetType::None;
			AssetMetadata Metadata;
			bool NeedsHandle = false; // no .meta file, or one without a handle
			bool MetaFileExists = false;
		};

		void
		Report(ScanReport& report, ScanIssueKind kind, std::string_view path, std::string message, bool fixed = false)
		{
			GB_CORE_WARN("Asset scan: {}: {}", path, message);
			report.Issues.push_back({ kind, std::string(path), std::move(message), fixed });
		}

		Result<void> ValidatePath(std::string_view path)
		{
			Result<std::string> normalized = FileSystem::NormalizePath(path);
			if (!normalized || normalized->empty() || FileSystem::IsAbsolutePath(*normalized) || *normalized != path)
			{
				return Error::Format("'{}' is not a normalized relative asset path", path);
			}
			return {};
		}

		// Writes the .meta file of a candidate that has a handle now.
		Result<void> WriteMetadata(VFS& vfs, const Candidate& candidate)
		{
			Result<std::string> text = candidate.Metadata.Serialize();
			if (!text)
			{
				return text.GetError();
			}
			return vfs.WriteText(AssetMetadata::GetMetaPath(candidate.Path), *text);
		}
	}

	std::string_view ToString(ScanIssueKind kind) noexcept
	{
		switch (kind)
		{
			case ScanIssueKind::MissingMeta:
				return "MissingMeta";
			case ScanIssueKind::InvalidMeta:
				return "InvalidMeta";
			case ScanIssueKind::MissingHandle:
				return "MissingHandle";
			case ScanIssueKind::DuplicateHandle:
				return "DuplicateHandle";
			case ScanIssueKind::TypeMismatch:
				return "TypeMismatch";
			case ScanIssueKind::OrphanedMeta:
				return "OrphanedMeta";
		}
		return "Unknown";
	}

	size_t ScanReport::Count(ScanIssueKind kind) const noexcept
	{
		return static_cast<size_t>(
			std::ranges::count_if(Issues, [kind](const ScanIssue& issue) { return issue.Kind == kind; }));
	}

	Result<void> AssetRegistry::Add(AssetRecord record)
	{
		if (!record.Handle.IsValid())
		{
			return Error("an asset needs a handle");
		}
		if (record.Type == AssetType::None)
		{
			return Error::Format("asset '{}' needs a type", record.Path);
		}
		if (Result<void> valid = ValidatePath(record.Path); !valid)
		{
			return valid;
		}
		if (m_PathOfHandle.contains(record.Handle))
		{
			return Error::Format(
				"handle {} is already used by '{}'", record.Handle.ToString(), m_PathOfHandle.at(record.Handle));
		}
		if (m_ByPath.contains(record.Path))
		{
			return Error::Format("'{}' is already registered", record.Path);
		}
		m_PathOfHandle.emplace(record.Handle, record.Path);
		m_ByPath.emplace(record.Path, std::move(record));
		return {};
	}

	bool AssetRegistry::Remove(AssetHandle handle)
	{
		const auto found = m_PathOfHandle.find(handle);
		if (found == m_PathOfHandle.end())
		{
			return false;
		}
		m_ByPath.erase(found->second);
		m_PathOfHandle.erase(found);
		return true;
	}

	void AssetRegistry::Clear() noexcept
	{
		m_ByPath.clear();
		m_PathOfHandle.clear();
	}

	const AssetRecord* AssetRegistry::Find(AssetHandle handle) const
	{
		const auto found = m_PathOfHandle.find(handle);
		return found == m_PathOfHandle.end() ? nullptr : &m_ByPath.find(found->second)->second;
	}

	const AssetRecord* AssetRegistry::FindByPath(std::string_view path) const
	{
		const auto found = m_ByPath.find(path);
		return found == m_ByPath.end() ? nullptr : &found->second;
	}

	std::vector<AssetRecord> AssetRegistry::List(AssetType type) const
	{
		std::vector<AssetRecord> records;
		for (const auto& [path, record] : m_ByPath)
		{
			if (type == AssetType::None || record.Type == type)
			{
				records.push_back(record);
			}
		}
		return records;
	}

	AssetHandle AssetRegistry::GenerateHandle() const
	{
		for (;;)
		{
			const AssetHandle handle = UUID::Generate();
			if (!Contains(handle))
			{
				return handle;
			}
		}
	}

	ScanReport AssetRegistry::Scan(VFS& vfs, std::span<const std::string> directories, const ScanOptions& options)
	{
		Clear();
		ScanReport report;

		// Every file below the directories, sorted: the order of the scan, so the same files always give the same
		// result (which of two assets with one handle keeps it, for example).
		std::set<std::string> files;
		for (const std::string& directory : directories)
		{
			const Result<std::vector<DirectoryEntry>> listing = vfs.List(directory, true);
			if (!listing)
			{
				if (!HasFileError(listing.GetError(), FileError::NotFound))
				{
					GB_CORE_WARN("Asset scan: cannot list '{}': {}", directory, listing.GetError().Message);
				}
				continue;
			}
			for (const DirectoryEntry& entry : *listing)
			{
				if (!entry.IsDirectory)
				{
					files.insert(entry.Path);
				}
			}
		}

		// Read the metadata of every asset first, so that handles generated later cannot clash with any of them.
		std::vector<Candidate> candidates;
		for (const std::string& path : files)
		{
			const AssetType type = DetectAssetType(path);
			if (AssetMetadata::IsMetaPath(path) || type == AssetType::None)
			{
				continue;
			}
			Candidate candidate;
			candidate.Path = path;
			candidate.Type = type;
			candidate.Metadata.Type = type;
			const std::string metaPath = AssetMetadata::GetMetaPath(path);
			candidate.MetaFileExists = files.contains(metaPath);
			if (!candidate.MetaFileExists)
			{
				candidate.NeedsHandle = true;
			}
			else
			{
				Result<std::string> text = vfs.ReadText(metaPath);
				LoadReport parseReport;
				Result<AssetMetadata> parsed =
					text ? AssetMetadata::Parse(*text, metaPath, parseReport) : Result<AssetMetadata>(text.GetError());
				parseReport.Log(metaPath);
				if (!parsed)
				{
					Report(
						report, ScanIssueKind::InvalidMeta, path,
						std::format(
							"cannot read '{}': {}. Fix or delete the file; the asset is not registered.", metaPath,
							parsed.GetError().Message));
					continue;
				}
				candidate.Metadata = std::move(*parsed);
				candidate.NeedsHandle = !candidate.Metadata.Handle.IsValid();
				if (candidate.Metadata.Type != AssetType::None && candidate.Metadata.Type != type)
				{
					Report(
						report, ScanIssueKind::TypeMismatch, path,
						std::format(
							"the .meta file says {} but the file is a {}; treated as {}",
							ToString(candidate.Metadata.Type), ToString(type), ToString(type)));
				}
				candidate.Metadata.Type = type;
			}
			candidates.push_back(std::move(candidate));
		}

		const auto registerAsset = [this](AssetHandle handle, const Candidate& candidate) {
			[[maybe_unused]] const Result<void> added = Add({ handle, candidate.Path, candidate.Type });
			GB_CORE_ASSERT(added.HasValue(), "Asset scan: a checked asset was refused");
		};

		// Assets that have a handle claim it, in path order.
		std::vector<Candidate*> unnamed;
		for (Candidate& candidate : candidates)
		{
			if (candidate.NeedsHandle)
			{
				unnamed.push_back(&candidate);
				continue;
			}
			const AssetHandle handle = candidate.Metadata.Handle;
			if (const AssetRecord* owner = Find(handle); owner != nullptr)
			{
				Report(
					report, ScanIssueKind::DuplicateHandle, candidate.Path,
					std::format(
						"handle {} is already used by '{}'; this asset is not registered until it gets a new handle",
						handle.ToString(), owner->Path));
				continue;
			}
			registerAsset(handle, candidate);
		}

		// The others get a handle and a .meta file, if the content may be written.
		for (Candidate* candidate : unnamed)
		{
			const bool hadMetaFile = candidate->MetaFileExists;
			if (!options.WriteMetas)
			{
				Report(
					report, hadMetaFile ? ScanIssueKind::MissingHandle : ScanIssueKind::MissingMeta, candidate->Path,
					hadMetaFile ? "the .meta file has no handle; the asset is not registered"
								: "there is no .meta file; the asset is not registered");
				continue;
			}
			candidate->Metadata.Handle = GenerateHandle();
			if (Result<void> written = WriteMetadata(vfs, *candidate); !written)
			{
				Report(
					report, hadMetaFile ? ScanIssueKind::MissingHandle : ScanIssueKind::MissingMeta, candidate->Path,
					std::format(
						"cannot write the .meta file ({}); the asset is not registered", written.GetError().Message));
				continue;
			}
			registerAsset(candidate->Metadata.Handle, *candidate);
			if (hadMetaFile)
			{
				Report(
					report, ScanIssueKind::MissingHandle, candidate->Path,
					std::format("the .meta file had no handle; assigned {}", candidate->Metadata.Handle.ToString()),
					true);
			}
			else
			{
				++report.CreatedMetas;
			}
		}

		for (const std::string& path : files)
		{
			if (!AssetMetadata::IsMetaPath(path))
			{
				continue;
			}
			const std::string asset = path.substr(0, path.size() - AssetMetadata::FileExtension.size());
			if (!files.contains(asset) || DetectAssetType(asset) == AssetType::None)
			{
				Report(report, ScanIssueKind::OrphanedMeta, path, "there is no asset file next to this .meta file");
			}
		}

		report.AssetCount = GetCount();
		if (report.CreatedMetas > 0)
		{
			GB_CORE_INFO("Asset scan: created {} .meta files", report.CreatedMetas);
		}
		return report;
	}
}
