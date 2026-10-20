#pragma once

#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/Base.h"
#include "Gambit/Renderer/MeshSource.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace Gambit
{
	class AssetManager;

	// The engine's built-in meshes.
	enum class PrimitiveMesh : uint8_t
	{
		Cube,
		Sphere,
		Plane,
		Capsule,
		Cylinder,
		Cone,
		Torus,
		Quad
	};

	inline constexpr std::array<PrimitiveMesh, 8> AllPrimitiveMeshes = {
		PrimitiveMesh::Cube,     PrimitiveMesh::Sphere, PrimitiveMesh::Plane, PrimitiveMesh::Capsule,
		PrimitiveMesh::Cylinder, PrimitiveMesh::Cone,   PrimitiveMesh::Torus, PrimitiveMesh::Quad,
	};

	[[nodiscard]] std::string_view ToString(PrimitiveMesh mesh) noexcept;

	// Procedural meshes (D-039). Every primitive fits the unit cube around the origin (the capsule is 1 wide and 2
	// tall), Y is up, triangles are counter-clockwise seen from outside, UVs follow glTF (origin at the top left), and
	// normals, MikkTSpace tangents and bounds are filled in. Each has one submesh with one material slot (the default
	// material). The built-in assets use fixed handles, so scenes can refer to them like to any other mesh.
	class MeshFactory
	{
	public:
		// 0x0000000000000101 for the cube, then one per primitive in enum order.
		[[nodiscard]] static AssetHandle GetHandle(PrimitiveMesh mesh) noexcept;
		[[nodiscard]] static std::optional<PrimitiveMesh> FindPrimitive(AssetHandle handle) noexcept;

		// The primitive with its default detail.
		[[nodiscard]] static MeshData Create(PrimitiveMesh mesh);

		// A box of `size` with 24 vertices (4 per face, each face's UVs cover 0..1).
		[[nodiscard]] static MeshData CreateCube(const glm::vec3& size = glm::vec3(1.0f));
		// A UV sphere: `segments` around Y (>= 3), `rings` from pole to pole (>= 2).
		[[nodiscard]] static MeshData CreateSphere(float radius = 0.5f, uint32_t segments = 32, uint32_t rings = 16);
		// A plane in XZ facing +Y, subdivided `subdivisions` times along each side.
		[[nodiscard]] static MeshData CreatePlane(const glm::vec2& size = glm::vec2(1.0f), uint32_t subdivisions = 1);
		// A quad in XY facing +Z.
		[[nodiscard]] static MeshData CreateQuad(const glm::vec2& size = glm::vec2(1.0f));
		// A capsule along Y: a cylinder of `height` - 2 * radius between two hemispheres; `height` >= 2 * radius.
		[[nodiscard]] static MeshData
		CreateCapsule(float radius = 0.5f, float height = 2.0f, uint32_t segments = 32, uint32_t hemisphereRings = 8);
		// A capped cylinder along Y.
		[[nodiscard]] static MeshData CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
		// A cone along Y with its base at -height / 2 and its tip at +height / 2.
		[[nodiscard]] static MeshData CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
		// A torus around Y: the tube of `minorRadius` follows a circle of `majorRadius` in XZ.
		[[nodiscard]] static MeshData CreateTorus(
			float majorRadius = 0.375f, float minorRadius = 0.125f, uint32_t segments = 48, uint32_t sides = 16);

		// Registers every primitive as a built-in asset of `manager` (RegisterEngineAssets calls it).
		static void RegisterBuiltinAssets(AssetManager& manager);
	};
}
