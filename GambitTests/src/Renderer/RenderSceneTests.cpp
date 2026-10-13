#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Renderer/DebugDraw.h"
#include "Gambit/Renderer/MeshFactory.h"
#include "Gambit/Renderer/RenderScene.h"
#include "Gambit/Renderer/RenderView.h"
#include "Gambit/Renderer/SceneRenderer.h"
#include "Gambit/Renderer/Systems/RenderExtractionSystem.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <numbers>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	constexpr TimeStep Tick60(1.0f / 60.0f);

	// A world with the transform and render extraction systems.
	struct ExtractionWorld
	{
		EngineTypes Types;
		World Scene{ Types.Registry };
		RenderExtractionSystem* Extraction = nullptr;

		ExtractionWorld()
		{
			REQUIRE(Scene.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
			auto extraction = CreateScope<RenderExtractionSystem>();
			Extraction = extraction.get();
			REQUIRE(Scene.GetScheduler().AddSystem(std::move(extraction)).HasValue());
		}

		Entity Add(std::string_view name, const glm::vec3& translation)
		{
			Entity entity = Scene.CreateEntity(name);
			entity.AddComponent<TransformComponent>().Translation = translation;
			return entity;
		}
	};
}

TEST_CASE("Renderer: extraction copies cameras, lights and mesh renderers with two ticks of transforms")
{
	ExtractionWorld world;
	CHECK(RenderExtractionSystem::Find(world.Scene) == world.Extraction);
	Entity mover = world.Add("Mover", glm::vec3(0.0f));
	MeshRendererComponent renderer;
	renderer.Mesh = MeshFactory::GetHandle(PrimitiveMesh::Cube);
	renderer.MaterialOverrides = { UUID(0x77) };
	renderer.CastShadows = false;
	mover.AddComponent<MeshRendererComponent>(renderer);
	Entity hidden = world.Add("Hidden", glm::vec3(0.0f));
	hidden.AddComponent<MeshRendererComponent>(renderer).Visible = false;
	Entity empty = world.Add("Empty", glm::vec3(0.0f));
	empty.AddComponent<MeshRendererComponent>(); // no mesh: nothing to draw
	Entity disabled = world.Add("Disabled", glm::vec3(0.0f));
	disabled.AddComponent<MeshRendererComponent>(renderer);
	world.Scene.SetEnabled(disabled, false);
	Entity camera = world.Add("Camera", glm::vec3(0.0f, 1.0f, 5.0f));
	camera.AddComponent<CameraComponent>();
	world.Add("Sun", glm::vec3(0.0f)).AddComponent<DirectionalLightComponent>();
	world.Add("Bulb", glm::vec3(1.0f)).AddComponent<PointLightComponent>();
	world.Add("Spot", glm::vec3(2.0f)).AddComponent<SpotLightComponent>();

	world.Scene.Tick(Tick60);
	const RenderScene& scene = world.Extraction->GetRenderScene();
	CHECK(scene.Tick == 1);
	REQUIRE(scene.DrawItems.size() == 1);
	const RenderDrawItem& item = scene.DrawItems[0];
	CHECK(item.Entity == mover.GetUUID());
	CHECK(item.Mesh == renderer.Mesh);
	CHECK(item.MaterialOverrides == renderer.MaterialOverrides);
	CHECK_FALSE(item.CastShadows);
	// Seen for the first time: it has not moved yet.
	CHECK(item.Transform.Previous == item.Transform.Current);
	REQUIRE(scene.Cameras.size() == 1);
	CHECK(glm::vec3(scene.Cameras[0].Transform.Current[3]) == glm::vec3(0.0f, 1.0f, 5.0f));
	CHECK(scene.DirectionalLights.size() == 1);
	CHECK(scene.PointLights.size() == 1);
	CHECK(scene.SpotLights.size() == 1);

	// The next tick knows where it was.
	mover.GetComponent<TransformComponent>().Translation = glm::vec3(4.0f, 0.0f, 0.0f);
	world.Scene.Tick(Tick60);
	const RenderTransform& moved = scene.DrawItems[0].Transform;
	CHECK(glm::vec3(moved.Previous[3]) == glm::vec3(0.0f));
	CHECK(glm::vec3(moved.Current[3]) == glm::vec3(4.0f, 0.0f, 0.0f));
	CHECK(glm::vec3(moved.Interpolate(0.25f)[3]) == glm::vec3(1.0f, 0.0f, 0.0f));

	// Edit-mode ticks extract too (the editor shows the scene without simulating it).
	mover.GetComponent<TransformComponent>().Translation = glm::vec3(6.0f, 0.0f, 0.0f);
	world.Scene.Tick(Tick60, SimulationMode::Edit);
	CHECK(glm::vec3(scene.DrawItems[0].Transform.Current[3]) == glm::vec3(6.0f, 0.0f, 0.0f));
	CHECK(scene.Tick == 2);
}

TEST_CASE("Renderer: extracted lists are ordered by entity UUID")
{
	ExtractionWorld world;
	std::vector<UUID> created;
	for (int index = 0; index < 8; ++index)
	{
		Entity entity = world.Scene.CreateEntityWithUUID(UUID(100 - static_cast<uint64_t>(index) * 7));
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<MeshRendererComponent>().Mesh = MeshFactory::GetHandle(PrimitiveMesh::Sphere);
		entity.AddComponent<PointLightComponent>();
		entity.AddComponent<CameraComponent>().Primary = index % 3 == 0;
		created.push_back(entity.GetUUID());
	}
	world.Scene.Tick(Tick60);
	const RenderScene& scene = world.Extraction->GetRenderScene();
	std::ranges::sort(created);
	std::vector<UUID> drawn;
	for (const RenderDrawItem& item : scene.DrawItems)
	{
		drawn.push_back(item.Entity);
	}
	CHECK(drawn == created);
	CHECK(std::ranges::is_sorted(scene.PointLights, {}, &RenderPointLight::Entity));
	CHECK(std::ranges::is_sorted(scene.Cameras, {}, &RenderCamera::Entity));
	// The primary camera with the smallest UUID wins: index 6 has UUID 58, index 3 has 79, index 0 has 100.
	REQUIRE(scene.FindPrimaryCamera() != nullptr);
	CHECK(scene.FindPrimaryCamera()->Entity == UUID(58));

	RenderScene noPrimary;
	noPrimary.Cameras.push_back({ UUID(1), {}, CameraComponent{ .Primary = false } });
	CHECK(noPrimary.FindPrimaryCamera() == nullptr);
}

TEST_CASE("Renderer: transforms interpolate translation, rotation and scale between ticks")
{
	RenderTransform transform;
	transform.Previous = ComposeTransform(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
	// A rotation of 180 + 90 degrees about Y: the shorter arc is -90 degrees.
	const glm::quat end = glm::angleAxis(glm::radians(270.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	transform.Current = ComposeTransform(glm::vec3(2.0f, 4.0f, -6.0f), end, glm::vec3(3.0f));

	CHECK(NearlyEqual(transform.Interpolate(0.0f), transform.Previous));
	CHECK(transform.Interpolate(1.0f) == transform.Current);
	CHECK(transform.Interpolate(2.0f) == transform.Current); // never extrapolates
	const glm::mat4 half = transform.Interpolate(0.5f);
	glm::vec3 translation;
	glm::quat rotation;
	glm::vec3 scale;
	REQUIRE(DecomposeTransform(half, translation, rotation, scale));
	CHECK(NearlyEqual(translation, glm::vec3(1.0f, 2.0f, -3.0f)));
	CHECK(NearlyEqual(scale, glm::vec3(2.0f)));
	const glm::quat expected = glm::angleAxis(glm::radians(-45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(std::abs(glm::dot(rotation, expected)) > 1.0f - 1e-5f);

	// Matrices that are no TRS (a projection, singular) snap to the current one.
	RenderTransform singular;
	singular.Previous = glm::mat4(0.0f);
	singular.Current = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f));
	CHECK(singular.Interpolate(0.5f) == singular.Current);
}

TEST_CASE("Renderer: projections use reversed depth and +Y up")
{
	CameraComponent camera;
	camera.VerticalFOV = 90.0f;
	camera.Near = 0.5f;
	camera.Far = 50.0f;
	const glm::mat4 projection = MakeProjection(camera, 2.0f);
	const auto project = [&](const glm::vec3& point) {
		const glm::vec4 clip = projection * glm::vec4(point, 1.0f);
		return glm::vec3(clip) / clip.w;
	};
	CHECK(project(glm::vec3(0.0f, 0.0f, -0.5f)).z == doctest::Approx(1.0f));
	CHECK(project(glm::vec3(0.0f, 0.0f, -50.0f)).z == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(project(glm::vec3(0.0f, 1.0f, -1.0f)).y == doctest::Approx(1.0f)); // top edge at 45 degrees
	CHECK(project(glm::vec3(2.0f, 0.0f, -1.0f)).x == doctest::Approx(1.0f)); // aspect 2
	CHECK(project(glm::vec3(0.0f, 0.0f, -10.0f)).z > project(glm::vec3(0.0f, 0.0f, -20.0f)).z);

	camera.Projection = ProjectionType::Orthographic;
	camera.OrthoSize = 4.0f;
	const glm::mat4 ortho = MakeProjection(camera, 0.5f);
	CHECK((ortho * glm::vec4(0.0f, 2.0f, -1.0f, 1.0f)).y == doctest::Approx(1.0f));
	CHECK((ortho * glm::vec4(1.0f, 0.0f, -1.0f, 1.0f)).x == doctest::Approx(1.0f));
	CHECK((ortho * glm::vec4(0.0f, 0.0f, -0.5f, 1.0f)).z == doctest::Approx(1.0f));
	CHECK((ortho * glm::vec4(0.0f, 0.0f, -50.0f, 1.0f)).z == doctest::Approx(0.0f).epsilon(1e-6));

	// Broken values stay usable: no NaN for a zero aspect ratio or inverted planes.
	CameraComponent broken;
	broken.Near = 0.0f;
	broken.Far = -1.0f;
	const glm::mat4 sane = MakeProjection(broken, 0.0f);
	for (int column = 0; column < 4; ++column)
	{
		CHECK(glm::all(glm::equal(sane[column], sane[column]))); // no NaN
	}

	CHECK(ExposureFromEV100(0.0f) == doctest::Approx(1.0f / 1.2f));
	CHECK(ExposureFromEV100(15.0f) == doctest::Approx(1.0f / (1.2f * 32768.0f)));

	RenderCamera sceneCamera;
	sceneCamera.Transform.Previous = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f));
	sceneCamera.Transform.Current = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 20.0f));
	sceneCamera.Camera.ClearColor = glm::vec4(0.25f);
	const RenderView view = RenderView::FromCamera(sceneCamera, 0.5f, 1.5f);
	CHECK(NearlyEqual(view.GetPosition(), glm::vec3(0.0f, 0.0f, 15.0f)));
	CHECK(view.ClearColor == glm::vec4(0.25f));
	CHECK(NearlyEqual(view.Projection, MakeProjection(sceneCamera.Camera, 1.5f)));
}

TEST_CASE("Renderer: lights are packed in physical units, culled by range and capped")
{
	RenderScene scene;
	RenderDirectionalLight sun;
	sun.Light.Color = glm::vec3(1.0f, 0.5f, 0.25f);
	sun.Light.Illuminance = 1000.0f;
	// Rotated to shine straight down (-Z turned to -Y).
	sun.Transform.Current = sun.Transform.Previous =
		glm::mat4_cast(glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
	scene.DirectionalLights.push_back(sun);
	RenderPointLight bulb;
	bulb.Light.Intensity = 4.0f * std::numbers::pi_v<float> * 100.0f; // 100 candela
	bulb.Light.Range = 5.0f;
	bulb.Transform.Current = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -10.0f));
	bulb.Transform.Previous = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -20.0f));
	scene.PointLights.push_back(bulb);
	RenderPointLight farAway = bulb;
	farAway.Transform.Current = farAway.Transform.Previous =
		glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 100.0f)); // behind the camera
	scene.PointLights.push_back(farAway);
	RenderSpotLight spot;
	spot.Light.InnerConeAngle = 40.0f; // larger than the outer angle: clamped to it
	spot.Light.OuterConeAngle = 30.0f;
	spot.Light.Range = 0.0f; // unlimited: never culled
	scene.SpotLights.push_back(spot);

	const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 100.0f, 0.1f);
	const Frustum frustum = Frustum::FromViewProjection(projection);
	const PackedLights packed = SceneRenderer::PackLights(scene, 0.5f, 0.5f, &frustum);
	REQUIRE(packed.Lights.size() == 3);
	CHECK(packed.Culled == 1);
	CHECK(packed.Dropped == 0);

	// Directional lights come first: lux times exposure.
	const Gpu::Light& directional = packed.Lights[0];
	CHECK(directional.PositionType.w == static_cast<float>(Gpu::LightType::Directional));
	CHECK(NearlyEqual(glm::vec3(directional.DirectionRange), glm::vec3(0.0f, -1.0f, 0.0f), 1e-5f));
	CHECK(NearlyEqual(glm::vec3(directional.Color), glm::vec3(500.0f, 250.0f, 125.0f)));
	// Then spot lights; the cone's cosines with the inner angle clamped to the outer one.
	const Gpu::Light& spotLight = packed.Lights[1];
	CHECK(spotLight.PositionType.w == static_cast<float>(Gpu::LightType::Spot));
	CHECK(spotLight.Spot.x == doctest::Approx(std::cos(glm::radians(30.0f))));
	CHECK(spotLight.Spot.y == doctest::Approx(1.0f / 1e-4f));
	CHECK(spotLight.DirectionRange.w == 0.0f);
	// Then point lights: lumens / 4 pi = candela, at the interpolated position.
	const Gpu::Light& point = packed.Lights[2];
	CHECK(point.PositionType.w == static_cast<float>(Gpu::LightType::Point));
	CHECK(NearlyEqual(glm::vec3(point.PositionType), glm::vec3(0.0f, 0.0f, -15.0f)));
	CHECK(point.Color.r == doctest::Approx(50.0f));
	CHECK(point.DirectionRange.w == 5.0f);

	// Without a frustum nothing is culled; beyond MaxLights lights are dropped.
	RenderScene crowded;
	crowded.PointLights.assign(SceneRenderer::MaxLights + 3, bulb);
	const PackedLights capped = SceneRenderer::PackLights(crowded, 1.0f, 1.0f, nullptr);
	CHECK(capped.Lights.size() == SceneRenderer::MaxLights);
	CHECK(capped.Dropped == 3);
	CHECK(capped.Culled == 0);
}

TEST_CASE("Renderer: the debug draw list stores shapes as lines that live for their duration")
{
	DebugDrawList list;
	list.DrawLine(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(1.0f));
	CHECK(list.GetLineCount() == 1);
	list.DrawBox(
		glm::vec3(0.0f), glm::vec3(1.0f, 2.0f, 3.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec4(1.0f), 1.0f);
	CHECK(list.GetLineCount() == 13);
	list.DrawSphere(glm::vec3(0.0f), 2.0f, glm::vec4(1.0f), 0.5f, false, 16);
	CHECK(list.GetLineCount() == 13 + 48);
	for (size_t index = 13; index < list.GetLineCount(); ++index)
	{
		const DebugLine& line = list.GetLines()[index];
		CHECK(glm::length(line.Start) == doctest::Approx(2.0f));
		CHECK_FALSE(line.DepthTested);
	}
	list.DrawAABB(AABB{}, glm::vec4(1.0f)); // empty: nothing
	list.DrawAxes(glm::mat4(1.0f), 2.0f);
	CHECK(list.GetLineCount() == 13 + 48 + 3);
	CHECK(list.GetLines().back().End == glm::vec3(0.0f, 0.0f, 2.0f));
	list.DrawCircle(glm::vec3(0.0f), glm::vec3(0.0f), 1.0f, glm::vec4(1.0f)); // no normal: nothing
	list.DrawFrustum(glm::perspectiveRH_ZO(1.0f, 1.0f, 1.0f, 10.0f), glm::vec4(1.0f));
	CHECK(list.GetLineCount() == 13 + 48 + 3 + 12);

	// Lines of duration 0 last until the next Advance; others until their time is over.
	list.Advance(0.25f);
	CHECK(list.GetLineCount() == 12 + 48);
	list.Advance(0.5f);
	CHECK(list.GetLineCount() == 12);
	list.Advance(0.25f);
	CHECK(list.IsEmpty());
}

TEST_CASE("Renderer: the debug draw list drops lines beyond its cap" * doctest::test_suite("slow"))
{
	// A runaway loop cannot exhaust memory.
	DebugDrawList full;
	for (size_t index = 0; index < DebugDrawList::MaxLines + 5; ++index)
	{
		full.DrawLine(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(1.0f));
	}
	CHECK(full.GetLineCount() == DebugDrawList::MaxLines);
	CHECK(full.GetDroppedLineCount() == 5);
	full.Clear();
	CHECK(full.IsEmpty());
	CHECK(full.GetDroppedLineCount() == 0);
}

TEST_CASE("Renderer: extraction hands the debug lines to the render scene")
{
	ExtractionWorld world;
	DebugDrawList& debug = world.Extraction->GetDebugDraw();
	debug.DrawLine(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(1.0f));        // one tick
	debug.DrawLine(glm::vec3(0.0f), glm::vec3(2.0f), glm::vec4(1.0f), 0.04f); // three ticks at 60 Hz
	world.Scene.Tick(Tick60);
	CHECK(world.Extraction->GetRenderScene().DebugLines.size() == 2);
	world.Scene.Tick(Tick60);
	CHECK(world.Extraction->GetRenderScene().DebugLines.size() == 1);
	world.Scene.Tick(Tick60);
	world.Scene.Tick(Tick60);
	CHECK(world.Extraction->GetRenderScene().DebugLines.empty());

	RenderEnvironment environment;
	environment.AmbientLuminance = 3.0f;
	world.Extraction->SetEnvironment(environment);
	world.Scene.Tick(Tick60);
	CHECK(world.Extraction->GetRenderScene().Environment.AmbientLuminance == 3.0f);
}

TEST_CASE("Renderer: engine debug shapes outline lights and camera frusta")
{
	RenderScene scene;
	RenderPointLight bulb;
	bulb.Light.Range = 4.0f;
	scene.PointLights.push_back(bulb);
	scene.DirectionalLights.emplace_back();
	scene.SpotLights.emplace_back();
	RenderCamera camera;
	camera.Camera.Far = 1000.0f;
	scene.Cameras.push_back(camera);

	DebugDrawList none;
	SceneRenderer::AppendDebugShapes(scene, DebugShapes::None, 1.0f, 1.0f, none);
	CHECK(none.IsEmpty());

	DebugDrawList lights;
	SceneRenderer::AppendDebugShapes(scene, DebugShapes::Lights, 1.0f, 1.0f, lights);
	// Directional: a circle and its direction; point: a marker and the range; spot: the cone's rim and four edges.
	CHECK(lights.GetLineCount() == (32 + 1) + 2 * (3 * 32) + (32 + 4));

	DebugDrawList cameras;
	SceneRenderer::AppendDebugShapes(scene, DebugShapes::Cameras, 1.0f, 1.0f, cameras);
	REQUIRE(cameras.GetLineCount() == 12);
	float farthest = 0.0f;
	for (const DebugLine& line : cameras.GetLines())
	{
		farthest = std::max({ farthest, -line.Start.z, -line.End.z });
	}
	CHECK(farthest == doctest::Approx(100.1f)); // the drawn far plane is capped
}

TEST_CASE("Renderer: render components are reflected and round-trip through scene files")
{
	EngineTypes types;
	for (const char* name : { "MeshRenderer", "Camera", "DirectionalLight", "PointLight", "SpotLight" })
	{
		CAPTURE(name);
		CHECK(types.Registry.FindComponent(name) != nullptr);
	}
	const ComponentInfo* meshRenderer = types.Registry.FindComponent("MeshRenderer");
	const FieldInfo* overrides = meshRenderer->FindField("MaterialOverrides");
	REQUIRE(overrides != nullptr);
	CHECK(overrides->Type == FieldType::Array);
	CHECK(overrides->ElementType == FieldType::AssetHandle);
	CHECK(overrides->Attributes.AssetType == "Material");

	World world(types.Registry);
	world.SetSeed(5);
	Entity entity = world.CreateEntityWithUUID(UUID(7), "Lamp"); // the random generator stays at the seed
	MeshRendererComponent renderer;
	renderer.Mesh = MeshFactory::GetHandle(PrimitiveMesh::Torus);
	renderer.SubmeshIndex = 0;
	renderer.MaterialOverrides = { UUID(5), UUID() };
	renderer.ReceiveShadows = false;
	entity.AddComponent<MeshRendererComponent>(renderer);
	CameraComponent camera;
	camera.Projection = ProjectionType::Orthographic;
	camera.OrthoSize = 7.5f;
	entity.AddComponent<CameraComponent>(camera);
	entity.AddComponent<SpotLightComponent>().OuterConeAngle = 45.0f;

	const Result<std::string> text = SceneSerializer::Serialize(world, {});
	REQUIRE(text.HasValue());
	CHECK(text->find("\"Projection\": \"Orthographic\"") != std::string::npos);
	World loaded(types.Registry);
	LoadReport report;
	REQUIRE(SceneSerializer::Deserialize(loaded, *text, "Test.gscene", report).HasValue());
	Entity copy = loaded.FindEntityByName("Lamp");
	REQUIRE(copy);
	const MeshRendererComponent& loadedRenderer = copy.GetComponent<MeshRendererComponent>();
	CHECK(loadedRenderer.Mesh == renderer.Mesh);
	CHECK(loadedRenderer.SubmeshIndex == 0);
	CHECK(loadedRenderer.MaterialOverrides == renderer.MaterialOverrides);
	CHECK_FALSE(loadedRenderer.ReceiveShadows);
	CHECK(copy.GetComponent<CameraComponent>().OrthoSize == 7.5f);
	CHECK(copy.GetComponent<SpotLightComponent>().OuterConeAngle == 45.0f);
	CHECK(loaded.ComputeStateHash() == world.ComputeStateHash());
}
