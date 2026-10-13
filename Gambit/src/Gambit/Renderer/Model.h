#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Renderer/Components/RenderComponents.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Gambit
{
	// One node of a model's hierarchy: a transform relative to its parent and what hangs at it.
	struct ModelNode
	{
		std::string Name;
		int32_t Parent = -1; // index in Model::GetNodes(), -1 for roots
		std::vector<uint32_t> Children;
		glm::vec3 Translation{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
		glm::mat4 ModelTransform{ 1.0f }; // node to model space (the product of the local transforms from the root)
		AssetHandle Mesh;                 // a Mesh sub-asset; null for nodes without a mesh
		std::optional<CameraComponent> Camera;
		std::optional<DirectionalLightComponent> DirectionalLight;
		std::optional<PointLightComponent> PointLight;
		std::optional<SpotLightComponent> SpotLight;
	};

	// A model asset (a glTF file, D-038): the node hierarchy of its scene with its meshes, materials and textures as
	// sub-assets, which have handles of their own. ModelUtils::Instantiate turns it into entities; a MeshRenderer may
	// also draw it whole.
	class Model final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Model;

		// `nodes` are in hierarchy pre-order (a parent before its children) with consistent Parent and Children
		// indices; `subAssets` holds every mesh, material and texture of the model (verified).
		Model(std::vector<ModelNode> nodes, std::vector<SubAssetRef> subAssets);

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		[[nodiscard]] std::vector<SubAssetRef> GetSubAssets() const override { return m_SubAssets; }

		[[nodiscard]] const std::vector<ModelNode>& GetNodes() const noexcept { return m_Nodes; }
		// The indices of the root nodes, in order.
		[[nodiscard]] const std::vector<uint32_t>& GetRoots() const noexcept { return m_Roots; }
		// The handles of the sub-assets of one type, in the order the model lists them.
		[[nodiscard]] std::vector<AssetHandle> GetSubAssetHandles(AssetType type) const;

	private:
		std::vector<ModelNode> m_Nodes;
		std::vector<uint32_t> m_Roots;
		std::vector<SubAssetRef> m_SubAssets;
	};
}
