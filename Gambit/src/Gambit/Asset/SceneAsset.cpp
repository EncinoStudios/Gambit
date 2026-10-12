#include "gbpch.h"

#include "Gambit/Asset/SceneAsset.h"

namespace Gambit
{
	SceneAsset::SceneAsset(const TypeRegistry& types, SceneData data)
		: m_Data(std::move(data)), m_Dependencies(CollectAssetReferences(types, m_Data.Entities))
	{}
}
