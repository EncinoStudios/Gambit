#pragma once

#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Gambit
{
	// One known asset: its handle, its project-relative path and its type.
	struct AssetRecord
	{
		AssetHandle Handle;
		std::string Path;
		AssetType Type = AssetType::None;
	};

	// The kinds of problems a scan reports (each is also logged as a warning).
	enum class ScanIssueKind
	{
		MissingMeta,     // an asset without a .meta file that could not be created
		InvalidMeta,     // a .meta file that cannot be read; the asset is not registered
		MissingHandle,   // a .meta file without a handle
		DuplicateHandle, // two assets with the same handle: the one that had it before the scan keeps it, else the
		                 // first in path order; the other is not registered
		TypeMismatch,    // the .meta file names another type than the file does; the file wins
		OrphanedMeta,    // a .meta file whose asset is gone
		InvalidPath      // an asset file whose path is not a portable project path (a reserved name, ':', '\\');
		                 // it is not registered and no .meta file is written for it
	};

	[[nodiscard]] std::string_view ToString(ScanIssueKind kind) noexcept;

	struct ScanIssue
	{
		ScanIssueKind Kind = ScanIssueKind::InvalidMeta;
		std::string Path; // the asset, or the .meta file for orphans
		std::string Message;
		bool Fixed = false; // the scan repaired it (by writing a .meta file)
	};

	struct ScanOptions
	{
		// Create the .meta file of assets that have none and give a handle to .meta files that lack one. With false
		// (read-only content) such assets are reported and not registered.
		bool WriteMetas = true;
	};

	struct ScanReport
	{
		size_t AssetCount = 0;   // registered assets
		size_t CreatedMetas = 0; // .meta files written for assets that had none
		std::vector<ScanIssue> Issues;

		[[nodiscard]] size_t Count(ScanIssueKind kind) const noexcept;
	};

	// The table of the assets of a project: handle to path to type (D-014), built by scanning the project through the
	// VFS. Handles and paths are unique. Not thread safe.
	class AssetRegistry
	{
	public:
		// Fails for a null handle, a type of None, a path that is not a normalized relative path, or when the handle or
		// the path is taken.
		[[nodiscard]] Result<void> Add(AssetRecord record);
		// False when the handle is not registered.
		bool Remove(AssetHandle handle);
		void Clear() noexcept;

		[[nodiscard]] const AssetRecord* Find(AssetHandle handle) const;
		[[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const;
		[[nodiscard]] bool Contains(AssetHandle handle) const { return Find(handle) != nullptr; }
		[[nodiscard]] size_t GetCount() const noexcept { return m_ByPath.size(); }
		// Every record sorted by path, or those of `type` unless it is None.
		[[nodiscard]] std::vector<AssetRecord> List(AssetType type = AssetType::None) const;

		// A random handle that no registered asset has (for tools: assets are not simulation).
		[[nodiscard]] AssetHandle GenerateHandle() const;

		// Replaces the content by what is below `directories` (project-relative, searched recursively): every file
		// whose name says it is an asset (DetectAssetType) is registered with the handle of its .meta file. Missing
		// .meta files are created and problems reported, see ScanIssueKind; a scan never deletes anything. A handle
		// that several files claim stays with the file that had it before the scan (rescanning after a file was copied
		// with its .meta file keeps references on the original), else with the first in path order.
		ScanReport Scan(VFS& vfs, std::span<const std::string> directories, const ScanOptions& options = {});

	private:
		std::map<std::string, AssetRecord, std::less<>> m_ByPath;
		std::unordered_map<AssetHandle, std::string> m_PathOfHandle; // lookups only
	};
}
