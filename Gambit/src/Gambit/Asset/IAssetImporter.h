#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetRegistry.h"
#include "Gambit/Asset/SubAssetHandles.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/Json.h"

namespace Gambit
{
	// What an importer gets to load one asset.
	struct AssetLoadContext
	{
		const AssetRecord& Record;     // handle, path and type
		const AssetMetadata& Metadata; // import settings from the .meta file
		const VFS& Vfs;                // read the file (and anything it refers to) through this
		const TypeRegistry& Types;
		LoadReport& Report;         // non-fatal problems; the manager logs them
		SubAssetHandles& SubAssets; // handles for the assets created inside this one (Asset::GetSubAssets)
	};

	// Loads the assets of one type from their files (D-014). Registered with AssetManager::RegisterImporter; every
	// manager starts with the importers of the engine (RegisterEngineAssets).
	class IAssetImporter
	{
	public:
		virtual ~IAssetImporter() = default;

		[[nodiscard]] virtual AssetType GetType() const noexcept = 0;
		// Loads the asset synchronously. The manager sets its handle and path. Never throws; a file that cannot be
		// loaded is an error that names the file.
		[[nodiscard]] virtual Result<Ref<Asset>> Load(const AssetLoadContext& context) const = 0;
	};
}
