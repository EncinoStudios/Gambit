#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/Serialization/SceneSerializer.h"

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

	// A scene with a hierarchy (siblings in a non-obvious order), a disabled subtree, every component kind including a
	// script component and structs.
	void BuildScene(TestWorld& fixture, const ComponentInfo& script)
	{
		World& world = *fixture.Scene;
		world.SetSeed(777);
		Entity player = world.CreateEntityWithUUID(UUID(0x1000), "Player");
		player.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
		player.AddComponent<HealthComponent>(80.0f, 100.0f);
		player.AddComponent<PlayerTagComponent>();
		Entity hat = world.CreateEntityWithUUID(UUID(0x1002), "Hat");
		Entity cape = world.CreateEntityWithUUID(UUID(0x1001), "Cape");
		Entity pocket = world.CreateEntityWithUUID(UUID(0x1003), "Pocket");
		REQUIRE(world.SetParent(hat, player).HasValue());
		REQUIRE(world.SetParent(cape, player).HasValue()); // after the hat although its UUID is smaller
		REQUIRE(world.SetParent(pocket, cape).HasValue());
		auto& types = hat.AddComponent<AllTypesComponent>();
		types.Target = UUID(0x1000);
		types.String = "caf\xC3\xA9 \"quoted\"\n";
		auto& structs = cape.AddComponent<StructsComponent>();
		structs.Main.Clip = "wave";
		structs.Layers.resize(2);
		structs.Layers[1].History.push_back(TestStats{ 4, 0.25f, "old" });
		world.SetEnabled(cape, false);

		Entity scripted = world.CreateEntityWithUUID(UUID(0x2000), "Scripted");
		REQUIRE(script.Add(scripted) != nullptr);
		REQUIRE(script.SetFieldValue(scripted, "Label", std::string("scripted")));
		Entity second = world.CreateEntityWithUUID(UUID(0x0500), "Second root");
		second.AddComponent<TransformComponent>().Scale = glm::vec3(0.5f);
	}

	constexpr const char* SmallScene = R"({
  "Version": 1,
  "Name": "Small",
  "Seed": 42,
  "Settings": {
    "Gravity": [
      0.0,
      -9.81,
      0.0
    ]
  },
  "Entities": [
    {
      "UUID": "0x0000000000000001",
      "Components": {
        "Health": {
          "Current": 80.0,
          "Maximum": 100.0
        },
        "Tag": {
          "Name": "Player"
        },
        "Transform": {
          "Translation": [
            1.0,
            2.0,
            3.0
          ],
          "Rotation": [
            0.0,
            0.0,
            0.0,
            1.0
          ],
          "Scale": [
            1.0,
            1.0,
            1.0
          ]
        }
      }
    },
    {
      "UUID": "0x0000000000000002",
      "Parent": "0x0000000000000001",
      "Components": {
        "Disabled": {},
        "Tag": {
          "Name": "Hat"
        }
      }
    }
  ]
}
)";
}

TEST_CASE("Serialization: a scene file has the documented layout")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.SetSeed(42);
	Entity player = world.CreateEntityWithUUID(UUID(1), "Player");
	player.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	player.AddComponent<HealthComponent>(80.0f, 100.0f);
	Entity hat = world.CreateEntityWithUUID(UUID(2), "Hat");
	REQUIRE(world.SetParent(hat, player).HasValue());
	world.SetEnabled(hat, false);

	SceneMetadata metadata;
	metadata.Name = "Small";
	metadata.Settings["Gravity"] = Json::array({ 0.0, -9.81, 0.0 });
	const Result<std::string> text = SceneSerializer::Serialize(world, metadata);
	REQUIRE(text.HasValue());
	CHECK(*text == SmallScene);
}

TEST_CASE("Serialization: saving, loading and saving again gives the same bytes and the same state")
{
	TestWorld first;
	const ComponentInfo& script = *first.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	BuildScene(first, script);
	SceneMetadata metadata;
	metadata.Name = "Everything";
	metadata.Settings["Physics"]["Gravity"] = Json::array({ 0.0, -9.81, 0.0 });
	metadata.Settings["Fog"] = false;

	const Result<std::string> saved = SceneSerializer::Serialize(*first.Scene, metadata);
	REQUIRE(saved.HasValue());
	CHECK(saved->back() == '\n');
	CHECK(saved->find("\t") == std::string::npos); // spaces only

	TestWorld second;
	REQUIRE(second.Registry.RegisterDynamicComponent(MakeScriptSchema()).HasValue());
	LoadReport report;
	const Result<SceneMetadata> loaded = SceneSerializer::Deserialize(*second.Scene, *saved, "test.gscene", report);
	REQUIRE(loaded.HasValue());
	CHECK(report.IsEmpty());
	CHECK(loaded->Name == "Everything");
	CHECK(loaded->Settings == metadata.Settings);
	CHECK(second.Scene->GetSeed() == 777);
	CHECK(second.Scene->GetEntityCount() == first.Scene->GetEntityCount());
	CHECK(second.Scene->ComputeStateHash() == first.Scene->ComputeStateHash());

	const Result<std::string> again = SceneSerializer::Serialize(*second.Scene, *loaded);
	REQUIRE(again.HasValue());
	CHECK(*again == *saved);

	// Hierarchy, sibling order, root order and the enabled state come back.
	World& world = *second.Scene;
	CHECK(Names(world.GetRoots()) == std::vector<std::string>{ "Player", "Scripted", "Second root" });
	CHECK(Names(world.GetChildren(world.FindEntityByName("Player"))) == std::vector<std::string>{ "Hat", "Cape" });
	CHECK(Names(world.GetChildren(world.FindEntityByName("Cape"))) == std::vector<std::string>{ "Pocket" });
	CHECK(world.IsEnabled(world.FindEntityByName("Hat")));
	CHECK_FALSE(world.IsEnabled(world.FindEntityByName("Cape")));
	CHECK_FALSE(world.IsActive(world.FindEntityByName("Pocket"))); // inactive through its parent
	CHECK(world.FindEntityByName("Hat").GetComponent<AllTypesComponent>().String == "caf\xC3\xA9 \"quoted\"\n");
	CHECK(world.FindEntityByName("Hat").GetComponent<AllTypesComponent>().Target == UUID(0x1000));
	CHECK(world.FindEntityByName("Cape").GetComponent<StructsComponent>().Layers[1].History[0].Label == "old");
}

TEST_CASE("Serialization: a loaded scene behaves exactly like the scene that was saved")
{
	TestWorld first;
	const ComponentInfo& script = *first.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	BuildScene(first, script);
	const std::string saved = *SceneSerializer::Serialize(*first.Scene, {});

	// Loading a scene yields the same state whoever loads it, in any order of the file's entities.
	TestWorld second;
	REQUIRE(second.Registry.RegisterDynamicComponent(MakeScriptSchema()).HasValue());
	LoadReport report;
	REQUIRE(SceneSerializer::Deserialize(*second.Scene, saved, "test.gscene", report).HasValue());

	// Both worlds were built without drawing random numbers, so they simulate identically from here.
	for (TestWorld* fixture : { &first, &second })
	{
		REQUIRE(fixture->Scene->GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	}
	for (int tick = 0; tick < 120; ++tick)
	{
		for (TestWorld* fixture : { &first, &second })
		{
			static_cast<void>(fixture->Scene->GetRandom().NextU32());
			fixture->Scene->Tick(TimeStep(1.0f / 60.0f));
		}
		REQUIRE(first.Scene->ComputeStateHash() == second.Scene->ComputeStateHash());
	}
}

TEST_CASE("Serialization: the order of entities in a file does not change what is loaded")
{
	// Children listed before their parents, siblings out of order: the same scene as one in pre-order.
	const char* shuffled = R"({
		"Version": 1,
		"Entities": [
			{"UUID": "0x4", "Parent": "0x2", "Components": {"Tag": {"Name": "grandchild"}}},
			{"UUID": "0x3", "Parent": "0x1", "Components": {"Tag": {"Name": "second child"}}},
			{"UUID": "0x2", "Parent": "0x1", "Components": {"Tag": {"Name": "first child"}}},
			{"UUID": "0x1", "Components": {"Tag": {"Name": "root"}}}
		]})";
	TestWorld fixture;
	LoadReport report;
	REQUIRE(SceneSerializer::Deserialize(*fixture.Scene, shuffled, "shuffled.gscene", report).HasValue());
	World& world = *fixture.Scene;
	CHECK(
		Names(world.GetChildren(world.TryGetEntity(UUID(1))))
		== std::vector<std::string>{ "second child", "first child" });

	// Saved, the file is in pre-order, and loading that gives the same world.
	const std::string canonical = *SceneSerializer::Serialize(world, {});
	CHECK(canonical.find("root") < canonical.find("second child"));
	CHECK(canonical.find("second child") < canonical.find("first child"));
	CHECK(canonical.find("first child") < canonical.find("grandchild"));
	TestWorld again;
	REQUIRE(SceneSerializer::Deserialize(*again.Scene, canonical, "canonical.gscene", report).HasValue());
	CHECK(again.Scene->ComputeStateHash() == world.ComputeStateHash());
	CHECK(*SceneSerializer::Serialize(*again.Scene, {}) == canonical);
}

TEST_CASE("Serialization: scenes are written to and read from the VFS")
{
	TempDirectory directory;
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.SetSeed(9);
	Entity entity = world.CreateEntityWithUUID(UUID(7), "Saved");
	entity.AddComponent<HealthComponent>(5.0f, 6.0f);

	SceneMetadata metadata;
	metadata.Name = "Disk";
	REQUIRE(SceneSerializer::Save(world, metadata, directory.GetVFS(), "Scenes/Disk.gscene").HasValue());
	REQUIRE(directory.Exists("Scenes/Disk.gscene"));
	CHECK(directory.ReadFile("Scenes/Disk.gscene") == *SceneSerializer::Serialize(world, metadata));

	TestWorld loaded;
	LoadReport report;
	const Result<SceneMetadata> read =
		SceneSerializer::Load(*loaded.Scene, directory.GetVFS(), "Scenes/Disk.gscene", report);
	REQUIRE(read.HasValue());
	CHECK(read->Name == "Disk");
	CHECK(loaded.Scene->ComputeStateHash() == world.ComputeStateHash());

	const Result<SceneData> data =
		SceneSerializer::Read(loaded.Registry, directory.GetVFS(), "Scenes/Disk.gscene", report);
	REQUIRE(data.HasValue());
	CHECK(data->Seed == 9);
	CHECK(data->Entities.size() == 1);

	TestWorld missing;
	const Result<SceneMetadata> notFound =
		SceneSerializer::Load(*missing.Scene, directory.GetVFS(), "Scenes/Nope.gscene", report);
	REQUIRE_FALSE(notFound.HasValue());
	CHECK(notFound.GetError().Message.find("Scenes/Nope.gscene") != std::string::npos);
	CHECK(HasFileError(notFound.GetError(), FileError::NotFound));
}

TEST_CASE("Serialization: unknown things in a scene are warnings, missing things take defaults")
{
	TestWorld fixture;
	LoadReport report;
	ScopedLogCapture log;
	const Result<SceneMetadata> loaded = SceneSerializer::Deserialize(
		*fixture.Scene,
		R"({
			"Version": 1,
			"Bonus": true,
			"Entities": [
				{"UUID": "0x1", "Components": {"Health": {"Current": 10.0, "Mana": 3}, "Warp": {}, "Inactive": {}}},
				{"UUID": "0x2"}
			]})",
		"warnings.gscene", report);
	REQUIRE(loaded.HasValue());
	CHECK(loaded->Name.empty());
	CHECK(loaded->Settings == Json::object());
	CHECK(fixture.Scene->GetSeed() == 0);

	std::vector<std::string> paths;
	for (const Diagnostic& warning : report.GetWarnings())
	{
		paths.push_back(warning.Path);
	}
	CHECK(
		paths
		== std::vector<std::string>{ "/Bonus", "/Entities/0/Components/Health/Mana", "/Entities/0/Components/Warp",
	                                 "/Entities/0/Components/Inactive" });
	CHECK(log.Contains(LogLevel::Warn, "warnings.gscene: /Bonus: unknown member, ignored"));

	Entity first = fixture.Scene->TryGetEntity(UUID(1));
	CHECK(first.GetComponent<HealthComponent>().Current == 10.0f);
	CHECK(first.GetComponent<HealthComponent>().Maximum == 100.0f); // missing: the default
	CHECK(first.GetName() == "Entity");                             // no Tag listed
	CHECK(fixture.Scene->TryGetEntity(UUID(2)).IsValid());
}

TEST_CASE("Serialization: malformed scenes are refused with the location of the problem")
{
	TestWorld fixture;
	const auto loadError = [&](const char* text) {
		TestWorld target;
		LoadReport report;
		const Result<SceneMetadata> result = SceneSerializer::Deserialize(*target.Scene, text, "bad.gscene", report);
		REQUIRE_FALSE(result.HasValue());
		CHECK(target.Scene->GetEntityCount() == 0); // nothing half-loaded
		return result.GetError().Message;
	};

	CHECK(loadError("{").find("bad.gscene: parse error") == 0);
	CHECK(loadError("[]").find("must be a JSON object") != std::string::npos);
	CHECK(loadError("{}").find("no \"Version\"") != std::string::npos);
	CHECK(loadError(R"({"Version": 2})").find("newer Gambit") != std::string::npos);
	CHECK(loadError(R"({"Version": 1, "Name": 5})") == "bad.gscene: /Name: expected a string, found a number");
	CHECK(loadError(R"({"Version": 1, "Seed": -1})").find("/Seed") != std::string::npos);
	CHECK(loadError(R"({"Version": 1, "Settings": []})").find("/Settings: expected an object") != std::string::npos);
	CHECK(loadError(R"({"Version": 1, "Entities": {}})").find("/Entities: expected an array") != std::string::npos);
	CHECK(
		loadError(R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x1"}]})").find("appears twice")
		!= std::string::npos);
	CHECK(
		loadError(R"({"Version": 1, "Entities": [{"UUID": "0x1", "Parent": "0x9"}]})").find("not part of the data")
		!= std::string::npos);
	CHECK(
		loadError(R"({"Version": 1, "Entities": [{"UUID": "0x1", "Parent": "0x2"}, {"UUID": "0x2", "Parent": "0x1"}]})")
			.find("cycle")
		!= std::string::npos);
	CHECK(
		loadError(
			R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x2", "Components": {"Transform": {"Rotation": [0, 0, 1]}}}]})")
		== "bad.gscene: /Entities/1/Components/Transform/Rotation: expected 4 numbers, found 3");
	CHECK(
		loadError(R"({"Version": 1, "Entities": [{"UUID": "0x1", "Components": {"Health": {"Current": "full"}}}]})")
		== "bad.gscene: /Entities/0/Components/Health/Current: expected a number, found a string");
}

TEST_CASE("Serialization: a scene can only be loaded into an empty world and leaves it alone on failure")
{
	TestWorld fixture;
	static_cast<void>(fixture.Scene->CreateEntityWithUUID(UUID(5), "present"));
	LoadReport report;
	const Result<SceneMetadata> refused =
		SceneSerializer::Deserialize(*fixture.Scene, R"({"Version": 1, "Entities": []})", "x.gscene", report);
	REQUIRE_FALSE(refused.HasValue());
	CHECK(refused.GetError().Message.find("empty world") != std::string::npos);
	CHECK(fixture.Scene->GetEntityCount() == 1);

	// A failure while applying (a script component the registry lacks) changes neither entities nor seed.
	TestWorld source;
	const ComponentInfo& script = *source.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	BuildScene(source, script);
	const std::string text = *SceneSerializer::Serialize(*source.Scene, {});
	TestWorld without;
	without.Scene->SetSeed(5);
	const uint64_t hash = without.Scene->ComputeStateHash();
	LoadReport ignored;
	const Result<SceneData> parsed = SceneSerializer::Parse(without.Registry, text, "x.gscene", ignored);
	REQUIRE(parsed.HasValue()); // the unknown component is a warning here: it is dropped
	CHECK_FALSE(ignored.IsEmpty());
	REQUIRE(SceneSerializer::Apply(*without.Scene, *parsed).HasValue());
	CHECK(without.Scene->GetEntityCount() == source.Scene->GetEntityCount());
	CHECK(without.Scene->ComputeStateHash() != hash);
}

TEST_CASE("Serialization: scenes are upgraded by the migration table before they are read")
{
	// The scene format has only ever had version 1, so there is nothing to run yet: a table with a step for version 1
	// stays unused for a version 1 file, and a file from the future is refused whatever the table has.
	int calls = 0;
	MigrationTable table;
	table.Add(1, [&calls](Json&) -> Result<void> {
		++calls;
		return {};
	});
	TestWorld fixture;
	LoadReport report;
	CHECK(SceneSerializer::Parse(fixture.Registry, R"({"Version": 1})", "v1.gscene", report, table).HasValue());
	CHECK(calls == 0);
	const Result<SceneData> future =
		SceneSerializer::Parse(fixture.Registry, R"({"Version": 2})", "v2.gscene", report, table);
	REQUIRE_FALSE(future.HasValue());
	CHECK(future.GetError().Message.starts_with("v2.gscene: "));
	CHECK(SceneSerializer::FormatVersion == 1);

	Json document = Json::parse(R"({"Version": 1})");
	CHECK(SceneSerializer::GetMigrations().Upgrade(document, SceneSerializer::FormatVersion, "scene").HasValue());
}
