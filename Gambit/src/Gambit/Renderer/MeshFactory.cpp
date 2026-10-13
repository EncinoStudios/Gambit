#include "gbpch.h"

#include "Gambit/Renderer/MeshFactory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Renderer/MeshUtils.h"

#include <numbers>

namespace Gambit
{
	namespace
	{
		constexpr uint64_t FirstHandle = 0x101;
		constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;
		constexpr float Pi = std::numbers::pi_v<float>;

		// A point of a parametric surface: (u, v) in [0, 1]^2 with u to the right and v down as seen from outside.
		struct SurfacePoint
		{
			glm::vec3 Position;
			glm::vec3 Normal;
		};

		// Adds a (columns + 1) x (rows + 1) lattice of vertices with UV (u, v) and two counter-clockwise triangles
		// per cell, skipping the degenerate ones (cells at a pole).
		template<typename Surface>
		void AddGrid(MeshData& mesh, uint32_t columns, uint32_t rows, const Surface& surface)
		{
			const auto base = static_cast<uint32_t>(mesh.Positions.size());
			for (uint32_t row = 0; row <= rows; ++row)
			{
				for (uint32_t column = 0; column <= columns; ++column)
				{
					const float u = static_cast<float>(column) / static_cast<float>(columns);
					const float v = static_cast<float>(row) / static_cast<float>(rows);
					const SurfacePoint point = surface(u, v);
					mesh.Positions.push_back(point.Position);
					mesh.Normals.push_back(glm::normalize(point.Normal));
					mesh.UV0.push_back({ u, v });
				}
			}
			// Corners closer than this (the poles, where sin(pi) is not exactly 0) make a triangle degenerate.
			constexpr float CoincidentDistance = 1e-6f;
			const auto addTriangle = [&mesh](uint32_t a, uint32_t b, uint32_t c) {
				const glm::vec3& pa = mesh.Positions[a];
				const glm::vec3& pb = mesh.Positions[b];
				const glm::vec3& pc = mesh.Positions[c];
				if (glm::distance(pa, pb) < CoincidentDistance || glm::distance(pb, pc) < CoincidentDistance
				    || glm::distance(pa, pc) < CoincidentDistance)
				{
					return;
				}
				mesh.Indices.insert(mesh.Indices.end(), { a, b, c });
			};
			for (uint32_t row = 0; row < rows; ++row)
			{
				for (uint32_t column = 0; column < columns; ++column)
				{
					const uint32_t topLeft = base + row * (columns + 1) + column;
					const uint32_t topRight = topLeft + 1;
					const uint32_t bottomLeft = topLeft + columns + 1;
					const uint32_t bottomRight = bottomLeft + 1;
					addTriangle(topLeft, bottomLeft, bottomRight);
					addTriangle(topLeft, bottomRight, topRight);
				}
			}
		}

		// A disc cap at height `y` facing +Y (`up`) or -Y, with planar UVs.
		void AddCap(MeshData& mesh, float radius, float y, uint32_t segments, bool up)
		{
			const auto center = static_cast<uint32_t>(mesh.Positions.size());
			const glm::vec3 normal(0.0f, up ? 1.0f : -1.0f, 0.0f);
			mesh.Positions.push_back({ 0.0f, y, 0.0f });
			mesh.Normals.push_back(normal);
			mesh.UV0.push_back({ 0.5f, 0.5f });
			for (uint32_t segment = 0; segment <= segments; ++segment)
			{
				const float angle = TwoPi * static_cast<float>(segment) / static_cast<float>(segments);
				const float x = std::sin(angle);
				const float z = std::cos(angle);
				mesh.Positions.push_back({ radius * x, y, radius * z });
				mesh.Normals.push_back(normal);
				// Seen from outside, u runs along +X; v runs along +Z from above and along -Z from below.
				mesh.UV0.push_back({ 0.5f + 0.5f * x, 0.5f + (up ? 0.5f : -0.5f) * z });
			}
			for (uint32_t segment = 0; segment < segments; ++segment)
			{
				const uint32_t current = center + 1 + segment;
				if (up)
				{
					mesh.Indices.insert(mesh.Indices.end(), { center, current, current + 1 });
				}
				else
				{
					mesh.Indices.insert(mesh.Indices.end(), { center, current + 1, current });
				}
			}
		}

		// One submesh over everything, the default material, tangents and bounds.
		MeshData Finish(MeshData mesh, std::string_view name)
		{
			Submesh submesh;
			submesh.Name = std::string(name);
			submesh.IndexCount = static_cast<uint32_t>(mesh.Indices.size());
			submesh.VertexCount = static_cast<uint32_t>(mesh.Positions.size());
			mesh.Submeshes = { submesh };
			mesh.Materials = { AssetHandle() };
			const Result<void> tangents = MeshUtils::GenerateTangents(mesh);
			GB_CORE_VERIFY(tangents.HasValue(), "MeshFactory: {}", tangents.GetError().Message);
			MeshUtils::ComputeBounds(mesh);
			return mesh;
		}

		// Around Y: u = 0 faces +Z and u grows towards +X (to the right seen from outside).
		glm::vec3 AroundY(float u)
		{
			const float angle = TwoPi * u;
			return { std::sin(angle), 0.0f, std::cos(angle) };
		}
	}

	std::string_view ToString(PrimitiveMesh mesh) noexcept
	{
		switch (mesh)
		{
			case PrimitiveMesh::Cube:
				return "Cube";
			case PrimitiveMesh::Sphere:
				return "Sphere";
			case PrimitiveMesh::Plane:
				return "Plane";
			case PrimitiveMesh::Capsule:
				return "Capsule";
			case PrimitiveMesh::Cylinder:
				return "Cylinder";
			case PrimitiveMesh::Cone:
				return "Cone";
			case PrimitiveMesh::Torus:
				return "Torus";
			case PrimitiveMesh::Quad:
				return "Quad";
		}
		return "Unknown";
	}

	AssetHandle MeshFactory::GetHandle(PrimitiveMesh mesh) noexcept
	{
		return AssetHandle(FirstHandle + static_cast<uint64_t>(mesh));
	}

	std::optional<PrimitiveMesh> MeshFactory::FindPrimitive(AssetHandle handle) noexcept
	{
		for (const PrimitiveMesh mesh : AllPrimitiveMeshes)
		{
			if (GetHandle(mesh) == handle)
			{
				return mesh;
			}
		}
		return std::nullopt;
	}

	MeshData MeshFactory::Create(PrimitiveMesh mesh)
	{
		switch (mesh)
		{
			case PrimitiveMesh::Cube:
				return CreateCube();
			case PrimitiveMesh::Sphere:
				return CreateSphere();
			case PrimitiveMesh::Plane:
				return CreatePlane();
			case PrimitiveMesh::Capsule:
				return CreateCapsule();
			case PrimitiveMesh::Cylinder:
				return CreateCylinder();
			case PrimitiveMesh::Cone:
				return CreateCone();
			case PrimitiveMesh::Torus:
				return CreateTorus();
			case PrimitiveMesh::Quad:
				return CreateQuad();
		}
		GB_CORE_VERIFY(false, "unknown primitive mesh {}", static_cast<int>(mesh));
		return CreateCube();
	}

	MeshData MeshFactory::CreateCube(const glm::vec3& size)
	{
		// Each face: its normal and the directions of +u (right) and +v (down) seen from outside.
		struct Face
		{
			glm::vec3 Normal;
			glm::vec3 Right;
			glm::vec3 Down;
		};
		constexpr std::array<Face, 6> Faces = { {
			{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, -1, 0 } },
			{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, -1, 0 } },
			{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, -1, 0 } },
			{ { -1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } },
			{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },
			{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },
		} };
		const glm::vec3 half = size * 0.5f;
		MeshData mesh;
		for (const Face& face : Faces)
		{
			AddGrid(mesh, 1, 1, [&](float u, float v) {
				const glm::vec3 unit = face.Normal + face.Right * (2.0f * u - 1.0f) + face.Down * (2.0f * v - 1.0f);
				return SurfacePoint{ unit * half, face.Normal };
			});
		}
		return Finish(std::move(mesh), "Cube");
	}

	MeshData MeshFactory::CreateSphere(float radius, uint32_t segments, uint32_t rings)
	{
		GB_CORE_VERIFY(radius > 0.0f && segments >= 3 && rings >= 2, "invalid sphere parameters");
		MeshData mesh;
		AddGrid(mesh, segments, rings, [&](float u, float v) {
			const float polar = Pi * v; // 0 at the top
			const glm::vec3 normal = AroundY(u) * std::sin(polar) + glm::vec3(0.0f, std::cos(polar), 0.0f);
			return SurfacePoint{ normal * radius, normal };
		});
		return Finish(std::move(mesh), "Sphere");
	}

	MeshData MeshFactory::CreatePlane(const glm::vec2& size, uint32_t subdivisions)
	{
		GB_CORE_VERIFY(size.x > 0.0f && size.y > 0.0f && subdivisions >= 1, "invalid plane parameters");
		MeshData mesh;
		// Seen from above (+Y), +u is +X and +v is +Z.
		AddGrid(mesh, subdivisions, subdivisions, [&](float u, float v) {
			return SurfacePoint{ { (u - 0.5f) * size.x, 0.0f, (v - 0.5f) * size.y }, { 0.0f, 1.0f, 0.0f } };
		});
		return Finish(std::move(mesh), "Plane");
	}

	MeshData MeshFactory::CreateQuad(const glm::vec2& size)
	{
		GB_CORE_VERIFY(size.x > 0.0f && size.y > 0.0f, "invalid quad parameters");
		MeshData mesh;
		AddGrid(mesh, 1, 1, [&](float u, float v) {
			return SurfacePoint{ { (u - 0.5f) * size.x, (0.5f - v) * size.y, 0.0f }, { 0.0f, 0.0f, 1.0f } };
		});
		return Finish(std::move(mesh), "Quad");
	}

	MeshData MeshFactory::CreateCapsule(float radius, float height, uint32_t segments, uint32_t hemisphereRings)
	{
		GB_CORE_VERIFY(
			radius > 0.0f && height >= 2.0f * radius && segments >= 3 && hemisphereRings >= 1,
			"invalid capsule parameters");
		const float halfCylinder = 0.5f * height - radius;
		// Rows: the top hemisphere's rings, the cylinder, the bottom hemisphere's rings; v follows the arc length.
		const uint32_t rows = 2 * hemisphereRings + 1;
		const float hemisphereLength = 0.5f * Pi * radius;
		const float totalLength = 2.0f * hemisphereLength + 2.0f * halfCylinder;
		std::vector<float> rowV;
		std::vector<float> rowPolar;
		std::vector<float> rowOffset;
		for (uint32_t ring = 0; ring <= hemisphereRings; ++ring)
		{
			const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
			rowPolar.push_back(0.5f * Pi * t);
			rowOffset.push_back(halfCylinder);
			rowV.push_back(t * hemisphereLength / totalLength);
		}
		for (uint32_t ring = 0; ring <= hemisphereRings; ++ring)
		{
			const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
			rowPolar.push_back(0.5f * Pi * (1.0f + t));
			rowOffset.push_back(-halfCylinder);
			rowV.push_back((hemisphereLength + 2.0f * halfCylinder + t * hemisphereLength) / totalLength);
		}
		MeshData mesh;
		AddGrid(mesh, segments, rows, [&](float u, float v) {
			const auto row = static_cast<size_t>(std::lround(v * static_cast<float>(rows)));
			const float polar = rowPolar[row];
			const glm::vec3 normal = AroundY(u) * std::sin(polar) + glm::vec3(0.0f, std::cos(polar), 0.0f);
			return SurfacePoint{ normal * radius + glm::vec3(0.0f, rowOffset[row], 0.0f), normal };
		});
		// The lattice spaces v evenly; the capsule's v follows the arc length instead.
		for (size_t vertex = 0; vertex < mesh.UV0.size(); ++vertex)
		{
			mesh.UV0[vertex].y = rowV[vertex / (segments + 1)];
		}
		return Finish(std::move(mesh), "Capsule");
	}

	MeshData MeshFactory::CreateCylinder(float radius, float height, uint32_t segments)
	{
		GB_CORE_VERIFY(radius > 0.0f && height > 0.0f && segments >= 3, "invalid cylinder parameters");
		MeshData mesh;
		AddGrid(mesh, segments, 1, [&](float u, float v) {
			const glm::vec3 normal = AroundY(u);
			return SurfacePoint{ normal * radius + glm::vec3(0.0f, (0.5f - v) * height, 0.0f), normal };
		});
		AddCap(mesh, radius, 0.5f * height, segments, true);
		AddCap(mesh, radius, -0.5f * height, segments, false);
		return Finish(std::move(mesh), "Cylinder");
	}

	MeshData MeshFactory::CreateCone(float radius, float height, uint32_t segments)
	{
		GB_CORE_VERIFY(radius > 0.0f && height > 0.0f && segments >= 3, "invalid cone parameters");
		MeshData mesh;
		// The side's normal leans up by the slope; the tip has one vertex per segment, so the lattice's top row
		// collapses into it and its degenerate triangles are skipped.
		AddGrid(mesh, segments, 1, [&](float u, float v) {
			const glm::vec3 around = AroundY(u);
			const glm::vec3 normal = around * height + glm::vec3(0.0f, radius, 0.0f);
			return SurfacePoint{ around * (radius * v) + glm::vec3(0.0f, (0.5f - v) * height, 0.0f), normal };
		});
		AddCap(mesh, radius, -0.5f * height, segments, false);
		return Finish(std::move(mesh), "Cone");
	}

	MeshData MeshFactory::CreateTorus(float majorRadius, float minorRadius, uint32_t segments, uint32_t sides)
	{
		GB_CORE_VERIFY(
			majorRadius > minorRadius && minorRadius > 0.0f && segments >= 3 && sides >= 3, "invalid torus parameters");
		MeshData mesh;
		// v runs around the tube from its top over the outside (down, seen from outside) to the inside.
		AddGrid(mesh, segments, sides, [&](float u, float v) {
			const glm::vec3 around = AroundY(u);
			const float tube = TwoPi * v;
			const glm::vec3 normal = around * std::sin(tube) + glm::vec3(0.0f, std::cos(tube), 0.0f);
			return SurfacePoint{ around * majorRadius + normal * minorRadius, normal };
		});
		return Finish(std::move(mesh), "Torus");
	}

	void MeshFactory::RegisterBuiltinAssets(AssetManager& manager)
	{
		for (const PrimitiveMesh primitive : AllPrimitiveMeshes)
		{
			manager.RegisterBuiltinAsset(
				GetHandle(primitive), AssetType::Mesh, std::format("Meshes/{}", ToString(primitive)),
				[primitive] { return Ref<Asset>(CreateRef<MeshSource>(Create(primitive))); });
		}
	}
}
