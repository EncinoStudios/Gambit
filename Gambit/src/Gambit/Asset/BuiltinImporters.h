#pragma once

#include "Gambit/Asset/IAssetImporter.h"

// The importers the engine registers in every AssetManager: the assets whose formats the engine owns. Importers for
// textures, meshes, audio and the like arrive with their modules.

namespace Gambit
{
	// .gscene files as SceneAsset.
	class SceneImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Scene; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};

	// .gprefab files as Prefab.
	class PrefabImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Prefab; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};

	// .csv files as DataTable.
	class DataTableImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::DataTable; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};
}
