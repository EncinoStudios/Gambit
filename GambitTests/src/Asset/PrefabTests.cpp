#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Asset/Components/AssetComponents.h"
#include "Gambit/Asset/Prefab.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::vector<std::string> Names(const std::vector<Entity>& entities)
	{
		std::vector<std::string> names;
		for (const Entity& entity : entities)
		{
			names.push_back(entity.GetName());
		}
		return names;
	}

	// A "Turret" with a barrel and a sensor; the sensor targets the barrel and the barrel has a texture and also refers
	// to an unrelated entity of the scene ("Player").
	struct TurretScene
	{
		TestWorld Fixture;
		Entity Turret, Barrel, Sensor, Player;

		TurretScene()
		{
			World& world = *Fixture.Scene;
			Player = world.CreateEntityWithUUID(UUID(900), "Player");
			Turret = world.CreateEntityWithUUID(UUID(100), "Turret");
			Barrel = world.CreateEntityWithUUID(UUID(101), "Barrel");
			Sensor = world.CreateEntityWithUUID(UUID(102), "Sensor");
			REQUIRE(world.SetParent(Barrel, Turret).HasValue());
			REQUIRE(world.SetParent(Sensor, Turret).HasValue());
			Turret.AddComponent<TransformComponent>().Translation = { 5.0f, 0.0f, 0.0f };
			Turret.AddComponent<HealthComponent>(40.0f, 40.0f);
			auto& barrel = Barrel.AddComponent<AllTypesComponent>();
			barrel.Target = UUID(900);      // the player: outside the prefab
			barrel.Targets = { UUID(102) }; // the sensor: inside
			barrel.Texture = UUID(0x55);
			auto& sensor = Sensor.AddComponent<StructsComponent>();
			sensor.Main.Target = UUID(101); // the barrel, inside a struct
			sensor.Layers.resize(1);
			sensor.Layers[0].Target = UUID(100); // the turret, inside an array of structs
			sensor.Source = UUID(101);           // a plain identifier: not a reference
		}
	};
}

TEST_CASE("Asset: an entity subtree becomes a prefab that refers to nothing outside itself")
{
	ScopedLogCapture log;
	TurretScene scene;
	const Result<Ref<Prefab>> created = PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret);
	REQUIRE(created.HasValue());
	const Prefab& prefab = **created;
	CHECK(prefab.GetName() == "Turret");
	CHECK(prefab.GetType() == AssetType::Prefab);
	CHECK(prefab.GetRootId() == UUID(100));
	REQUIRE(prefab.GetEntities().size() == 3);
	CHECK(prefab.GetEntities()[0].Id == UUID(100));
	CHECK_FALSE(prefab.GetEntities()[0].Parent.IsValid());
	CHECK(prefab.GetEntities()[1].Parent == UUID(100));
	CHECK(prefab.GetDependencies() == std::vector<AssetHandle>{ UUID(2), UUID(0x55) }); // Textures default and Texture
	CHECK(log.Contains(LogLevel::Warn, "reference(s) to entities outside the prefab were cleared"));

	// The world is untouched, the prefab carries no reference to the player.
	CHECK(scene.Barrel.GetComponent<AllTypesComponent>().Target == UUID(900));
	const Result<std::string> text = PrefabUtils::Serialize(scene.Fixture.Registry, prefab);
	REQUIRE(text.HasValue());
	CHECK(text->find("0x0000000000000384") == std::string::npos); // 900

	CHECK((*PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret, "Gun"))->GetName() == "Gun");
	TestWorld other;
	CHECK_FALSE(PrefabUtils::CreateFromEntity(*other.Scene, scene.Turret).HasValue()); // another world's entity
}

TEST_CASE("Asset: instantiating a prefab copies the subtree with new UUIDs and remapped references")
{
	ScopedLogCapture log;
	TurretScene scene;
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret);

	TestWorld target;
	World& world = *target.Scene;
	world.SetSeed(2024);
	Entity holder = world.CreateEntityWithUUID(UUID(1), "Holder");
	const Result<Entity> instance = PrefabUtils::Instantiate(world, *prefab, holder);
	REQUIRE(instance.HasValue());
	CHECK(instance->GetName() == "Turret");
	CHECK(world.GetParent(*instance) == holder);
	CHECK(world.GetEntityCount() == 4);
	const std::vector<Entity> children = world.GetChildren(*instance);
	CHECK(Names(children) == std::vector<std::string>{ "Barrel", "Sensor" });

	// Every UUID is new.
	for (const Entity entity : { *instance, children[0], children[1] })
	{
		CHECK(entity.GetUUID() != UUID(100));
		CHECK(entity.GetUUID() != UUID(101));
		CHECK(entity.GetUUID() != UUID(102));
		CHECK(entity.GetUUID().IsValid());
	}
	CHECK(instance->GetComponent<HealthComponent>().Current == 40.0f);
	CHECK(instance->GetComponent<TransformComponent>().Translation == glm::vec3(5.0f, 0.0f, 0.0f));

	// References between the prefab's entities follow the copies; the ones to the player are gone; assets stay.
	const auto& barrel = children[0].GetComponent<AllTypesComponent>();
	CHECK(barrel.Target == UUID());
	CHECK(barrel.Targets == std::vector<UUID>{ children[1].GetUUID() });
	CHECK(barrel.Texture == UUID(0x55));
	const auto& sensor = children[1].GetComponent<StructsComponent>();
	CHECK(sensor.Main.Target == children[0].GetUUID());
	CHECK(sensor.Layers[0].Target == instance->GetUUID());
	CHECK(sensor.Source == UUID(101)); // an identifier, not a reference

	// Every entity knows where it came from.
	CHECK(instance->GetComponent<PrefabInstanceComponent>().SourceEntity == UUID(100));
	CHECK(children[0].GetComponent<PrefabInstanceComponent>().SourceEntity == UUID(101));
	CHECK(children[1].GetComponent<PrefabInstanceComponent>().SourceEntity == UUID(102));

	// Instantiating again gives yet other entities.
	const Result<Entity> second = PrefabUtils::Instantiate(world, *prefab);
	REQUIRE(second.HasValue());
	CHECK(second->GetUUID() != instance->GetUUID());
	CHECK_FALSE(world.GetParent(*second).IsValid());
	CHECK(world.GetEntityCount() == 7);
}

TEST_CASE("Asset: instances are deterministic when their UUIDs come from the world generator")
{
	ScopedLogCapture log;
	TurretScene scene;
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret);

	TestWorld first;
	TestWorld second;
	first.Scene->SetSeed(77);
	second.Scene->SetSeed(77);
	for (int index = 0; index < 5; ++index)
	{
		REQUIRE(PrefabUtils::Instantiate(*first.Scene, *prefab).HasValue());
		REQUIRE(PrefabUtils::Instantiate(*second.Scene, *prefab).HasValue());
		CHECK(first.Scene->ComputeStateHash() == second.Scene->ComputeStateHash());
	}
	TestWorld other;
	other.Scene->SetSeed(78);
	REQUIRE(PrefabUtils::Instantiate(*other.Scene, *prefab).HasValue());
	CHECK(other.Scene->GetRoots()[0].GetUUID() != first.Scene->GetRoots()[0].GetUUID());

	// Tools may use random UUIDs.
	PrefabUtils::InstantiateOptions random;
	random.Ids = UUIDMode::Random;
	TestWorld tool;
	const Result<Entity> instance = PrefabUtils::Instantiate(*tool.Scene, *prefab, {}, std::nullopt, random);
	REQUIRE(instance.HasValue());
	CHECK(tool.Scene->GetEntityCount() == 3);
}

TEST_CASE("Asset: the root of an instance can be placed with a transform")
{
	ScopedLogCapture log;
	TurretScene scene;
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret);
	TestWorld target;

	TransformComponent placement;
	placement.Translation = { 1.0f, 2.0f, 3.0f };
	placement.Rotation = glm::quat(0.0f, 0.0f, 1.0f, 0.0f);
	placement.Scale = glm::vec3(2.0f);
	const Result<Entity> placed = PrefabUtils::Instantiate(*target.Scene, *prefab, {}, placement);
	REQUIRE(placed.HasValue());
	const auto& transform = placed->GetComponent<TransformComponent>();
	CHECK(transform.Translation == placement.Translation);
	CHECK(transform.Rotation == placement.Rotation);
	CHECK(transform.Scale == placement.Scale);
	// The children keep what the prefab says.
	CHECK_FALSE(target.Scene->GetChildren(*placed)[0].HasComponent<TransformComponent>());

	// A prefab whose root has no transform gets one.
	Entity bare = scene.Fixture.Scene->TryGetEntity(UUID(102));
	const Ref<Prefab> sensorPrefab = *PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, bare);
	const Result<Entity> sensor = PrefabUtils::Instantiate(*target.Scene, *sensorPrefab, {}, placement);
	REQUIRE(sensor.HasValue());
	CHECK(sensor->GetComponent<TransformComponent>().Scale == glm::vec3(2.0f));
}

TEST_CASE("Asset: instances remember their prefab asset and instances of instances start over")
{
	ScopedLogCapture log;
	TempDirectory directory;
	TurretScene scene;
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(*scene.Fixture.Scene, scene.Turret);
	REQUIRE(PrefabUtils::Save(scene.Fixture.Registry, *prefab, directory.GetVFS(), "Assets/Turret.gprefab").HasValue());
	AssetManager assets(directory.GetVFSRef(), { "Assets" }, scene.Fixture.Registry);
	assets.Scan();
	const AssetHandle handle = assets.GetHandle("Assets/Turret.gprefab");
	const Ref<Prefab> loaded = assets.GetAsset<Prefab>(handle);
	REQUIRE(loaded != nullptr);

	TestWorld target;
	World& world = *target.Scene;
	const Entity instance = *PrefabUtils::Instantiate(world, *loaded);
	CHECK(instance.GetComponent<PrefabInstanceComponent>().Prefab == handle);

	// A prefab made from an instance does not remember the old prefab, and its own instances point to it only.
	const Ref<Prefab> derived = *PrefabUtils::CreateFromEntity(world, instance, "Derived");
	for (const EntityData& entity : derived->GetEntities())
	{
		CHECK(std::ranges::none_of(
			entity.Components, [](const ComponentData& component) { return component.Name == "PrefabInstance"; }));
	}
	const Entity again = *PrefabUtils::Instantiate(world, *derived);
	CHECK(again.GetComponent<PrefabInstanceComponent>().Prefab == derived->GetHandle());
	CHECK(again.GetComponent<PrefabInstanceComponent>().SourceEntity == instance.GetUUID());
}

TEST_CASE("Asset: instantiating a prefab fails without creating anything when the world cannot take it")
{
	ScopedLogCapture log;
	TestWorld source;
	const ComponentInfo& script = *source.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	Entity entity = source.Scene->CreateEntityWithUUID(UUID(1), "Scripted");
	REQUIRE(script.Add(entity) != nullptr);
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(*source.Scene, entity);

	TestWorld without; // no ScriptStats component in its registry
	const Result<Entity> result = PrefabUtils::Instantiate(*without.Scene, *prefab);
	REQUIRE_FALSE(result.HasValue());
	CHECK(result.GetError().Message.find("cannot instantiate prefab 'Scripted'") != std::string::npos);
	CHECK(result.GetError().Message.find("unknown component 'ScriptStats'") != std::string::npos);
	CHECK(without.Scene->GetEntityCount() == 0);
}

TEST_CASE("Asset: the prefab file format")
{
	ScopedLogCapture log;
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity root = world.CreateEntityWithUUID(UUID(1), "Crate");
	Entity lid = world.CreateEntityWithUUID(UUID(2), "Lid");
	REQUIRE(world.SetParent(lid, root).HasValue());
	root.AddComponent<HealthComponent>(5.0f, 5.0f);
	const Ref<Prefab> prefab = *PrefabUtils::CreateFromEntity(world, root);

	const std::string text = *PrefabUtils::Serialize(fixture.Registry, *prefab);
	CHECK(text == R"({
  "Version": 1,
  "Name": "Crate",
  "Entities": [
    {
      "UUID": "0x0000000000000001",
      "Components": {
        "Health": {
          "Current": 5.0,
          "Maximum": 5.0
        },
        "Tag": {
          "Name": "Crate"
        }
      }
    },
    {
      "UUID": "0x0000000000000002",
      "Parent": "0x0000000000000001",
      "Components": {
        "Tag": {
          "Name": "Lid"
        }
      }
    }
  ]
}
)");

	// Reading it back gives an equal prefab, and writing that gives the same bytes.
	LoadReport report;
	const Result<Ref<Prefab>> parsed = PrefabUtils::Parse(fixture.Registry, text, "Crate.gprefab", report);
	REQUIRE(parsed.HasValue());
	CHECK(report.IsEmpty());
	CHECK((*parsed)->GetName() == "Crate");
	CHECK((*parsed)->GetEntities().size() == 2);
	CHECK(*PrefabUtils::Serialize(fixture.Registry, **parsed) == text);

	TempDirectory directory;
	REQUIRE(PrefabUtils::Save(fixture.Registry, *prefab, directory.GetVFS(), "Assets/Crate.gprefab").HasValue());
	CHECK(directory.ReadFile("Assets/Crate.gprefab") == text);
	const Result<Ref<Prefab>> loaded =
		PrefabUtils::Load(fixture.Registry, directory.GetVFS(), "Assets/Crate.gprefab", report);
	REQUIRE(loaded.HasValue());
	CHECK_FALSE(PrefabUtils::Load(fixture.Registry, directory.GetVFS(), "Assets/Nope.gprefab", report).HasValue());
}

TEST_CASE("Asset: malformed prefab files are refused")
{
	TestWorld fixture;
	const auto parseError = [&](const char* text) {
		LoadReport report;
		const Result<Ref<Prefab>> result = PrefabUtils::Parse(fixture.Registry, text, "bad.gprefab", report);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(parseError("[").find("bad.gprefab: parse error") == 0);
	CHECK(parseError("{}").find("no \"Version\"") != std::string::npos);
	CHECK(parseError(R"({"Version": 3})").find("newer Gambit") != std::string::npos);
	CHECK(parseError(R"({"Version": 1})").find("no \"Entities\"") != std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": 4, "Entities": []})").find("/Name: expected a string")
		!= std::string::npos);
	CHECK(parseError(R"({"Version": 1, "Entities": []})").find("at least its root") != std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x2"}]})").find("exactly one root")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Entities": [{"UUID": "0x2", "Parent": "0x1"}, {"UUID": "0x1"}]})")
			.find("not the first entity")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x2", "Parent": "0x9"}]})")
			.find("not part of the data")
		!= std::string::npos);
	CHECK(
		parseError(
			R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x2", "Parent": "0x3"}, {"UUID": "0x3", "Parent": "0x2"}]})")
			.find("cycle")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x1", "Parent": "0x1"}]})").find("twice")
		!= std::string::npos);

	LoadReport report;
	REQUIRE(PrefabUtils::Parse(
				fixture.Registry, R"({"Version": 1, "Extra": 1, "Entities": [{"UUID": "0x1"}]})", "x", report)
	            .HasValue());
	CHECK(report.GetWarnings().size() == 1);
	CHECK(report.GetWarnings()[0].Path == "/Extra");

	// A hand-edited file that refers outside the prefab loads without those references, with a warning.
	LoadReport dangling;
	const Result<Ref<Prefab>> edited = PrefabUtils::Parse(
		fixture.Registry,
		R"({"Version": 1, "Entities": [{"UUID": "0x1", "Components": {"Structs": {"Main": {"Target": "0x9"},
		    "Layers": [{"Target": "0x1"}, {"Target": "0x8"}]}}}]})",
		"x", dangling);
	REQUIRE(edited.HasValue());
	REQUIRE(dangling.GetWarnings().size() == 1);
	CHECK(dangling.GetWarnings()[0].Message.find("2 reference(s)") != std::string::npos);
	const ComponentData& structs = (*edited)->GetEntities()[0].Components[0];
	const auto& main = std::get<StructValue>(structs.Fields[0].Value);
	const auto& layers = std::get<std::vector<StructValue>>(structs.Fields[1].Value);
	const auto targetOf = [&](const StructValue& layer) {
		const StructInfo& info = *fixture.Registry.FindStruct("TestLayer");
		return std::get<UUID>(layer.Values[*info.FindFieldIndex("Target")]);
	};
	CHECK(targetOf(main) == UUID());
	CHECK(targetOf(layers[0]) == UUID(1)); // inside the prefab: kept
	CHECK(targetOf(layers[1]) == UUID());
}
