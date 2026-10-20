#include "gbpch.h"

#include "Gambit/Renderer/MeshUtils.h"

#include <mikktspace.h>

#include <limits>

namespace Gambit::MeshUtils
{
	namespace
	{
		// Appends vertex `vertex` of `source` to every stream of `target` that `source` has.
		void AppendVertex(MeshData& target, const MeshData& source, uint32_t vertex)
		{
			target.Positions.push_back(source.Positions[vertex]);
			const auto copy = [vertex](auto& to, const auto& from) {
				if (!from.empty())
				{
					to.push_back(from[vertex]);
				}
			};
			copy(target.Normals, source.Normals);
			copy(target.Tangents, source.Tangents);
			copy(target.UV0, source.UV0);
			copy(target.UV1, source.UV1);
			copy(target.Colors, source.Colors);
			copy(target.Joints, source.Joints);
			copy(target.Weights, source.Weights);
		}

		// `source` without its vertices and indices: what a rebuild starts from.
		MeshData CopyStructure(const MeshData& source)
		{
			MeshData target;
			target.Submeshes = source.Submeshes;
			target.Materials = source.Materials;
			target.Indices.reserve(source.Indices.size());
			return target;
		}

		// A unit vector perpendicular to `normal`, for vertices without texture coordinates.
		glm::vec4 PerpendicularTangent(const glm::vec3& normal)
		{
			const glm::vec3 helper =
				std::abs(normal.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 tangent = glm::cross(helper, normal);
			const float length = glm::length(tangent);
			return length > 0.0f ? glm::vec4(tangent / length, 1.0f) : glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		}

		// The triangles of one submesh as MikkTSpace sees them: faces of three corners.
		struct TangentContext
		{
			const MeshData& Mesh;
			const Submesh& Range;
			std::vector<glm::vec4> CornerTangents; // per corner, filled by MikkTSpace

			[[nodiscard]] uint32_t GetVertex(int face, int corner) const
			{
				return Mesh.Indices[Range.BaseIndex + static_cast<uint32_t>(face) * 3 + static_cast<uint32_t>(corner)];
			}
		};

		TangentContext& GetContext(const SMikkTSpaceContext* context)
		{
			return *static_cast<TangentContext*>(context->m_pUserData);
		}

		int GetFaceCount(const SMikkTSpaceContext* context)
		{
			return static_cast<int>(GetContext(context).Range.IndexCount / 3);
		}

		int GetFaceVertexCount(const SMikkTSpaceContext*, int)
		{
			return 3;
		}

		void GetPosition(const SMikkTSpaceContext* context, float* out, int face, int corner)
		{
			const TangentContext& data = GetContext(context);
			const glm::vec3& position = data.Mesh.Positions[data.GetVertex(face, corner)];
			std::memcpy(out, glm::value_ptr(position), sizeof(float) * 3);
		}

		void GetNormal(const SMikkTSpaceContext* context, float* out, int face, int corner)
		{
			const TangentContext& data = GetContext(context);
			const glm::vec3& normal = data.Mesh.Normals[data.GetVertex(face, corner)];
			std::memcpy(out, glm::value_ptr(normal), sizeof(float) * 3);
		}

		// MikkTSpace assumes V up (OpenGL); glTF's UVs have V down from the top-left corner. Flipping V gives the
		// tangent signs that DCC tools export for glTF (bitangent = sign * cross(normal, tangent) points up the image).
		void GetTexCoord(const SMikkTSpaceContext* context, float* out, int face, int corner)
		{
			const TangentContext& data = GetContext(context);
			const glm::vec2& uv = data.Mesh.UV0[data.GetVertex(face, corner)];
			out[0] = uv.x;
			out[1] = 1.0f - uv.y;
		}

		void SetTangent(const SMikkTSpaceContext* context, const float* tangent, float sign, int face, int corner)
		{
			TangentContext& data = GetContext(context);
			data.CornerTangents[static_cast<size_t>(face) * 3 + static_cast<size_t>(corner)] =
				glm::vec4(tangent[0], tangent[1], tangent[2], sign);
		}

		bool SameBits(const glm::vec4& a, const glm::vec4& b)
		{
			return std::memcmp(glm::value_ptr(a), glm::value_ptr(b), sizeof(glm::vec4)) == 0;
		}
	}

	void ComputeBounds(MeshData& mesh)
	{
		for (Submesh& submesh : mesh.Submeshes)
		{
			const auto first = mesh.Positions.begin() + submesh.BaseVertex;
			submesh.Bounds = AABB::FromPoints(std::span<const glm::vec3>(first, first + submesh.VertexCount));
		}
		mesh.Bounds = AABB::FromPoints(mesh.Positions);
		mesh.Sphere = BoundingSphere::FromPoints(mesh.Positions);
	}

	void GenerateFlatNormals(MeshData& mesh)
	{
		MeshData result = CopyStructure(mesh);
		for (size_t index = 0; index < mesh.Submeshes.size(); ++index)
		{
			const Submesh& source = mesh.Submeshes[index];
			Submesh& target = result.Submeshes[index];
			target.BaseIndex = static_cast<uint32_t>(result.Indices.size());
			target.BaseVertex = static_cast<uint32_t>(result.Positions.size());
			for (uint32_t corner = 0; corner < source.IndexCount; corner += 3)
			{
				const uint32_t* triangle = &mesh.Indices[source.BaseIndex + corner];
				const glm::vec3 edge1 = mesh.Positions[triangle[1]] - mesh.Positions[triangle[0]];
				const glm::vec3 edge2 = mesh.Positions[triangle[2]] - mesh.Positions[triangle[0]];
				const glm::vec3 cross = glm::cross(edge1, edge2);
				const float length = glm::length(cross);
				const glm::vec3 normal = length > 0.0f ? cross / length : glm::vec3(0.0f, 1.0f, 0.0f);
				for (int i = 0; i < 3; ++i)
				{
					result.Indices.push_back(static_cast<uint32_t>(result.Positions.size()));
					AppendVertex(result, mesh, triangle[i]);
					if (mesh.Normals.empty())
					{
						result.Normals.push_back(normal);
					}
					else
					{
						result.Normals.back() = normal;
					}
				}
			}
			target.IndexCount = source.IndexCount;
			target.VertexCount = static_cast<uint32_t>(result.Positions.size()) - target.BaseVertex;
		}
		result.Bounds = mesh.Bounds;
		result.Sphere = mesh.Sphere;
		mesh = std::move(result);
	}

	Result<void> GenerateTangents(MeshData& mesh)
	{
		if (mesh.Normals.size() != mesh.Positions.size())
		{
			return Error("tangents need normals");
		}
		for (size_t index = 0; index < mesh.Submeshes.size(); ++index)
		{
			if (mesh.Submeshes[index].IndexCount < 3)
			{
				return Error::Format("submesh {} has no triangles", index);
			}
		}
		if (mesh.UV0.empty())
		{
			mesh.Tangents.resize(mesh.Positions.size());
			for (size_t vertex = 0; vertex < mesh.Positions.size(); ++vertex)
			{
				mesh.Tangents[vertex] = PerpendicularTangent(mesh.Normals[vertex]);
			}
			return {};
		}

		SMikkTSpaceInterface callbacks{};
		callbacks.m_getNumFaces = &GetFaceCount;
		callbacks.m_getNumVerticesOfFace = &GetFaceVertexCount;
		callbacks.m_getPosition = &GetPosition;
		callbacks.m_getNormal = &GetNormal;
		callbacks.m_getTexCoord = &GetTexCoord;
		callbacks.m_setTSpaceBasic = &SetTangent;

		MeshData source = std::move(mesh);
		source.Tangents.clear();
		MeshData result = CopyStructure(source);
		result.Tangents.reserve(source.Positions.size());
		for (size_t index = 0; index < source.Submeshes.size(); ++index)
		{
			const Submesh& range = source.Submeshes[index];
			TangentContext context{ source, range, std::vector<glm::vec4>(range.IndexCount) };
			SMikkTSpaceContext mikk{ &callbacks, &context };
			if (genTangSpaceDefault(&mikk) == 0)
			{
				mesh = std::move(source);
				return Error::Format("MikkTSpace failed on submesh {} (out of memory)", index);
			}

			// The submesh's vertices keep their places (with the tangent of their first corner); a corner whose
			// tangent differs from its vertex's gets a copy of the vertex, appended to the submesh's range.
			Submesh& target = result.Submeshes[index];
			target.BaseIndex = static_cast<uint32_t>(result.Indices.size());
			target.BaseVertex = static_cast<uint32_t>(result.Positions.size());
			for (uint32_t vertex = range.BaseVertex; vertex < range.BaseVertex + range.VertexCount; ++vertex)
			{
				AppendVertex(result, source, vertex);
				result.Tangents.push_back(PerpendicularTangent(source.Normals[vertex])); // unused vertices keep it
			}
			std::vector<bool> assigned(range.VertexCount, false);
			std::map<std::pair<uint32_t, std::array<uint32_t, 4>>, uint32_t> copies; // (vertex, tangent bits) -> copy
			for (uint32_t corner = 0; corner < range.IndexCount; ++corner)
			{
				const uint32_t vertex = source.Indices[range.BaseIndex + corner];
				const uint32_t local = vertex - range.BaseVertex;
				const glm::vec4& tangent = context.CornerTangents[corner];
				const uint32_t placed = target.BaseVertex + local;
				if (!assigned[local])
				{
					assigned[local] = true;
					result.Tangents[placed] = tangent;
					result.Indices.push_back(placed);
					continue;
				}
				if (SameBits(result.Tangents[placed], tangent))
				{
					result.Indices.push_back(placed);
					continue;
				}
				std::array<uint32_t, 4> bits{};
				std::memcpy(bits.data(), glm::value_ptr(tangent), sizeof(bits));
				const auto [copy, inserted] =
					copies.try_emplace({ vertex, bits }, static_cast<uint32_t>(result.Positions.size()));
				if (inserted)
				{
					AppendVertex(result, source, vertex);
					result.Tangents.push_back(tangent);
				}
				result.Indices.push_back(copy->second);
			}
			target.VertexCount = static_cast<uint32_t>(result.Positions.size()) - target.BaseVertex;
			if (result.Positions.size() > std::numeric_limits<uint32_t>::max())
			{
				mesh = std::move(source);
				return Error("splitting vertices for tangents exceeds 2^32 vertices");
			}
		}
		result.Bounds = source.Bounds;
		result.Sphere = source.Sphere;
		mesh = std::move(result);
		return {};
	}

	std::vector<uint32_t> TriangleStripToList(std::span<const uint32_t> strip)
	{
		std::vector<uint32_t> list;
		for (size_t i = 0; i + 2 < strip.size(); ++i)
		{
			const uint32_t a = strip[i];
			const uint32_t b = strip[i + 1 + (i % 2)];
			const uint32_t c = strip[i + 2 - (i % 2)];
			if (a == b || b == c || a == c)
			{
				continue;
			}
			list.insert(list.end(), { a, b, c });
		}
		return list;
	}

	std::vector<uint32_t> TriangleFanToList(std::span<const uint32_t> fan)
	{
		std::vector<uint32_t> list;
		for (size_t i = 0; i + 2 < fan.size(); ++i)
		{
			list.insert(list.end(), { fan[i + 1], fan[i + 2], fan[0] });
		}
		return list;
	}
}
