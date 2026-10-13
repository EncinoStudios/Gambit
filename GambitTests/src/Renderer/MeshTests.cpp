#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"

#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshFactory.h"
#include "Gambit/Renderer/MeshSource.h"
#include "Gambit/Renderer/MeshUtils.h"

#include <array>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// One quad in the XY plane facing +Z: top-left, bottom-left, top-right, bottom-right, two counter-clockwise
	// triangles, glTF UVs (u right, v down).
	MeshData MakeQuad()
	{
		MeshData mesh;
		mesh.Positions = { { -1, 1, 0 }, { -1, -1, 0 }, { 1, 1, 0 }, { 1, -1, 0 } };
		mesh.Normals.assign(4, glm::vec3(0.0f, 0.0f, 1.0f));
		mesh.UV0 = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
		mesh.Indices = { 0, 1, 3, 0, 3, 2 };
		Submesh submesh;
		submesh.IndexCount = 6;
		submesh.VertexCount = 4;
		mesh.Submeshes = { submesh };
		mesh.Materials = { AssetHandle() };
		return mesh;
	}

	bool NearlyUnit(const glm::vec3& vector)
	{
		return std::abs(glm::length(vector) - 1.0f) < 1e-4f;
	}

	// The triangle corners whose tangent frame disagrees with the triangle's (planar) UV mapping: the tangent must
	// point along the direction in which U grows, and the bitangent sign * cross(normal, tangent) to the side where V
	// shrinks (up the image, glTF's V runs down). The bitangent is perpendicular to the tangent, so for skewed
	// mappings only its side is checked.
	size_t CountBadTangentFrames(const MeshData& mesh)
	{
		size_t bad = 0;
		for (size_t corner = 0; corner < mesh.Indices.size(); corner += 3)
		{
			const std::array<uint32_t, 3> vertices = { mesh.Indices[corner], mesh.Indices[corner + 1],
				                                       mesh.Indices[corner + 2] };
			const glm::vec3 edge1 = mesh.Positions[vertices[1]] - mesh.Positions[vertices[0]];
			const glm::vec3 edge2 = mesh.Positions[vertices[2]] - mesh.Positions[vertices[0]];
			const glm::vec2 delta1 = mesh.UV0[vertices[1]] - mesh.UV0[vertices[0]];
			const glm::vec2 delta2 = mesh.UV0[vertices[2]] - mesh.UV0[vertices[0]];
			const float determinant = delta1.x * delta2.y - delta2.x * delta1.y;
			const glm::vec3 alongU = glm::normalize((edge1 * delta2.y - edge2 * delta1.y) / determinant);
			const glm::vec3 alongV = glm::normalize((edge2 * delta1.x - edge1 * delta2.x) / determinant);
			for (const uint32_t vertex : vertices)
			{
				const glm::vec3 tangent(mesh.Tangents[vertex]);
				const glm::vec3 bitangent = mesh.Tangents[vertex].w * glm::cross(mesh.Normals[vertex], tangent);
				if (glm::dot(tangent, alongU) < 0.999f || glm::dot(bitangent, -alongV) <= 0.0f)
				{
					++bad;
				}
			}
		}
		return bad;
	}
}

TEST_CASE("Renderer: mesh data is validated before it becomes a mesh")
{
	MeshData mesh = MakeQuad();
	CHECK(mesh.Validate().HasValue());

	const auto failsWith = [](const MeshData& broken, std::string_view text) {
		const Result<void> valid = broken.Validate();
		REQUIRE_FALSE(valid.HasValue());
		CHECK_MESSAGE(valid.GetError().Message.find(text) != std::string::npos, valid.GetError().Message);
	};
	MeshData shortStream = mesh;
	shortStream.UV0.pop_back();
	failsWith(shortStream, "UV0 stream has 3 entries for 4 vertices");
	MeshData partialTriangle = mesh;
	partialTriangle.Indices.pop_back();
	failsWith(partialTriangle, "not whole triangles");
	MeshData outside = mesh;
	outside.Indices[2] = 4;
	failsWith(outside, "outside its vertex range");
	MeshData noMaterial = mesh;
	noMaterial.Submeshes[0].MaterialSlot = 1;
	failsWith(noMaterial, "material slot 1 of 1");
	MeshData halfSkinned = mesh;
	halfSkinned.Joints.assign(4, glm::u16vec4(0));
	failsWith(halfSkinned, "joints and weights");
	MeshData infinite = mesh;
	infinite.Positions[1].x = std::numeric_limits<float>::infinity();
	failsWith(infinite, "not finite");
	failsWith(MeshData{}, "needs vertices");

	// A mesh's dependencies are the materials of its slots.
	mesh.Materials = { UUID(9) };
	MeshUtils::ComputeBounds(mesh);
	const MeshSource source(mesh);
	CHECK(source.GetDependencies() == std::vector<AssetHandle>{ UUID(9) });
	CHECK(source.GetBounds().Min == glm::vec3(-1.0f, -1.0f, 0.0f));
	CHECK(source.GetBounds().Max == glm::vec3(1.0f, 1.0f, 0.0f));
	CHECK(source.GetSubmeshes()[0].Bounds == source.GetBounds());
	CHECK(source.GetBoundingSphere().Radius == doctest::Approx(std::sqrt(2.0f)));
}

TEST_CASE("Renderer: MikkTSpace tangents follow +U with the bitangent up the image")
{
	MeshData quad = MakeQuad();
	REQUIRE(MeshUtils::GenerateTangents(quad).HasValue());
	REQUIRE(quad.Tangents.size() == quad.Positions.size());
	CHECK(quad.Positions.size() == 4); // consistent UVs split nothing
	for (const glm::vec4& tangent : quad.Tangents)
	{
		CHECK(NearlyEqual(tangent, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 1e-5f));
	}
	CHECK(CountBadTangentFrames(quad) == 0);

	// Mirrored UVs (u running to the left) flip the handedness: the tangent follows +U to the left and the sign
	// flips, so that the bitangent still points up the image.
	MeshData mirrored = MakeQuad();
	for (glm::vec2& uv : mirrored.UV0)
	{
		uv.x = 1.0f - uv.x;
	}
	REQUIRE(MeshUtils::GenerateTangents(mirrored).HasValue());
	for (const glm::vec4& tangent : mirrored.Tangents)
	{
		CHECK(NearlyEqual(tangent, glm::vec4(-1.0f, 0.0f, 0.0f, -1.0f), 1e-5f));
	}
	CHECK(CountBadTangentFrames(mirrored) == 0);

	// Two triangles sharing an edge, the second with mirrored UVs: the shared vertices need a tangent frame per side
	// and are split; the submesh's vertex range follows.
	MeshData seam;
	seam.Positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } };
	seam.Normals.assign(4, glm::vec3(0.0f, 0.0f, 1.0f));
	seam.UV0 = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { -1, 1 } };
	seam.Indices = { 0, 1, 2, 1, 3, 2 };
	Submesh submesh;
	submesh.IndexCount = 6;
	submesh.VertexCount = 4;
	seam.Submeshes = { submesh };
	seam.Materials = { AssetHandle() };
	REQUIRE(MeshUtils::GenerateTangents(seam).HasValue());
	CHECK(seam.Validate().HasValue());
	CHECK(seam.Positions.size() == 6);
	CHECK(seam.Submeshes[0].VertexCount == 6);
	CHECK(seam.Tangents[seam.Indices[0]].w == -seam.Tangents[seam.Indices[3]].w);
	CHECK(CountBadTangentFrames(seam) == 0);

	// Without UVs every tangent is some unit vector perpendicular to the normal.
	MeshData plain = MakeQuad();
	plain.UV0.clear();
	REQUIRE(MeshUtils::GenerateTangents(plain).HasValue());
	for (size_t vertex = 0; vertex < plain.Positions.size(); ++vertex)
	{
		CHECK(NearlyUnit(glm::vec3(plain.Tangents[vertex])));
		CHECK(std::abs(glm::dot(glm::vec3(plain.Tangents[vertex]), plain.Normals[vertex])) < 1e-5f);
	}

	MeshData noNormals = MakeQuad();
	noNormals.Normals.clear();
	CHECK_FALSE(MeshUtils::GenerateTangents(noNormals).HasValue());
}

TEST_CASE("Renderer: the cube's MikkTSpace tangents run along each face's U direction")
{
	const MeshData cube = MeshFactory::CreateCube();
	REQUIRE(cube.Positions.size() == 24);
	CHECK(CountBadTangentFrames(cube) == 0);
	for (size_t face = 0; face < 6; ++face)
	{
		// Vertices 0 and 1 of a face are its top edge from left to right as seen from outside.
		const glm::vec3 alongU = glm::normalize(cube.Positions[face * 4 + 1] - cube.Positions[face * 4]);
		for (size_t corner = 0; corner < 4; ++corner)
		{
			CHECK(NearlyEqual(cube.Tangents[face * 4 + corner], glm::vec4(alongU, 1.0f), 1e-5f));
		}
	}
}

TEST_CASE("Renderer: flat normals give every triangle corner the face normal")
{
	MeshData mesh;
	mesh.Positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	mesh.Indices = { 0, 2, 1, 0, 1, 3, 1, 1, 1 }; // the last triangle is degenerate
	Submesh submesh;
	submesh.IndexCount = 9;
	submesh.VertexCount = 4;
	mesh.Submeshes = { submesh };
	mesh.Materials = { AssetHandle() };
	MeshUtils::GenerateFlatNormals(mesh);
	REQUIRE(mesh.Validate().HasValue());
	CHECK(mesh.Positions.size() == 9);
	CHECK(mesh.Submeshes[0].VertexCount == 9);
	for (size_t corner = 0; corner < 3; ++corner)
	{
		CHECK(mesh.Normals[corner] == glm::vec3(0.0f, 0.0f, -1.0f));
		CHECK(mesh.Normals[3 + corner] == glm::vec3(0.0f, -1.0f, 0.0f));
		CHECK(mesh.Normals[6 + corner] == glm::vec3(0.0f, 1.0f, 0.0f));
	}
}

TEST_CASE("Renderer: strips and fans become triangle lists with consistent winding")
{
	// glTF 2.0 section 3.7.2.1: strip triangle i is (i, i + 1 + i % 2, i + 2 - i % 2), fan triangle i is
	// (i + 1, i + 2, 0); degenerate strip triangles (restarts) are dropped.
	const std::array<uint32_t, 6> strip = { 0, 1, 2, 3, 3, 4 };
	CHECK(MeshUtils::TriangleStripToList(strip) == std::vector<uint32_t>{ 0, 1, 2, 1, 3, 2 });
	const std::array<uint32_t, 5> fan = { 0, 1, 2, 3, 4 };
	CHECK(MeshUtils::TriangleFanToList(fan) == std::vector<uint32_t>{ 1, 2, 0, 2, 3, 0, 3, 4, 0 });
	CHECK(MeshUtils::TriangleStripToList(std::span<const uint32_t>(strip).first(2)).empty());
}

TEST_CASE("Renderer: every MeshFactory primitive is a closed-form mesh with consistent attributes")
{
	for (const PrimitiveMesh primitive : AllPrimitiveMeshes)
	{
		CAPTURE(ToString(primitive));
		const MeshData mesh = MeshFactory::Create(primitive);
		REQUIRE(mesh.Validate().HasValue());
		REQUIRE(mesh.Submeshes.size() == 1);
		CHECK(mesh.Submeshes[0].VertexCount == mesh.Positions.size());
		CHECK(mesh.Submeshes[0].IndexCount == mesh.Indices.size());
		CHECK(mesh.Materials == std::vector<AssetHandle>{ AssetHandle() });
		REQUIRE(mesh.Normals.size() == mesh.Positions.size());
		REQUIRE(mesh.Tangents.size() == mesh.Positions.size());
		REQUIRE(mesh.UV0.size() == mesh.Positions.size());

		// Within the unit cube around the origin (the capsule is twice as tall), with exact bounds.
		const glm::vec3 limit = primitive == PrimitiveMesh::Capsule ? glm::vec3(0.5f, 1.0f, 0.5f) : glm::vec3(0.5f);
		const AABB expected = AABB::FromPoints(mesh.Positions);
		CHECK(mesh.Bounds == expected);
		CHECK(glm::all(glm::lessThanEqual(glm::abs(mesh.Bounds.Min), limit + 1e-5f)));
		CHECK(glm::all(glm::lessThanEqual(glm::abs(mesh.Bounds.Max), limit + 1e-5f)));
		for (const glm::vec3& position : mesh.Positions)
		{
			CHECK(glm::distance(position, mesh.Sphere.Center) <= mesh.Sphere.Radius + 1e-5f);
		}

		bool attributesValid = true;
		for (size_t vertex = 0; vertex < mesh.Positions.size(); ++vertex)
		{
			const glm::vec3 tangent(mesh.Tangents[vertex]);
			attributesValid = attributesValid && NearlyUnit(mesh.Normals[vertex]) && NearlyUnit(tangent)
				&& std::abs(glm::dot(tangent, mesh.Normals[vertex])) < 1e-3f
				&& std::abs(mesh.Tangents[vertex].w) == 1.0f && mesh.UV0[vertex].x >= 0.0f && mesh.UV0[vertex].x <= 1.0f
				&& mesh.UV0[vertex].y >= 0.0f && mesh.UV0[vertex].y <= 1.0f;
		}
		CHECK(attributesValid);

		// Counter-clockwise seen from outside: each triangle's face normal agrees with its vertex normals, and no
		// triangle is degenerate.
		size_t inverted = 0;
		size_t degenerate = 0;
		for (size_t corner = 0; corner < mesh.Indices.size(); corner += 3)
		{
			const glm::vec3& a = mesh.Positions[mesh.Indices[corner]];
			const glm::vec3& b = mesh.Positions[mesh.Indices[corner + 1]];
			const glm::vec3& c = mesh.Positions[mesh.Indices[corner + 2]];
			const glm::vec3 face = glm::cross(b - a, c - a);
			if (glm::length(face) < 1e-9f)
			{
				++degenerate;
				continue;
			}
			const glm::vec3 vertexNormals = mesh.Normals[mesh.Indices[corner]] + mesh.Normals[mesh.Indices[corner + 1]]
				+ mesh.Normals[mesh.Indices[corner + 2]];
			if (glm::dot(face, vertexNormals) <= 0.0f)
			{
				++inverted;
			}
		}
		CHECK(inverted == 0);
		CHECK(degenerate == 0);
	}

	// Closed shapes: the cube's faces at +-0.5, the sphere's vertices on its radius.
	const MeshData sphere = MeshFactory::CreateSphere(2.0f, 12, 6);
	for (const glm::vec3& position : sphere.Positions)
	{
		CHECK(glm::length(position) == doctest::Approx(2.0f));
	}
	const MeshData plane = MeshFactory::CreatePlane(glm::vec2(4.0f, 2.0f), 3);
	CHECK(plane.Positions.size() == 16);
	CHECK(plane.Indices.size() == 3 * 3 * 6);
	CHECK(plane.Bounds.Min == glm::vec3(-2.0f, 0.0f, -1.0f));
	CHECK(plane.Bounds.Max == glm::vec3(2.0f, 0.0f, 1.0f));
}

TEST_CASE("Renderer: the primitive meshes and the default material are built-in assets")
{
	RendererProject project;
	CHECK(project.Assets.ListBuiltinAssets(AssetType::Mesh).size() == AllPrimitiveMeshes.size());
	for (const PrimitiveMesh primitive : AllPrimitiveMeshes)
	{
		CAPTURE(ToString(primitive));
		const AssetHandle handle = MeshFactory::GetHandle(primitive);
		CHECK(handle.GetValue() == 0x101 + static_cast<uint64_t>(primitive));
		CHECK(MeshFactory::FindPrimitive(handle) == primitive);
		CHECK(project.Assets.GetPath(handle) == std::format("builtin:Meshes/{}", ToString(primitive)));
		const Ref<MeshSource> mesh = project.Assets.GetAsset<MeshSource>(handle);
		REQUIRE(mesh != nullptr);
		CHECK(mesh->GetHandle() == handle);
		CHECK(mesh->GetVertexCount() == MeshFactory::Create(primitive).Positions.size());
	}
	CHECK_FALSE(MeshFactory::FindPrimitive(UUID(0x100)).has_value());

	const Ref<Material> material = project.Assets.GetAsset<Material>(Material::DefaultHandle);
	REQUIRE(material != nullptr);
	CHECK(material->GetData() == MaterialData{});
	CHECK(project.Assets.GetPath(Material::DefaultHandle) == "builtin:Materials/Default");

	// A scan keeps loaded built-in assets: they have no file that could have changed.
	project.Assets.Scan();
	CHECK(project.Assets.GetAsset<Material>(Material::DefaultHandle) == material);
}
