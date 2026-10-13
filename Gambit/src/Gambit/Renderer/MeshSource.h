#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Bounds.h"
#include "Gambit/Math/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Gambit
{
	// A range of a mesh drawn with one material: indices [BaseIndex, BaseIndex + IndexCount) of the mesh's index
	// list, which refer to vertices [BaseVertex, BaseVertex + VertexCount) (indices are absolute, not relative to
	// BaseVertex).
	struct Submesh
	{
		std::string Name;
		uint32_t BaseIndex = 0;
		uint32_t IndexCount = 0;
		uint32_t BaseVertex = 0;
		uint32_t VertexCount = 0;
		uint32_t MaterialSlot = 0; // index into MeshData::Materials
		AABB Bounds;               // of the submesh's vertices (MeshUtils::ComputeBounds)
	};

	// The CPU data of a triangle mesh, for the code that builds meshes (importers, MeshFactory, tools). Every vertex
	// stream other than Positions is either empty or has one entry per position. Triangles are counter-clockwise when
	// seen from the side their normals point to (glTF's convention; the renderer flips its front-face setting to match
	// NVRHI's clockwise default). Tangents are xyz plus the bitangent sign in w (MikkTSpace: bitangent = sign *
	// cross(normal, tangent)).
	struct MeshData
	{
		std::vector<glm::vec3> Positions;
		std::vector<glm::vec3> Normals;
		std::vector<glm::vec4> Tangents;
		std::vector<glm::vec2> UV0;
		std::vector<glm::vec2> UV1;
		std::vector<glm::vec4> Colors;    // linear RGBA, multiplied with the base color
		std::vector<glm::u16vec4> Joints; // skinning (T14): four joint indices per vertex
		std::vector<glm::vec4> Weights;   // and their weights
		std::vector<uint32_t> Indices;    // three per triangle
		std::vector<Submesh> Submeshes;
		std::vector<AssetHandle> Materials; // the default material of each slot (null: the default material)
		AABB Bounds;
		BoundingSphere Sphere;

		[[nodiscard]] size_t GetVertexCount() const noexcept { return Positions.size(); }
		// Checks the invariants above: stream sizes, index count a multiple of three, indices inside their submesh's
		// vertex range, submesh ranges inside the lists, material slots that exist, finite positions. The error says
		// what is wrong.
		[[nodiscard]] Result<void> Validate() const;
	};

	// A mesh asset (D-014): validated MeshData with its bounds. The renderer uploads it as a GPU Mesh
	// (RenderResources). Meshes are sub-assets of models (D-038) or built-in (MeshFactory, D-039).
	class MeshSource final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Mesh;

		// `data` must pass MeshData::Validate (verified); call MeshUtils::ComputeBounds first.
		explicit MeshSource(MeshData data);

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		// The default materials of the slots.
		[[nodiscard]] std::vector<AssetHandle> GetDependencies() const override;

		[[nodiscard]] const MeshData& GetData() const noexcept { return m_Data; }
		[[nodiscard]] const std::vector<Submesh>& GetSubmeshes() const noexcept { return m_Data.Submeshes; }
		[[nodiscard]] const std::vector<AssetHandle>& GetMaterials() const noexcept { return m_Data.Materials; }
		[[nodiscard]] const AABB& GetBounds() const noexcept { return m_Data.Bounds; }
		[[nodiscard]] const BoundingSphere& GetBoundingSphere() const noexcept { return m_Data.Sphere; }
		[[nodiscard]] size_t GetVertexCount() const noexcept { return m_Data.Positions.size(); }
		[[nodiscard]] size_t GetIndexCount() const noexcept { return m_Data.Indices.size(); }

	private:
		MeshData m_Data;
	};
}
