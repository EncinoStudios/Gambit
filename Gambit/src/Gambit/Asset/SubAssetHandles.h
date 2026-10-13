#pragma once

#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetType.h"

#include <functional>
#include <string_view>
#include <vector>

namespace Gambit
{
	// The handles of the sub-assets of the file being loaded (D-038): those its .meta file lists, so that re-importing
	// gives the same assets the same handles, plus new ones for keys seen for the first time, which the AssetManager
	// then writes to the .meta file.
	class SubAssetHandles
	{
	public:
		// `known` is the .meta file's list; `generate` makes a handle no other asset uses.
		SubAssetHandles(std::vector<SubAssetEntry> known, std::function<AssetHandle()> generate);

		// The handle of the sub-asset `key` (unique within the file, e.g. "Mesh/Body") of type `type`: the known one,
		// else a new one. A known key of another type gets a new handle too (its references were to another kind of
		// asset). Asking twice for a key gives the same handle.
		[[nodiscard]] AssetHandle Acquire(std::string_view key, AssetType type);

		// The known entries followed by the new ones; the keys of earlier imports stay even when this import did not
		// ask for them, so a sub-asset that comes back keeps its handle.
		[[nodiscard]] const std::vector<SubAssetEntry>& GetEntries() const noexcept { return m_Entries; }
		// Whether Acquire added or replaced entries, which the .meta file then needs.
		[[nodiscard]] bool HasChanged() const noexcept { return m_Changed; }

	private:
		std::vector<SubAssetEntry> m_Entries;
		std::function<AssetHandle()> m_Generate;
		bool m_Changed = false;
	};
}
