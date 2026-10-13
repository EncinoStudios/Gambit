#include "gbpch.h"

#include "Gambit/Renderer/MeshSource.h"

#include <set>

namespace Gambit
{
	namespace
	{
		template<typename T>
		Result<void> CheckStream(const std::vector<T>& stream, size_t vertexCount, std::string_view name)
		{
			if (!stream.empty() && stream.size() != vertexCount)
			{
				return Error::Format(
					"the {} stream has {} entries for {} vertices (it must be empty or have one per vertex)", name,
					stream.size(), vertexCount);
			}
			return {};
		}
	}

	Result<void> MeshData::Validate() const
	{
		const size_t vertexCount = Positions.size();
		if (vertexCount == 0 || Indices.empty() || Submeshes.empty())
		{
			return Error("a mesh needs vertices, indices and at least one submesh");
		}
		if (vertexCount > std::numeric_limits<uint32_t>::max() || Indices.size() > std::numeric_limits<uint32_t>::max())
		{
			return Error("a mesh holds at most 2^32 - 1 vertices and indices");
		}
		for (const Result<void>& check :
		     { CheckStream(Normals, vertexCount, "normal"), CheckStream(Tangents, vertexCount, "tangent"),
		       CheckStream(UV0, vertexCount, "UV0"), CheckStream(UV1, vertexCount, "UV1"),
		       CheckStream(Colors, vertexCount, "color"), CheckStream(Joints, vertexCount, "joint"),
		       CheckStream(Weights, vertexCount, "weight") })
		{
			if (!check)
			{
				return check;
			}
		}
		if (Joints.empty() != Weights.empty())
		{
			return Error("joints and weights come together");
		}
		if (Indices.size() % 3 != 0)
		{
			return Error::Format("{} indices are not whole triangles", Indices.size());
		}
		for (const glm::vec3& position : Positions)
		{
			if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
			{
				return Error("a vertex position is not finite");
			}
		}
		for (size_t index = 0; index < Submeshes.size(); ++index)
		{
			const Submesh& submesh = Submeshes[index];
			const uint64_t indexEnd = uint64_t(submesh.BaseIndex) + submesh.IndexCount;
			const uint64_t vertexEnd = uint64_t(submesh.BaseVertex) + submesh.VertexCount;
			if (submesh.IndexCount == 0 || submesh.IndexCount % 3 != 0 || indexEnd > Indices.size())
			{
				return Error::Format("submesh {} has an invalid index range", index);
			}
			if (submesh.VertexCount == 0 || vertexEnd > vertexCount)
			{
				return Error::Format("submesh {} has an invalid vertex range", index);
			}
			if (submesh.MaterialSlot >= Materials.size())
			{
				return Error::Format(
					"submesh {} uses material slot {} of {}", index, submesh.MaterialSlot, Materials.size());
			}
			for (uint64_t i = submesh.BaseIndex; i < indexEnd; ++i)
			{
				const uint32_t vertex = Indices[i];
				if (vertex < submesh.BaseVertex || vertex >= vertexEnd)
				{
					return Error::Format(
						"index {} of submesh {} refers to vertex {}, outside its vertex range", i, index, vertex);
				}
			}
		}
		return {};
	}

	MeshSource::MeshSource(MeshData data)
		: m_Data(std::move(data))
	{
		const Result<void> valid = m_Data.Validate();
		GB_CORE_VERIFY(valid.HasValue(), "invalid mesh data: {}", valid.GetError().Message);
	}

	std::vector<AssetHandle> MeshSource::GetDependencies() const
	{
		std::set<AssetHandle> handles;
		for (const AssetHandle handle : m_Data.Materials)
		{
			if (handle.IsValid())
			{
				handles.insert(handle);
			}
		}
		return { handles.begin(), handles.end() };
	}
}
