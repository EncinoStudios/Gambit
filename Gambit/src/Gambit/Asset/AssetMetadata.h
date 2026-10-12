#pragma once

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Gambit
{
	// The contents of the .meta sidecar file that gives an asset its handle (D-014, D-025):
	//   { "Version": 1, "Handle": "0x...", "Type": "Texture", "ImportSettings": { ... } }
	// It sits next to the asset ("Assets/Crate.png.meta"), travels with it and is written when the asset is
	// imported or first found. ImportSettings belong to the importer of the type.
	struct AssetMetadata
	{
		static constexpr uint32_t FormatVersion = 1;
		static constexpr std::string_view FileExtension = ".meta";

		AssetHandle Handle;
		AssetType Type = AssetType::None;
		Json ImportSettings = Json::object();

		// The sidecar of an asset path: "Assets/Crate.png" gives "Assets/Crate.png.meta".
		[[nodiscard]] static std::string GetMetaPath(std::string_view assetPath);
		// Whether the path names a sidecar file.
		[[nodiscard]] static bool IsMetaPath(std::string_view path) noexcept;

		[[nodiscard]] static const MigrationTable& GetMigrations();
		// A missing "Handle" or "Type" leaves the member null/None (the scan repairs or reports it); a malformed one or
		// an unknown type name is an error, an unknown member a warning.
		[[nodiscard]] static Result<AssetMetadata>
		Parse(std::string_view text, std::string_view source, LoadReport& report);
		[[nodiscard]] Result<std::string> Serialize() const;
	};
}
