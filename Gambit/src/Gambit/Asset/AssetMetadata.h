#pragma once

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Gambit
{
	// An asset inside a file (D-038): a mesh, material or texture of a glTF model. Its key names it within the file
	// ("Mesh/Body", "Texture/#2"); the handle stays the same as long as re-imports produce the same key.
	struct SubAssetEntry
	{
		std::string Key;
		AssetType Type = AssetType::None;
		AssetHandle Handle;

		friend bool operator==(const SubAssetEntry&, const SubAssetEntry&) = default;
	};

	// The contents of the .meta sidecar file that gives an asset its handle (D-014, D-025):
	//   { "Version": 1, "Handle": "0x...", "Type": "Texture", "ImportSettings": { ... },
	//     "SubAssets": [ { "Key": "Mesh/Body", "Type": "Mesh", "Handle": "0x..." }, ... ] }
	// It sits next to the asset ("Assets/Crate.png.meta"), travels with it and is written when the asset is
	// imported or first found. ImportSettings belong to the importer of the type. SubAssets (left out when empty) lists
	// the handles of the assets inside the file, which the AssetManager adds when an import produces new ones.
	struct AssetMetadata
	{
		static constexpr uint32_t FormatVersion = 1;
		static constexpr std::string_view FileExtension = ".meta";

		AssetHandle Handle;
		AssetType Type = AssetType::None;
		Json ImportSettings = Json::object();
		// Keys and handles are unique and handles valid (Parse checks it).
		std::vector<SubAssetEntry> SubAssets;

		// The sidecar of an asset path: "Assets/Crate.png" gives "Assets/Crate.png.meta".
		[[nodiscard]] static std::string GetMetaPath(std::string_view assetPath);
		// Whether the path names a sidecar file.
		[[nodiscard]] static bool IsMetaPath(std::string_view path) noexcept;

		[[nodiscard]] static const MigrationTable& GetMigrations();
		// A missing "Handle" or "Type" leaves the member null/None (the scan repairs or reports it); a malformed one or
		// an unknown type name is an error, an unknown member a warning. Sub-assets need a key, a type and a valid
		// handle, all unique.
		[[nodiscard]] static Result<AssetMetadata>
		Parse(std::string_view text, std::string_view source, LoadReport& report);
		[[nodiscard]] Result<std::string> Serialize() const;
	};
}
