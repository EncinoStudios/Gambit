#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"
#include "TestSupport.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Renderer/Material.h"
#include "Gambit/Renderer/MeshSource.h"
#include "Gambit/Renderer/Model.h"
#include "Gambit/Renderer/ModelUtils.h"
#include "Gambit/Renderer/Texture.h"

#include <map>
#include <numbers>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	constexpr std::string_view CubePath = "Assets/Models/TexturedCube.gltf";
	constexpr std::string_view ShapesPath = "Assets/Models/Shapes.glb";

	// Key -> handle of the sub-assets the registry knows for a file.
	std::map<std::string, AssetHandle> GetSubAssetHandles(const AssetManager& assets, AssetHandle file)
	{
		std::map<std::string, AssetHandle> handles;
		for (const AssetRecord& record : assets.GetRegistry().ListSubAssets(file))
		{
			handles.emplace(record.SubAssetKey, record.Handle);
		}
		return handles;
	}

	const ModelNode* FindNode(const Model& model, std::string_view name)
	{
		const auto found = std::ranges::find(model.GetNodes(), name, &ModelNode::Name);
		return found != model.GetNodes().end() ? &*found : nullptr;
	}

	size_t CountWarnings(const ScopedLogCapture& log, std::string_view text)
	{
		const std::vector<LogEntry> entries = log.GetEntries();
		return static_cast<size_t>(std::ranges::count_if(entries, [&](const LogEntry& entry) {
			return entry.Level == LogLevel::Warn && entry.Message.find(text) != std::string::npos;
		}));
	}
}

TEST_CASE("Renderer: a .gltf with an external buffer and images imports as a model with sub-assets")
{
	RendererProject project;
	project.CopyTexturedCube();
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle(CubePath);
	REQUIRE(project.Assets.GetType(handle) == AssetType::Model);
	const Ref<Model> model = project.Assets.GetAsset<Model>(handle);
	REQUIRE(model != nullptr);

	// The node hierarchy in pre-order, with transforms, lights and the camera.
	REQUIRE(model->GetNodes().size() == 4);
	CHECK(model->GetRoots() == std::vector<uint32_t>{ 0 });
	const std::vector<ModelNode>& nodes = model->GetNodes();
	CHECK(nodes[0].Name == "Root");
	CHECK(nodes[0].Children == std::vector<uint32_t>{ 1, 2, 3 });
	CHECK(nodes[0].Translation == glm::vec3(0.0f, 0.5f, 0.0f));
	CHECK(nodes[1].Name == "Cube");
	CHECK(nodes[1].Parent == 0);
	CHECK(NearlyEqual(nodes[1].Rotation, glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f))));
	CHECK(NearlyEqual(
		nodes[1].ModelTransform,
		nodes[0].ModelTransform * ComposeTransform(nodes[1].Translation, nodes[1].Rotation, nodes[1].Scale)));
	const ModelNode& lamp = nodes[2];
	CHECK(lamp.Name == "Lamp");
	REQUIRE(lamp.PointLight.has_value());
	CHECK(lamp.PointLight->Intensity == doctest::Approx(50.0f * 4.0f * std::numbers::pi_v<float>)); // candela to lumens
	CHECK(lamp.PointLight->Range == 10.0f);
	CHECK(lamp.PointLight->Color == glm::vec3(1.0f, 0.9f, 0.8f));
	CHECK(NearlyEqual(glm::vec3(lamp.ModelTransform[3]), glm::vec3(1.0f, 2.5f, 1.0f)));
	const ModelNode& camera = nodes[3];
	REQUIRE(camera.Camera.has_value());
	CHECK(camera.Camera->Projection == ProjectionType::Perspective);
	CHECK(camera.Camera->VerticalFOV == doctest::Approx(glm::degrees(0.8f)));
	CHECK(camera.Camera->Near == doctest::Approx(0.1f));
	CHECK(camera.Camera->Far == doctest::Approx(100.0f));
	CHECK_FALSE(camera.Camera->Primary);

	// Sub-assets: registered with keys from the glTF names, recorded in the .meta file.
	const std::map<std::string, AssetHandle> subAssets = GetSubAssetHandles(project.Assets, handle);
	CHECK(subAssets.size() == 5);
	for (const char* key :
	     { "Material/Painted", "Mesh/Cube", "Texture/Bumps.png", "Texture/Checker", "Texture/Checker (Linear)" })
	{
		CAPTURE(key);
		REQUIRE(subAssets.contains(key));
		CHECK(project.Assets.GetPath(subAssets.at(key)) == CubePath);
		CHECK(project.Assets.IsLoaded(subAssets.at(key)));
	}
	const Result<AssetMetadata> metadata = project.Assets.ReadMetadata(handle);
	REQUIRE(metadata.HasValue());
	CHECK(metadata->SubAssets.size() == 5);
	CHECK(model->GetSubAssetHandles(AssetType::Mesh) == std::vector<AssetHandle>{ subAssets.at("Mesh/Cube") });

	// The mesh: no tangents in the file, so MikkTSpace made them (no seams: no vertex was split).
	CHECK(nodes[1].Mesh == subAssets.at("Mesh/Cube"));
	const Ref<MeshSource> mesh = project.Assets.GetAsset<MeshSource>(nodes[1].Mesh);
	REQUIRE(mesh != nullptr);
	CHECK(mesh->GetVertexCount() == 24);
	CHECK(mesh->GetIndexCount() == 36);
	CHECK(mesh->GetData().Tangents.size() == 24);
	CHECK(mesh->GetMaterials() == std::vector<AssetHandle>{ subAssets.at("Material/Painted") });
	CHECK(mesh->GetBounds().Min == glm::vec3(-0.5f));
	CHECK(mesh->GetBounds().Max == glm::vec3(0.5f));

	// The material: factors, textures by color space, the texture transform as UV tiling and offset.
	const Ref<Material> material = project.Assets.GetAsset<Material>(subAssets.at("Material/Painted"));
	REQUIRE(material != nullptr);
	const MaterialData& data = material->GetData();
	CHECK(data.BaseColor == glm::vec4(1.0f, 0.9f, 0.8f, 1.0f));
	CHECK(data.BaseColorTexture == subAssets.at("Texture/Checker"));
	CHECK(data.OcclusionTexture == subAssets.at("Texture/Checker (Linear)"));
	CHECK(data.NormalTexture == subAssets.at("Texture/Bumps.png"));
	CHECK(data.MetallicRoughnessTexture == AssetHandle());
	CHECK(data.Metallic == 0.0f);
	CHECK(data.Roughness == doctest::Approx(0.6f));
	CHECK(data.NormalScale == doctest::Approx(0.8f));
	CHECK(data.OcclusionStrength == doctest::Approx(0.5f));
	CHECK(data.UVTiling == glm::vec2(2.0f));
	CHECK(data.UVOffset == glm::vec2(0.25f, 0.5f));
	CHECK(data.Alpha == AlphaMode::Opaque);

	// Textures: one image used as color and as data becomes an sRGB and a linear texture; samplers become settings.
	const Ref<Texture2D> checker = project.Assets.GetAsset<Texture2D>(subAssets.at("Texture/Checker"));
	const Ref<Texture2D> linear = project.Assets.GetAsset<Texture2D>(subAssets.at("Texture/Checker (Linear)"));
	const Ref<Texture2D> bumps = project.Assets.GetAsset<Texture2D>(subAssets.at("Texture/Bumps.png"));
	REQUIRE((checker != nullptr && linear != nullptr && bumps != nullptr));
	CHECK(checker->IsSrgb());
	CHECK_FALSE(linear->IsSrgb());
	CHECK(checker->GetImage() == linear->GetImage());
	CHECK(checker->GetWidth() == 8);
	CHECK(checker->GetMipLevelCount() == 4);
	CHECK(checker->GetSettings().Filter == TextureFilter::Linear);
	CHECK(checker->GetImage().GetPixel(0, 0) == glm::vec4(230.0f / 255.0f, 120.0f / 255.0f, 30.0f / 255.0f, 1.0f));
	CHECK(checker->GetImage().GetPixel(2, 0) == glm::vec4(40.0f / 255.0f, 80.0f / 255.0f, 200.0f / 255.0f, 1.0f));
	CHECK_FALSE(bumps->IsSrgb());
	CHECK(bumps->GetMipLevelCount() == 1);
	CHECK(bumps->GetSettings().Filter == TextureFilter::Nearest);
	CHECK(bumps->GetSettings().WrapU == TextureWrap::ClampToEdge);
	CHECK(bumps->GetSettings().WrapV == TextureWrap::ClampToEdge);

	// A sub-asset loads by handle like a file: asking for it loads the model.
	project.Assets.UnloadAsset(handle);
	CHECK_FALSE(project.Assets.IsLoaded(subAssets.at("Mesh/Cube")));
	CHECK(project.Assets.GetAsset<MeshSource>(subAssets.at("Mesh/Cube")) != nullptr);
	CHECK(project.Assets.IsLoaded(handle));
}

TEST_CASE("Renderer: glTF sub-assets keep their handles across re-imports")
{
	RendererProject project;
	project.CopyTexturedCube();
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle(CubePath);
	REQUIRE(project.Assets.GetAsset<Model>(handle) != nullptr);
	const std::map<std::string, AssetHandle> first = GetSubAssetHandles(project.Assets, handle);
	REQUIRE(first.size() == 5);

	// Reloading imports again with the handles of the .meta file.
	REQUIRE(project.Assets.ReloadAsset(handle).HasValue());
	CHECK(GetSubAssetHandles(project.Assets, handle) == first);

	// Another session: the scan registers the sub-assets from the .meta file before anything is loaded, so
	// references to them resolve right away.
	{
		AssetManager other(project.Directory.GetVFSRef(), { "Assets" }, project.Types.Registry);
		other.Scan();
		CHECK(GetSubAssetHandles(other, handle) == first);
		CHECK(other.GetType(first.at("Material/Painted")) == AssetType::Material);
		const Ref<Material> material = other.GetAsset<Material>(first.at("Material/Painted"));
		REQUIRE(material != nullptr);
		CHECK(material->GetData().BaseColorTexture == first.at("Texture/Checker"));
	}

	// The file changes: a new material and a renamed mesh. Known keys keep their handles, new keys get new ones, and
	// the keys that are gone stay registered and in the .meta file (a sub-asset that comes back keeps its handle).
	std::string text = project.Directory.ReadFile(CubePath);
	const auto replace = [&text](std::string_view from, std::string_view to) {
		const size_t at = text.find(from);
		REQUIRE(at != std::string::npos);
		text.replace(at, from.size(), to);
	};
	replace(R"("name": "Cube",)", R"("name": "Box",)");
	replace(R"("materials": [)", R"("materials": [ { "name": "Unused" },)");
	replace(R"("material": 0,)", R"("material": 1,)");
	project.Directory.WriteFile(CubePath, text);
	REQUIRE(project.Assets.ReloadAsset(handle).HasValue());
	const std::map<std::string, AssetHandle> second = GetSubAssetHandles(project.Assets, handle);
	CHECK(second.size() == 7);
	CHECK(second.at("Material/Painted") == first.at("Material/Painted"));
	CHECK(second.at("Texture/Checker") == first.at("Texture/Checker"));
	CHECK(second.at("Mesh/Cube") == first.at("Mesh/Cube"));
	REQUIRE(second.contains("Mesh/Box"));
	CHECK(second.at("Mesh/Box") != first.at("Mesh/Cube"));
	CHECK(second.contains("Material/Unused"));
	const Result<AssetMetadata> metadata = project.Assets.ReadMetadata(handle);
	REQUIRE(metadata.HasValue());
	CHECK(metadata->SubAssets.size() == 7);
	CHECK(std::ranges::find(metadata->SubAssets, "Mesh/Cube", &SubAssetEntry::Key) != metadata->SubAssets.end());

	// The old mesh is gone from the file: loading it by its handle fails with a reason.
	const ScopedLogCapture log;
	const Result<Ref<Asset>> gone = project.Assets.LoadAsset(first.at("Mesh/Cube"));
	CHECK_FALSE(gone.HasValue());

	// Renaming it back restores the original handle.
	replace(R"("name": "Box",)", R"("name": "Cube",)");
	project.Directory.WriteFile(CubePath, text);
	project.Assets.Scan();
	REQUIRE(project.Assets.ReloadAsset(handle).HasValue());
	const Ref<MeshSource> mesh = project.Assets.GetAsset<MeshSource>(first.at("Mesh/Cube"));
	REQUIRE(mesh != nullptr);
	CHECK(mesh->GetVertexCount() == 24);
}

TEST_CASE("Renderer: a binary glTF imports strips, fans, quantized data, lights and cameras")
{
	RendererProject project;
	project.CopyTestAsset("Models/Shapes.glb", ShapesPath);
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle(ShapesPath);
	const ScopedLogCapture log;
	const Ref<Model> model = project.Assets.GetAsset<Model>(handle);
	REQUIRE(model != nullptr);
	CHECK(CountWarnings(log, "only triangles are imported") == 1);
	CHECK(CountWarnings(log, "mesh 5 has no triangles") == 1);

	const std::map<std::string, AssetHandle> subAssets = GetSubAssetHandles(project.Assets, handle);
	for (const char* key : { "Mesh/Strip", "Mesh/Fan", "Mesh/Faceted", "Mesh/Quantized", "Mesh/TwoMaterials",
	                         "Material/Leaf", "Material/Glow", "Texture/Leaf" })
	{
		CAPTURE(key);
		CHECK(subAssets.contains(key));
	}
	CHECK(subAssets.size() == 8); // the points-only mesh is not imported

	const auto meshOf = [&](std::string_view key) {
		const Ref<MeshSource> mesh = project.Assets.GetAsset<MeshSource>(subAssets.at(std::string(key)));
		REQUIRE(mesh != nullptr);
		CHECK(mesh->GetData().Validate().HasValue());
		CHECK(mesh->GetData().Tangents.size() == mesh->GetVertexCount());
		return mesh;
	};

	const Ref<MeshSource> strip = meshOf("Mesh/Strip");
	CHECK(strip->GetIndexCount() == 6);
	REQUIRE(strip->GetData().Colors.size() == 4);
	CHECK(strip->GetData().Colors[0] == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

	const Ref<MeshSource> fan = meshOf("Mesh/Fan");
	CHECK(fan->GetIndexCount() == 18);
	REQUIRE(fan->GetData().Weights.size() == 7);
	CHECK(fan->GetData().Weights[3] == glm::vec4(0.5f, 0.5f, 0.0f, 0.0f)); // normalized to a sum of 1
	CHECK(fan->GetData().Joints[3] == glm::u16vec4(0, 1, 0, 0));

	const Ref<MeshSource> faceted = meshOf("Mesh/Faceted");
	CHECK(faceted->GetVertexCount() == 12); // flat normals: a vertex per corner
	const glm::vec3 center = faceted->GetBounds().GetCenter();
	for (size_t corner = 0; corner < faceted->GetIndexCount(); corner += 3)
	{
		const MeshData& data = faceted->GetData();
		const glm::vec3 normal = data.Normals[data.Indices[corner]];
		CHECK(glm::length(normal) == doctest::Approx(1.0f));
		const glm::vec3 faceCenter = (data.Positions[data.Indices[corner]] + data.Positions[data.Indices[corner + 1]]
		                              + data.Positions[data.Indices[corner + 2]])
			/ 3.0f;
		CHECK(glm::dot(normal, faceCenter - center) > 0.0f); // outwards
	}

	const Ref<MeshSource> quantized = meshOf("Mesh/Quantized");
	CHECK(quantized->GetBounds().Min == glm::vec3(-100.0f, -100.0f, 0.0f));
	CHECK(quantized->GetBounds().Max == glm::vec3(100.0f, 100.0f, 0.0f));
	CHECK(quantized->GetData().Normals[0] == glm::vec3(0.0f, 0.0f, 1.0f));
	CHECK(quantized->GetData().UV0[3] == glm::vec2(1.0f, 1.0f));

	const Ref<MeshSource> twoMaterials = meshOf("Mesh/TwoMaterials");
	REQUIRE(twoMaterials->GetSubmeshes().size() == 2);
	CHECK(
		twoMaterials->GetMaterials()
		== std::vector<AssetHandle>{ subAssets.at("Material/Leaf"), subAssets.at("Material/Glow") });
	CHECK(twoMaterials->GetSubmeshes()[1].MaterialSlot == 1);
	CHECK(twoMaterials->GetSubmeshes()[1].Bounds.Min.z == 0.5f);

	const MaterialData leaf = project.Assets.GetAsset<Material>(subAssets.at("Material/Leaf"))->GetData();
	CHECK(leaf.Alpha == AlphaMode::Mask);
	CHECK(leaf.AlphaCutoff == doctest::Approx(0.4f));
	CHECK(leaf.DoubleSided);
	CHECK(leaf.Roughness == 1.0f); // glTF's default
	CHECK(leaf.BaseColorTexture == subAssets.at("Texture/Leaf"));
	const Ref<Texture2D> leafTexture = project.Assets.GetAsset<Texture2D>(subAssets.at("Texture/Leaf"));
	REQUIRE(leafTexture != nullptr);
	CHECK(leafTexture->GetImage().GetPixel(0, 0).a == 0.0f);
	CHECK(leafTexture->GetImage().GetPixel(8, 8).a == 1.0f);
	const MaterialData glow = project.Assets.GetAsset<Material>(subAssets.at("Material/Glow"))->GetData();
	CHECK(glow.Alpha == AlphaMode::Blend);
	CHECK(glow.EmissiveColor == glm::vec3(1.0f, 0.5f, 0.0f));
	CHECK(glow.EmissiveStrength == 4.0f);
	CHECK(glow.BaseColor.a == 0.5f);
	CHECK(glow.Metallic == 0.25f);
	CHECK(glow.Roughness == 0.75f);

	// Nodes: the scene's roots in order, children after their parent; matrices become TRS.
	std::vector<std::string> names;
	for (const ModelNode& node : model->GetNodes())
	{
		names.push_back(node.Name);
	}
	CHECK(
		names
		== std::vector<std::string>{ "Strip", "Fan", "Faceted", "Quantized", "TwoMaterials", "Points", "Node #6", "Sun",
	                                 "Torch", "TopView" });
	const ModelNode* stripNode = FindNode(*model, "Strip");
	REQUIRE(stripNode != nullptr);
	CHECK(NearlyEqual(stripNode->Translation, glm::vec3(2.0f, 0.0f, 0.0f)));
	CHECK(std::abs(glm::dot(stripNode->Rotation, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)))) > 0.9999f);
	const ModelNode* fanNode = FindNode(*model, "Fan");
	REQUIRE(fanNode != nullptr);
	CHECK(NearlyEqual(glm::vec3(fanNode->ModelTransform[3]), glm::vec3(2.0f, 1.0f, 0.0f)));
	CHECK(FindNode(*model, "Quantized")->Scale == glm::vec3(0.01f));
	CHECK_FALSE(FindNode(*model, "Points")->Mesh.IsValid());

	const ModelNode* sun = FindNode(*model, "Sun");
	REQUIRE((sun != nullptr && sun->DirectionalLight.has_value()));
	CHECK(sun->DirectionalLight->Illuminance == 1000.0f);
	CHECK(sun->DirectionalLight->Color == glm::vec3(1.0f, 1.0f, 0.9f));
	// The light shines along the node's -Z axis, which the rotation turns downwards.
	CHECK(NearlyEqual(glm::vec3(sun->ModelTransform * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)), glm::vec3(0, -1, 0), 1e-5f));
	const ModelNode* torch = FindNode(*model, "Torch");
	REQUIRE((torch != nullptr && torch->SpotLight.has_value()));
	CHECK(torch->SpotLight->Intensity == doctest::Approx(10.0f * 4.0f * std::numbers::pi_v<float>));
	CHECK(torch->SpotLight->Range == 0.0f); // unlimited
	CHECK(torch->SpotLight->InnerConeAngle == doctest::Approx(glm::degrees(0.25f)));
	CHECK(torch->SpotLight->OuterConeAngle == doctest::Approx(glm::degrees(0.5f)));
	const ModelNode* topView = FindNode(*model, "TopView");
	REQUIRE((topView != nullptr && topView->Camera.has_value()));
	CHECK(topView->Camera->Projection == ProjectionType::Orthographic);
	CHECK(topView->Camera->OrthoSize == 4.0f);
	CHECK(topView->Camera->Near == 0.5f);
	CHECK(topView->Camera->Far == 50.0f);
}

TEST_CASE("Renderer: glTF data URIs import, and shared names get index keys")
{
	RendererProject project;
	project.CopyTestAsset("Models/Embedded.gltf", "Assets/Embedded.gltf");
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Assets/Embedded.gltf");
	const Ref<Model> model = project.Assets.GetAsset<Model>(handle);
	REQUIRE(model != nullptr);
	const std::map<std::string, AssetHandle> subAssets = GetSubAssetHandles(project.Assets, handle);
	CHECK(subAssets.size() == 4);
	CHECK(subAssets.contains("Mesh/#0"));
	CHECK(subAssets.contains("Mesh/#1"));
	CHECK(subAssets.contains("Texture/#0"));
	CHECK(subAssets.contains("Material/Sticker"));
	const Ref<Texture2D> texture = project.Assets.GetAsset<Texture2D>(subAssets.at("Texture/#0"));
	REQUIRE(texture != nullptr);
	CHECK(texture->GetWidth() == 8);
	CHECK(FindNode(*model, "Second")->Mesh == subAssets.at("Mesh/#1"));
}

TEST_CASE("Renderer: glTF files that cannot be imported fail with the reason")
{
	RendererProject project;
	const auto loadError = [&](std::string_view path) {
		project.Assets.Scan();
		const ScopedLogCapture log;
		const Result<Ref<Asset>> loaded = project.Assets.LoadAsset(project.Assets.GetHandle(path));
		REQUIRE_FALSE(loaded.HasValue());
		return loaded.GetError().Message;
	};

	project.Directory.WriteFile(
		"Assets/Draco.gltf",
		R"({ "asset": { "version": "2.0" }, "extensionsUsed": [ "KHR_draco_mesh_compression" ],
		     "extensionsRequired": [ "KHR_draco_mesh_compression" ] })");
	CHECK(loadError("Assets/Draco.gltf").find("KHR_draco_mesh_compression") != std::string::npos);

	project.Directory.WriteFile("Assets/Broken.gltf", "{ not json");
	CHECK(loadError("Assets/Broken.gltf").find("not a valid glTF 2.0 file") != std::string::npos);

	project.Directory.WriteFile("Assets/Old.gltf", R"({ "asset": { "version": "1.0" } })");
	CHECK(loadError("Assets/Old.gltf").find("Assets/Old.gltf") != std::string::npos);

	// The buffer is missing: the error names the file that cannot be read.
	project.CopyTestAsset("Models/TexturedCube.gltf", "Assets/Lonely/TexturedCube.gltf");
	const std::string missing = loadError("Assets/Lonely/TexturedCube.gltf");
	CHECK(missing.find("cannot load the buffers") != std::string::npos);
	CHECK(missing.find("Assets/Lonely/TexturedCube.bin") != std::string::npos);

	// Missing images are not fatal: the material loses those textures, with a warning.
	project.CopyTestAsset("Models/TexturedCube.bin", "Assets/Lonely/TexturedCube.bin");
	project.Assets.Scan();
	const ScopedLogCapture log;
	const AssetHandle handle = project.Assets.GetHandle("Assets/Lonely/TexturedCube.gltf");
	const Ref<Model> model = project.Assets.GetAsset<Model>(handle);
	REQUIRE(model != nullptr);
	CHECK(CountWarnings(log, "image 0 cannot be read") == 1);
	const std::map<std::string, AssetHandle> subAssets = GetSubAssetHandles(project.Assets, handle);
	CHECK(subAssets.size() == 2); // the mesh and the material
	const Ref<Material> material = project.Assets.GetAsset<Material>(subAssets.at("Material/Painted"));
	REQUIRE(material != nullptr);
	CHECK_FALSE(material->GetData().BaseColorTexture.IsValid());
}

TEST_CASE("Renderer: ModelUtils::Instantiate turns a model's nodes into entities")
{
	RendererProject project;
	project.CopyTexturedCube();
	project.Assets.Scan();
	const Ref<Model> model = project.Assets.GetAsset<Model>(project.Assets.GetHandle(CubePath));
	REQUIRE(model != nullptr);

	World world(project.Types.Registry);
	world.SetSeed(42);
	REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	Entity parent = world.CreateEntity("Level");
	parent.AddComponent<TransformComponent>().Translation = glm::vec3(10.0f, 0.0f, 0.0f);

	TransformComponent placement;
	placement.Translation = glm::vec3(0.0f, 0.0f, -5.0f);
	const Result<Entity> instantiated = ModelUtils::Instantiate(world, *model, parent, placement);
	REQUIRE(instantiated.HasValue());
	Entity root = *instantiated;
	CHECK(root.GetComponent<TagComponent>().Name == "TexturedCube");
	CHECK(world.GetParent(root) == parent);
	CHECK(root.GetComponent<TransformComponent>().Translation == placement.Translation);

	const std::vector<Entity> rootChildren = world.GetChildren(root);
	REQUIRE(rootChildren.size() == 1);
	Entity nodeRoot = rootChildren[0];
	CHECK(nodeRoot.GetComponent<TagComponent>().Name == "Root");
	const std::vector<Entity> children = world.GetChildren(nodeRoot);
	REQUIRE(children.size() == 3);
	CHECK(children[0].GetComponent<TagComponent>().Name == "Cube");
	REQUIRE(children[0].HasComponent<MeshRendererComponent>());
	CHECK(children[0].GetComponent<MeshRendererComponent>().Mesh == model->GetNodes()[1].Mesh);
	CHECK(children[0].GetComponent<MeshRendererComponent>().SubmeshIndex == -1);
	CHECK(children[1].HasComponent<PointLightComponent>());
	REQUIRE(children[2].HasComponent<CameraComponent>());
	CHECK_FALSE(children[2].GetComponent<CameraComponent>().Primary);

	// The world transforms are the model transforms below the placement.
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	const glm::mat4 base = world.ComputeWorldTransform(root);
	CHECK(NearlyEqual(base, glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, -5.0f))));
	CHECK(NearlyEqual(
		children[0].GetComponent<WorldTransformComponent>().Matrix, base * model->GetNodes()[1].ModelTransform));

	// UUIDs come from the world's generator: the same seed gives the same entities.
	World again(project.Types.Registry);
	again.SetSeed(42);
	again.CreateEntity("Level").AddComponent<TransformComponent>();
	const Result<Entity> repeated = ModelUtils::Instantiate(again, *model, again.FindEntityByName("Level"), placement);
	REQUIRE(repeated.HasValue());
	CHECK(world.GetEntityUUIDs() == again.GetEntityUUIDs());

	// A name and no parent: a root of its own.
	const Result<Entity> named = ModelUtils::Instantiate(world, *model, {}, std::nullopt, "Crate");
	REQUIRE(named.HasValue());
	CHECK(named->GetComponent<TagComponent>().Name == "Crate");
	CHECK_FALSE(world.GetParent(*named));

	ModelUtils::InstantiateOptions keep;
	keep.Ids = UUIDMode::Keep;
	CHECK_FALSE(ModelUtils::Instantiate(world, *model, {}, std::nullopt, {}, keep).HasValue());
	World other(project.Types.Registry);
	Entity stranger = other.CreateEntity("Stranger");
	CHECK_FALSE(ModelUtils::Instantiate(world, *model, stranger).HasValue());
}
