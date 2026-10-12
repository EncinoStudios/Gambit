#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Serialization/SceneSerializer.h"

namespace Gambit
{
	// A scene file loaded as an asset: decoded and checked, ready to be loaded into a World with
	// SceneSerializer::Apply (the scene a project starts with, scenes that scripts switch to).
	class SceneAsset final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Scene;

		// `types` finds the asset references that make up the dependencies.
		SceneAsset(const TypeRegistry& types, SceneData data);

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		[[nodiscard]] std::vector<AssetHandle> GetDependencies() const override { return m_Dependencies; }

		[[nodiscard]] const SceneData& GetData() const noexcept { return m_Data; }

	private:
		SceneData m_Data;
		std::vector<AssetHandle> m_Dependencies;
	};
}
