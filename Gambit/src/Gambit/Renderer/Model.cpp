#include "gbpch.h"

#include "Gambit/Renderer/Model.h"

namespace Gambit
{
	Model::Model(std::vector<ModelNode> nodes, std::vector<SubAssetRef> subAssets)
		: m_Nodes(std::move(nodes)), m_SubAssets(std::move(subAssets))
	{
		for (size_t index = 0; index < m_Nodes.size(); ++index)
		{
			const ModelNode& node = m_Nodes[index];
			GB_CORE_VERIFY(
				node.Parent < static_cast<int32_t>(index), "model node {} comes before its parent {}", index,
				node.Parent);
			if (node.Parent < 0)
			{
				m_Roots.push_back(static_cast<uint32_t>(index));
			}
			else
			{
				const std::vector<uint32_t>& siblings = m_Nodes[static_cast<size_t>(node.Parent)].Children;
				GB_CORE_VERIFY(
					std::ranges::find(siblings, static_cast<uint32_t>(index)) != siblings.end(),
					"model node {} is missing from its parent's children", index);
			}
		}
		for (const SubAssetRef& subAsset : m_SubAssets)
		{
			GB_CORE_VERIFY(
				subAsset.Handle.IsValid() && subAsset.Object != nullptr,
				"a model's sub-asset needs a handle and an asset");
		}
	}

	std::vector<AssetHandle> Model::GetSubAssetHandles(AssetType type) const
	{
		std::vector<AssetHandle> handles;
		for (const SubAssetRef& subAsset : m_SubAssets)
		{
			if (subAsset.Object->GetType() == type)
			{
				handles.push_back(subAsset.Handle);
			}
		}
		return handles;
	}
}
