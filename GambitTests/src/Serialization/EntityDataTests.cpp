#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/Serialization/EntityData.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	const ComponentData* FindComponent(const EntityData& entity, std::string_view name)
	{
		const auto found = std::ranges::find(entity.Components, name, &ComponentData::Name);
		return found == entity.Components.end() ? nullptr : &*found;
	}

	const FieldValue* FindField(const ComponentData& component, std::string_view name)
	{
		const auto found = std::ranges::find(component.Fields, name, &FieldData::Name);
		return found == component.Fields.end() ? nullptr : &found->Value;
	}

	std::vector<std::string> Names(const std::vector<Entity>& entities)
	{
		std::vector<std::string> names;
		for (const Entity& entity : entities)
		{
			names.push_back(entity.GetName());
		}
		return names;
	}

	// root
	//   a            (Transform, Health)
	//     a1
	//   b            (Disabled)
	// other          (a second root)
	struct Hierarchy
	{
		TestWorld Fixture;
		Entity Root, A, A1, B, Other;

		Hierarchy()
		{
			World& world = *Fixture.Scene;
			Root = world.CreateEntityWithUUID(UUID(10), "root");
			A = world.CreateEntityWithUUID(UUID(20), "a");
			A1 = world.CreateEntityWithUUID(UUID(21), "a1");
			B = world.CreateEntityWithUUID(UUID(30), "b");
			Other = world.CreateEntityWithUUID(UUID(40), "other");
			REQUIRE(world.SetParent(A, Root).HasValue());
			REQUIRE(world.SetParent(B, Root).HasValue());
			REQUIRE(world.SetParent(A1, A).HasValue());
			A.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
			A.AddComponent<HealthComponent>(25.0f, 50.0f);
			world.SetEnabled(B, false);
		}
	};
}

TEST_CASE("Serialization: capturing a subtree lists parents first and siblings in order")
{
	Hierarchy h;
	World& world = *h.Fixture.Scene;
	const std::vector<EntityData> subtree = CaptureSubtree(world, h.Root);
	REQUIRE(subtree.size() == 4);
	CHECK(subtree[0].Id == UUID(10));
	CHECK(subtree[1].Id == UUID(20));
	CHECK(subtree[2].Id == UUID(21));
	CHECK(subtree[3].Id == UUID(30));
	CHECK_FALSE(subtree[0].Parent.IsValid());
	CHECK(subtree[1].Parent == UUID(10));
	CHECK(subtree[2].Parent == UUID(20));

	// The root of a subtree is detached unless asked otherwise.
	const std::vector<EntityData> detached = CaptureSubtree(world, h.A);
	REQUIRE(detached.size() == 2);
	CHECK_FALSE(detached[0].Parent.IsValid());
	CHECK(detached[1].Parent == UUID(20));
	CHECK(CaptureSubtree(world, h.A, true)[0].Parent == UUID(10));

	// The whole world: roots in order, each with its subtree.
	const std::vector<EntityData> everything = CaptureWorld(world);
	REQUIRE(everything.size() == 5);
	CHECK(everything[4].Id == UUID(40));

	// Components in registry order; engine-managed and derived ones are left out.
	const EntityData& a = subtree[1];
	std::vector<std::string> names;
	for (const ComponentData& component : a.Components)
	{
		names.push_back(component.Name);
	}
	CHECK(names == std::vector<std::string>{ "Health", "Tag", "Transform" });
	REQUIRE(FindComponent(subtree[3], "Disabled") != nullptr);
	CHECK(FindComponent(subtree[3], "Disabled")->Fields.empty());
	CHECK(FindComponent(a, "ID") == nullptr);
	CHECK(FindComponent(a, "Relationship") == nullptr);
	CHECK(FindComponent(a, "Inactive") == nullptr);
	CHECK(*FindField(*FindComponent(a, "Tag"), "Name") == FieldValue(std::string("a")));
	CHECK(*FindField(*FindComponent(a, "Health"), "Current") == FieldValue(25.0f));
	CHECK(FindComponent(subtree[2], "Inactive") == nullptr);
}

TEST_CASE("Serialization: derived components are never captured")
{
	Hierarchy h;
	World& world = *h.Fixture.Scene;
	REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	REQUIRE(h.A.HasComponent<WorldTransformComponent>());
	CHECK(FindComponent(CaptureSubtree(world, h.A)[0], "WorldTransform") == nullptr);
}

TEST_CASE("Serialization: spawning recreates entities, components and hierarchy")
{
	Hierarchy source;
	const std::vector<EntityData> data = CaptureWorld(*source.Fixture.Scene);

	TestWorld target;
	const Result<std::vector<Entity>> spawned = SpawnEntities(*target.Scene, data);
	REQUIRE(spawned.HasValue());
	CHECK(Names(*spawned) == std::vector<std::string>{ "root", "a", "a1", "b", "other" });
	CHECK(target.Scene->ComputeStateHash() == source.Fixture.Scene->ComputeStateHash());

	World& world = *target.Scene;
	CHECK(world.TryGetEntity(UUID(21)) != Entity());
	CHECK(world.GetParent(world.TryGetEntity(UUID(21))) == world.TryGetEntity(UUID(20)));
	CHECK(Names(world.GetChildren(world.TryGetEntity(UUID(10)))) == std::vector<std::string>{ "a", "b" });
	CHECK(Names(world.GetRoots()) == std::vector<std::string>{ "root", "other" });
	CHECK(world.TryGetEntity(UUID(20)).GetComponent<HealthComponent>().Current == 25.0f);
	CHECK(world.TryGetEntity(UUID(20)).GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK_FALSE(world.IsEnabled(world.TryGetEntity(UUID(30))));
	CHECK_FALSE(world.IsActive(world.TryGetEntity(UUID(30))));
}

TEST_CASE("Serialization: spawning creates entities in hierarchy order whatever the order of the data")
{
	Hierarchy source;
	std::vector<EntityData> data = CaptureWorld(*source.Fixture.Scene);
	std::ranges::reverse(data); // children before parents

	TestWorld target;
	const Result<std::vector<Entity>> spawned = SpawnEntities(*target.Scene, data);
	REQUIRE(spawned.HasValue());
	// The roots keep the order of the data; each family is created parents first, siblings in data order.
	CHECK(Names(*spawned) == std::vector<std::string>{ "other", "root", "b", "a", "a1" });
	CHECK(
		Names(target.Scene->GetChildren(target.Scene->TryGetEntity(UUID(10)))) == std::vector<std::string>{ "b", "a" });
}

TEST_CASE("Serialization: components that are not fully listed start from their defaults")
{
	TestWorld target;
	EntityData data;
	data.Id = UUID(1);
	data.Components.push_back({ "Transform", { { "Scale", glm::vec3(2.0f) } } });
	data.Components.push_back({ "Health", {} });
	data.Components.push_back({ "PlayerTag", {} });
	REQUIRE(SpawnEntities(*target.Scene, std::span(&data, 1)).HasValue());
	Entity entity = target.Scene->TryGetEntity(UUID(1));
	REQUIRE(entity.HasComponent<HealthComponent>());
	REQUIRE(entity.HasComponent<PlayerTagComponent>());
	CHECK(entity.GetComponent<HealthComponent>().Current == 100.0f);
	CHECK(entity.GetComponent<TransformComponent>().Scale == glm::vec3(2.0f));
	CHECK(entity.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f));
	CHECK(entity.GetName() == "Entity"); // the Tag every entity has, without a listed name
}

TEST_CASE("Serialization: new UUIDs come from the world generator and are deterministic")
{
	Hierarchy source;
	const std::vector<EntityData> data = CaptureSubtree(*source.Fixture.Scene, source.Root);

	TestWorld first;
	TestWorld second;
	first.Scene->SetSeed(5);
	second.Scene->SetSeed(5);
	SpawnOptions options;
	options.Ids = UUIDMode::FromWorld;
	const Result<std::vector<Entity>> a = SpawnEntities(*first.Scene, data, options);
	const Result<std::vector<Entity>> b = SpawnEntities(*second.Scene, data, options);
	REQUIRE(a.HasValue());
	REQUIRE(b.HasValue());
	REQUIRE(a->size() == 4);
	for (size_t index = 0; index < a->size(); ++index)
	{
		CHECK((*a)[index].GetUUID() == (*b)[index].GetUUID());
		CHECK((*a)[index].GetUUID() != data[index].Id);
	}
	CHECK(first.Scene->ComputeStateHash() == second.Scene->ComputeStateHash());
	// The hierarchy is the same with new identities.
	CHECK(first.Scene->GetParent((*a)[2]) == (*a)[1]);
	CHECK(Names(first.Scene->GetChildren((*a)[0])) == std::vector<std::string>{ "a", "b" });

	// Spawning the same data again never clashes with what is there.
	SpawnOptions again = options;
	const Result<std::vector<Entity>> third = SpawnEntities(*first.Scene, data, again);
	REQUIRE(third.HasValue());
	CHECK(first.Scene->GetEntityCount() == 8);

	// Random UUIDs for tools.
	TestWorld tool;
	SpawnOptions random;
	random.Ids = UUIDMode::Random;
	const Result<std::vector<Entity>> r = SpawnEntities(*tool.Scene, data, random);
	REQUIRE(r.HasValue());
	CHECK((*r)[0].GetUUID() != data[0].Id);
	CHECK(tool.Scene->GetEntityCount() == 4);
}

TEST_CASE("Serialization: spawning under a parent attaches the roots of the data to it")
{
	Hierarchy source;
	const std::vector<EntityData> data = CaptureSubtree(*source.Fixture.Scene, source.A);

	TestWorld target;
	World& world = *target.Scene;
	Entity holder = world.CreateEntityWithUUID(UUID(900), "holder");
	Entity existing = world.CreateEntityWithUUID(UUID(901), "existing");
	REQUIRE(world.SetParent(existing, holder).HasValue());

	SpawnOptions options;
	options.Ids = UUIDMode::FromWorld;
	options.Parent = holder;
	const Result<std::vector<Entity>> spawned = SpawnEntities(world, data, options);
	REQUIRE(spawned.HasValue());
	CHECK(world.GetParent((*spawned)[0]) == holder);
	CHECK(Names(world.GetChildren(holder)) == std::vector<std::string>{ "existing", "a" }); // appended
	CHECK(world.GetParent((*spawned)[1]) == (*spawned)[0]);

	SpawnOptions foreign;
	foreign.Parent = Hierarchy().Root; // an entity of another world
	CHECK_FALSE(SpawnEntities(world, data, foreign).HasValue());
	CHECK(world.GetEntityCount() == 4);
}

TEST_CASE("Serialization: a deleted subtree can be restored under its old parent")
{
	Hierarchy h;
	World& world = *h.Fixture.Scene;
	const std::vector<EntityData> snapshot = CaptureSubtree(world, h.A, true);
	world.DestroyEntity(h.A);
	CHECK(world.GetEntityCount() == 3);

	REQUIRE(SpawnEntities(world, snapshot).HasValue());
	CHECK(world.GetEntityCount() == 5);
	CHECK(world.GetParent(world.TryGetEntity(UUID(20))) == world.TryGetEntity(UUID(10)));
	CHECK(world.GetParent(world.TryGetEntity(UUID(21))) == world.TryGetEntity(UUID(20)));
	// The subtree comes back as the last child; the rest of the state is as before.
	CHECK(Names(world.GetChildren(world.TryGetEntity(UUID(10)))) == std::vector<std::string>{ "b", "a" });

	// With new UUIDs the parent of the snapshot's root is not an option.
	SpawnOptions remapped;
	remapped.Ids = UUIDMode::FromWorld;
	CHECK_FALSE(SpawnEntities(world, snapshot, remapped).HasValue());
}

TEST_CASE("Serialization: spawning remaps references to spawned entities, including those inside structs")
{
	TestWorld source;
	World& world = *source.Scene;
	Entity a = world.CreateEntityWithUUID(UUID(1), "a");
	Entity b = world.CreateEntityWithUUID(UUID(2), "b");
	static_cast<void>(world.CreateEntityWithUUID(UUID(3), "outside")); // referenced, but not part of the data
	REQUIRE(world.SetParent(b, a).HasValue());
	auto& types = a.AddComponent<AllTypesComponent>();
	types.Target = UUID(2);                       // to b
	types.Targets = { UUID(1), UUID(3), UUID() }; // to a, to outside, to nothing
	types.Texture = UUID(0x7E);                   // an asset: never remapped
	auto& structs = b.AddComponent<StructsComponent>();
	structs.Main.Target = UUID(1);
	structs.Layers.resize(2);
	structs.Layers[0].Target = UUID(2);
	structs.Layers[1].Target = UUID(3);
	structs.Source = UUID(1); // a plain identifier: never remapped
	structs.Sources = { UUID(1), UUID(2) };
	const std::vector<EntityData> data = CaptureSubtree(world, a);

	const auto spawn = [&](ExternalReferences references) {
		TestWorld target;
		SpawnOptions options;
		options.Ids = UUIDMode::FromWorld;
		options.References = references;
		const Result<std::vector<Entity>> spawned = SpawnEntities(*target.Scene, data, options);
		REQUIRE(spawned.HasValue());
		const UUID newA = (*spawned)[0].GetUUID();
		const UUID newB = (*spawned)[1].GetUUID();
		const auto& copiedTypes = (*spawned)[0].GetComponent<AllTypesComponent>();
		const auto& copiedStructs = (*spawned)[1].GetComponent<StructsComponent>();
		CHECK(copiedTypes.Target == newB);
		CHECK(copiedTypes.Targets[0] == newA);
		CHECK(copiedTypes.Targets[2] == UUID());
		CHECK(copiedTypes.Texture == UUID(0x7E));
		CHECK(copiedStructs.Main.Target == newA);
		CHECK(copiedStructs.Layers[0].Target == newB);
		CHECK(copiedStructs.Source == UUID(1));
		CHECK(copiedStructs.Sources == std::vector<UUID>{ UUID(1), UUID(2) });
		return std::pair(copiedTypes.Targets[1], copiedStructs.Layers[1].Target);
	};

	// References to entities that are not part of the data stay, or are cleared.
	const auto kept = spawn(ExternalReferences::Keep);
	CHECK(kept.first == UUID(3));
	CHECK(kept.second == UUID(3));
	const auto cleared = spawn(ExternalReferences::Clear);
	CHECK(cleared.first == UUID());
	CHECK(cleared.second == UUID());

	// With the original UUIDs nothing is remapped.
	TestWorld restored;
	REQUIRE(SpawnEntities(*restored.Scene, data).HasValue());
	CHECK(restored.Scene->TryGetEntity(UUID(1)).GetComponent<AllTypesComponent>().Target == UUID(2));
}

TEST_CASE("Serialization: references outside the data can be cleared and asset references collected")
{
	TestWorld source;
	World& world = *source.Scene;
	Entity a = world.CreateEntityWithUUID(UUID(1), "a");
	Entity b = world.CreateEntityWithUUID(UUID(2), "b");
	REQUIRE(world.SetParent(b, a).HasValue());
	auto& types = a.AddComponent<AllTypesComponent>();
	types.Target = UUID(2);
	types.Targets = { UUID(5), UUID(1) };
	types.Texture = UUID(0x30);
	types.Textures = { UUID(0x10), UUID(0x30), UUID() };
	b.AddComponent<StructsComponent>().Main.Target = UUID(7);

	std::vector<EntityData> data = CaptureSubtree(world, a);
	CHECK(CollectAssetReferences(source.Registry, data) == std::vector<UUID>{ UUID(0x10), UUID(0x30) });
	CHECK(ClearExternalEntityReferences(source.Registry, data) == 2); // UUID(5) and UUID(7)

	TestWorld target;
	REQUIRE(SpawnEntities(*target.Scene, data).HasValue());
	CHECK(
		target.Scene->TryGetEntity(UUID(1)).GetComponent<AllTypesComponent>().Targets
		== std::vector<UUID>{ UUID(), UUID(1) });
	CHECK(target.Scene->TryGetEntity(UUID(2)).GetComponent<StructsComponent>().Main.Target == UUID());
	CHECK(ClearExternalEntityReferences(source.Registry, data) == 0);
}

TEST_CASE("Serialization: spawning invalid data fails without changing the world")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	static_cast<void>(world.CreateEntityWithUUID(UUID(100), "present"));
	const uint64_t hash = world.ComputeStateHash();

	const auto spawnError = [&](std::vector<EntityData> data, const SpawnOptions& options = {}) {
		const Result<std::vector<Entity>> result = SpawnEntities(world, data, options);
		REQUIRE_FALSE(result.HasValue());
		CHECK(world.GetEntityCount() == 1);
		CHECK(world.ComputeStateHash() == hash);
		return result.GetError().Message;
	};
	const auto entity = [](uint64_t id, uint64_t parent = 0) {
		EntityData data;
		data.Id = UUID(id);
		data.Parent = UUID(parent);
		return data;
	};

	CHECK(spawnError({ entity(0) }).find("null UUID") != std::string::npos);
	CHECK(spawnError({ entity(1), entity(1) }).find("appears twice") != std::string::npos);
	CHECK(spawnError({ entity(100) }).find("already used") != std::string::npos);
	CHECK(spawnError({ entity(1, 99) }).find("not part of the data") != std::string::npos);
	CHECK(spawnError({ entity(1, 1) }).find("its own parent") != std::string::npos);
	CHECK(spawnError({ entity(1, 2), entity(2, 3), entity(3, 1) }).find("cycle") != std::string::npos);
	CHECK(spawnError({ entity(1), entity(2, 3), entity(3, 2) }).find("cycle") != std::string::npos);

	EntityData unknown = entity(1);
	unknown.Components.push_back({ "Warp", {} });
	CHECK(spawnError({ unknown }).find("unknown component 'Warp'") != std::string::npos);

	for (const char* internal : { "ID", "Relationship", "WorldTransform", "Inactive" })
	{
		EntityData managed = entity(1);
		managed.Components.push_back({ internal, {} });
		CHECK_MESSAGE(spawnError({ managed }).find("managed by the engine") != std::string::npos, internal);
	}

	EntityData twice = entity(1);
	twice.Components.push_back({ "Health", {} });
	twice.Components.push_back({ "Health", {} });
	CHECK(spawnError({ twice }).find("appears twice") != std::string::npos);

	EntityData noField = entity(1);
	noField.Components.push_back({ "Health", { { "Mana", 1.0f } } });
	CHECK(spawnError({ noField }).find("has no field 'Mana'") != std::string::npos);

	EntityData wrongType = entity(1);
	wrongType.Components.push_back({ "Health", { { "Current", 1.0 } } }); // a double for a float
	CHECK(spawnError({ wrongType }).find("does not fit its type Float") != std::string::npos);

	// The parent of the spawned entities must belong to this world.
	SpawnOptions foreign;
	foreign.Parent = Hierarchy().Root;
	CHECK(spawnError({ entity(1) }, foreign).find("not a valid entity") != std::string::npos);

	// Hierarchy checks work without a world, too.
	CHECK(ValidateEntityHierarchy(std::vector<EntityData>{ entity(1), entity(2, 1) }).HasValue());
	CHECK_FALSE(ValidateEntityHierarchy(std::vector<EntityData>{ entity(1), entity(2, 7) }).HasValue());
	CHECK_FALSE(ValidateEntityHierarchy(std::vector<EntityData>{ entity(1), entity(1) }).HasValue());
	CHECK_FALSE(ValidateEntityHierarchy(std::vector<EntityData>{ entity(2, 3), entity(3, 2) }).HasValue());
}

TEST_CASE("Serialization: script components are captured and spawned like C++ ones")
{
	TestWorld source;
	const ComponentInfo& script = *source.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	Entity entity = source.Scene->CreateEntityWithUUID(UUID(5), "Scripted");
	REQUIRE(script.Add(entity) != nullptr);
	REQUIRE(script.SetFieldValue(entity, "Label", std::string("custom")));
	REQUIRE(script.SetFieldValue(entity, "Weights", std::vector<float>{ 1.0f, 2.0f, 3.0f }));
	const std::vector<EntityData> data = CaptureWorld(*source.Scene);
	const ComponentData* captured = FindComponent(data[0], "ScriptStats");
	REQUIRE(captured != nullptr);
	CHECK(captured->Fields.size() == 13); // the Debug field is not stored

	TestWorld target;
	REQUIRE(target.Registry.RegisterDynamicComponent(MakeScriptSchema()).HasValue());
	REQUIRE(SpawnEntities(*target.Scene, data).HasValue());
	CHECK(target.Scene->ComputeStateHash() == source.Scene->ComputeStateHash());

	// A world whose registry lacks the schema refuses the data.
	TestWorld without;
	CHECK_FALSE(SpawnEntities(*without.Scene, data).HasValue());
}

TEST_CASE("Serialization: entities have a JSON form")
{
	Hierarchy h;
	World& world = *h.Fixture.Scene;
	const TypeRegistry& types = h.Fixture.Registry;
	const std::vector<EntityData> data = CaptureWorld(world);

	const Result<Json> json = EntitiesToJson(types, data, "/Entities");
	REQUIRE(json.HasValue());
	REQUIRE(json->size() == 5);
	const Json& a = (*json)[1];
	CHECK(a["UUID"] == "0x0000000000000014");
	CHECK(a["Parent"] == "0x000000000000000A");
	CHECK(a["Components"]["Tag"] == Json::parse(R"({"Name": "a"})"));
	CHECK(a["Components"]["Health"] == Json::parse(R"({"Current": 25.0, "Maximum": 50.0})"));
	CHECK((*json)[0].contains("Parent") == false);
	CHECK((*json)[3]["Components"]["Disabled"] == Json::object());
	// Keys: UUID, Parent, Components; components in name order.
	std::vector<std::string> keys;
	for (const auto& [key, member] : a["Components"].items())
	{
		keys.push_back(key);
	}
	CHECK(keys == std::vector<std::string>{ "Health", "Tag", "Transform" });

	LoadReport report;
	const Result<std::vector<EntityData>> decoded = EntitiesFromJson(types, *json, "/Entities", report);
	REQUIRE(decoded.HasValue());
	CHECK(report.IsEmpty());
	CHECK(*EntitiesToJson(types, *decoded) == *json);
	TestWorld target;
	REQUIRE(SpawnEntities(*target.Scene, *decoded).HasValue());
	CHECK(target.Scene->ComputeStateHash() == world.ComputeStateHash());
}

TEST_CASE("Serialization: unknown components and members are warnings, malformed ones are errors")
{
	TestWorld fixture;
	const TypeRegistry& types = fixture.Registry;
	LoadReport report;
	const Result<EntityData> entity = EntityFromJson(
		types, Json::parse(R"({
			"UUID": "0x1",
			"Tint": 5,
			"Components": {
				"Warp": {"Speed": 9},
				"Inactive": {},
				"ID": {"ID": "0x2"},
				"Health": {"Current": 5.0, "Armor": 3},
				"PlayerTag": {"Oops": 1}
			}
		})"),
		"/Entities/0", report);
	REQUIRE(entity.HasValue());
	CHECK(entity->Id == UUID(1));
	REQUIRE(entity->Components.size() == 2); // Health and PlayerTag
	CHECK(entity->Components[0].Name == "Health");
	REQUIRE(entity->Components[0].Fields.size() == 1);
	std::vector<std::string> warned;
	for (const Diagnostic& warning : report.GetWarnings())
	{
		warned.push_back(warning.Path);
	}
	CHECK(
		warned
		== std::vector<std::string>{ "/Entities/0/Tint", "/Entities/0/Components/Warp",
	                                 "/Entities/0/Components/Inactive", "/Entities/0/Components/ID",
	                                 "/Entities/0/Components/Health/Armor", "/Entities/0/Components/PlayerTag/Oops" });

	const auto errorOf = [&](const char* text) {
		LoadReport ignored;
		const Result<EntityData> result = EntityFromJson(types, Json::parse(text), "/Entities/2", ignored);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(errorOf("[]").find("expected an entity object") != std::string::npos);
	CHECK(errorOf("{}").find("has no \"UUID\"") != std::string::npos);
	CHECK(errorOf(R"({"UUID": 5})").find("/Entities/2/UUID: expected a UUID string") == 0);
	CHECK(errorOf(R"({"UUID": "nope"})").find("not a UUID") != std::string::npos);
	CHECK(errorOf(R"({"UUID": "0x0"})").find("must not be null") != std::string::npos);
	CHECK(errorOf(R"({"UUID": "0x1", "Parent": 3})").find("/Entities/2/Parent") == 0);
	CHECK(
		errorOf(R"({"UUID": "0x1", "Components": []})").find("expected an object of components") != std::string::npos);
	CHECK(
		errorOf(R"({"UUID": "0x1", "Components": {"Health": {"Current": "full"}}})")
		== "/Entities/2/Components/Health/Current: expected a number, found a string");
	CHECK(errorOf(R"({"UUID": "0x1", "Components": {"Health": 5}})").find("expected an object") != std::string::npos);
}

TEST_CASE("Serialization: a live component converts to a JSON object and back")
{
	TestWorld fixture;
	Entity entity = fixture.Scene->CreateEntityWithUUID(UUID(1), "Holder");
	const ComponentInfo& health = fixture.Registry.GetComponent<HealthComponent>();
	const ComponentInfo& player = fixture.Registry.GetComponent<PlayerTagComponent>();
	const ComponentInfo& allTypes = fixture.Registry.GetComponent<AllTypesComponent>();
	static_cast<void>(entity.AddComponent<HealthComponent>(5.0f, 6.0f));
	entity.AddComponent<PlayerTagComponent>();
	auto& types = entity.AddComponent<AllTypesComponent>();
	types.Cache = 3.0f; // NotSerialized

	const Result<Json> healthJson = ComponentToJson(health, health.GetRaw(entity));
	REQUIRE(healthJson.HasValue());
	CHECK(*healthJson == Json::parse(R"({"Current": 5.0, "Maximum": 6.0})"));
	CHECK(*ComponentToJson(player, player.GetRaw(entity)) == Json::object()); // tags have no fields
	const Result<Json> allJson = ComponentToJson(allTypes, allTypes.GetRaw(entity));
	REQUIRE(allJson.HasValue());
	CHECK_FALSE(allJson->contains("Cache"));
	CHECK((*allJson)["Mode"] == "Slow");

	// The object reads back to the same field values; a partial object gives a partial component.
	LoadReport report;
	const Result<ComponentData> data = ComponentDataFromJson(allTypes, *allJson, "/Components/AllTypes", report);
	REQUIRE(data.HasValue());
	CHECK(report.IsEmpty());
	CHECK(data->Name == "AllTypes");
	CHECK(data->Fields.size() == allTypes.GetFields().size() - 1); // all but Cache
	CHECK(*ComponentDataToJson(allTypes, *data) == *allJson);
	const Result<ComponentData> partial = ComponentDataFromJson(health, Json::parse(R"({"Maximum": 9.0})"), "", report);
	REQUIRE(partial.HasValue());
	REQUIRE(partial->Fields.size() == 1);
	CHECK(partial->Fields[0].Name == "Maximum");
	CHECK(*ComponentDataToJson(health, *partial) == Json::parse(R"({"Maximum": 9.0})"));

	// Problems say where they are.
	types.String = "bad \xFF text";
	const Result<Json> invalid = ComponentToJson(allTypes, allTypes.GetRaw(entity), "/Components/AllTypes");
	REQUIRE_FALSE(invalid.HasValue());
	CHECK(invalid.GetError().Message == "/Components/AllTypes/String: the text is not valid UTF-8");
	ComponentData unknown;
	unknown.Name = "Health";
	unknown.Fields.push_back({ "Mana", FieldValue(1.0f) });
	CHECK_FALSE(ComponentDataToJson(health, unknown).HasValue());
	unknown.Fields = { { "Current", FieldValue(std::string("full")) } };
	CHECK_FALSE(ComponentDataToJson(health, unknown).HasValue()); // does not fit the field
}
