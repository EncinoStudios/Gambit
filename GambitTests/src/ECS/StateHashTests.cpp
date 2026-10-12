#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/ECS/Systems/TransformSystem.h"

#include <string>
#include <utility>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct Damage
	{
		UUID Target;
		float Amount = 0.0f;
	};

	void HashEvent(Hasher& hasher, const Damage& damage)
	{
		HashEventFields(hasher, damage.Target, damage.Amount);
	}

	enum class HealKind : uint8_t
	{
		Potion,
		Spell
	};

	struct Heal
	{
		UUID Target;
		HealKind Kind = HealKind::Potion;
		std::vector<glm::vec3> Path;
		std::string Source;
	};

	void HashEvent(Hasher& hasher, const Heal& heal)
	{
		HashEventFields(hasher, heal.Target, heal.Kind, heal.Path, heal.Source);
	}

	// Populates a world with one entity per kind of content: every C++ test component, a script component, a tag,
	// a hierarchy, a disabled subtree, and random generator use.
	void Populate(World& world, const ComponentInfo& script)
	{
		world.SetSeed(2024);
		Entity root = world.CreateEntityWithUUID(UUID(100), "Root");
		root.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
		root.AddComponent<AllTypesComponent>();
		Entity child = world.CreateEntityWithUUID(UUID(200), "Child");
		child.AddComponent<HealthComponent>(25.0f, 50.0f);
		child.AddComponent<PlayerTagComponent>();
		REQUIRE(world.SetParent(child, root).HasValue());
		Entity scripted = world.CreateEntityWithUUID(UUID(300), "Scripted");
		REQUIRE(script.Add(scripted) != nullptr);
		REQUIRE(world.SetParent(scripted, child).HasValue());
		world.SetEnabled(child, false);
		static_cast<void>(world.GetRandom().NextU32());
	}

	// Calls `check` after changing each hashed field of each component of `entity` (restoring it afterwards), and
	// `checkUnhashed` for the fields left out of the hash.
	template<typename Changed, typename Unchanged>
	void ForEachFieldChange(World& world, Entity entity, Changed&& check, Unchanged&& checkUnhashed)
	{
		for (const ComponentInfo* info : world.GetTypeRegistry().GetComponents())
		{
			void* component = info->GetRaw(entity);
			if (component == nullptr)
			{
				continue;
			}
			for (const FieldInfo& field : info->GetFields())
			{
				CAPTURE(info->GetName());
				CAPTURE(field.Name);
				const FieldValue original = field.GetValue(component);
				REQUIRE(field.SetValue(component, MakeDifferentValue(field, original)));
				if (field.HasFlag(FieldFlags::NotHashed))
				{
					checkUnhashed();
				}
				else
				{
					check();
				}
				REQUIRE(field.SetValue(component, original));
			}
		}
	}
}

TEST_CASE("ECS: the state hash is stable and reproducible")
{
	TestWorld first;
	TestWorld second;
	const ComponentInfo& script = *first.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	const ComponentInfo& script2 = *second.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	Populate(*first.Scene, script);
	Populate(*second.Scene, script2);

	const uint64_t hash = first.Scene->ComputeStateHash();
	CHECK(hash == first.Scene->ComputeStateHash()); // hashing changes nothing
	CHECK(hash == second.Scene->ComputeStateHash());

	TestWorld empty;
	CHECK(empty.Scene->ComputeStateHash() != hash);
	CHECK(empty.Scene->ComputeStateHash() == TestWorld().Scene->ComputeStateHash());
}

TEST_CASE("ECS: the state hash does not depend on the order in which unrelated entities were created")
{
	TestWorld forward;
	TestWorld backward;
	const std::vector<uint64_t> ids = { 5, 1, 9, 3, 7 };
	for (size_t i = 0; i < ids.size(); ++i)
	{
		Entity entity = forward.Scene->CreateEntityWithUUID(UUID(ids[i]), "E" + std::to_string(ids[i]));
		entity.AddComponent<HealthComponent>(static_cast<float>(ids[i]), 100.0f);
		entity.AddComponent<TransformComponent>().Translation.x = static_cast<float>(ids[i]);
	}
	for (size_t i = ids.size(); i-- > 0;)
	{
		Entity entity = backward.Scene->CreateEntityWithUUID(UUID(ids[i]), "E" + std::to_string(ids[i]));
		entity.AddComponent<TransformComponent>().Translation.x = static_cast<float>(ids[i]); // other component order
		entity.AddComponent<HealthComponent>(static_cast<float>(ids[i]), 100.0f);
	}
	CHECK(forward.Scene->ComputeStateHash() == backward.Scene->ComputeStateHash());

	// The same state reached through a destroyed extra entity hashes the same too.
	Entity extra = backward.Scene->CreateEntityWithUUID(UUID(42), "Extra");
	CHECK(forward.Scene->ComputeStateHash() != backward.Scene->ComputeStateHash());
	backward.Scene->DestroyEntity(extra);
	CHECK(forward.Scene->ComputeStateHash() == backward.Scene->ComputeStateHash());
}

TEST_CASE("ECS: changing any hashed field changes the state hash")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	const ComponentInfo& script = *fixture.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	Populate(world, script);
	const uint64_t original = world.ComputeStateHash();

	size_t hashed = 0;
	size_t unhashed = 0;
	for (const UUID uuid : world.GetEntityUUIDs())
	{
		ForEachFieldChange(
			world, world.TryGetEntity(uuid),
			[&] {
				++hashed;
				CHECK(world.ComputeStateHash() != original);
			},
			[&] {
				++unhashed;
				CHECK(world.ComputeStateHash() == original);
			});
		CHECK(world.ComputeStateHash() == original); // restored
	}
	// Every hashed field that generic access may write: AllTypes 33, the script schema 13, Transform 3, Health 2 and
	// the Tag of each entity. Identity and hierarchy are Internal components, covered by their own tests.
	CHECK(hashed == 33 + 13 + 3 + 2 + 3);
	CHECK(unhashed == 2); // AllTypes.Cache, ScriptStats.Debug

	// Presence of components, tags, entities and the simulation resources count too.
	Entity root = world.TryGetEntity(UUID(100));
	root.AddComponent<PlayerTagComponent>();
	CHECK(world.ComputeStateHash() != original);
	root.RemoveComponent<PlayerTagComponent>();
	CHECK(world.ComputeStateHash() == original);
	static_cast<void>(world.GetRandom().NextU32());
	const uint64_t afterDraw = world.ComputeStateHash();
	CHECK(afterDraw != original);
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(world.ComputeStateHash() != afterDraw);
	const uint64_t afterTick = world.ComputeStateHash();
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit); // no simulation step: nothing changes
	CHECK(world.ComputeStateHash() == afterTick);
}

TEST_CASE("ECS: World::Copy produces an equal and independent world")
{
	TestWorld fixture;
	World& source = *fixture.Scene;
	const ComponentInfo& script = *fixture.Registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	Populate(source, script);
	REQUIRE(source.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	source.Events<Damage>().Emit({ UUID(100), 1.0f });
	source.Tick(TimeStep(0.5f));
	source.Events<Damage>().Emit({ UUID(200), 3.0f });
	const UUID pending = source.GetCommandBuffer().CreateEntity("Pending");

	const Scope<World> copy = World::Copy(source);
	CHECK(copy->ComputeStateHash() == source.ComputeStateHash());
	CHECK(&copy->GetTypeRegistry() == &fixture.Registry);
	CHECK(copy->GetTick() == 1);
	CHECK(copy->GetTime() == 0.5);
	CHECK(copy->GetSeed() == source.GetSeed());
	CHECK(copy->GetRandom().GetState() == source.GetRandom().GetState());
	CHECK(copy->GetEntityCount() == source.GetEntityCount());
	CHECK(copy->GetScheduler().GetSystemCount() == 0); // systems are not copied

	// Same handles, same hierarchy, same derived state, same events and pending commands.
	for (const UUID uuid : source.GetEntityUUIDs())
	{
		CHECK(copy->TryGetEntity(uuid).GetHandle() == source.TryGetEntity(uuid).GetHandle());
	}
	Entity copiedChild = copy->TryGetEntity(UUID(200));
	CHECK(copy->GetParent(copiedChild).GetUUID() == UUID(100));
	CHECK_FALSE(copy->IsActive(copiedChild));
	CHECK_FALSE(copy->IsActive(copy->TryGetEntity(UUID(300))));
	CHECK(copiedChild.GetComponent<HealthComponent>().Current == 25.0f);
	CHECK(copy->TryGetEntity(UUID(100)).HasComponent<WorldTransformComponent>());
	CHECK(script.GetFieldValue(copy->TryGetEntity(UUID(300)), "Label") == FieldValue(std::string("spin")));
	REQUIRE(copy->Events<Damage>().View().size() == 1); // both buffers
	CHECK(copy->Events<Damage>().View()[0].Amount == 1.0f);
	REQUIRE(copy->Events<Damage>().ViewCurrent().size() == 1);
	CHECK(copy->Events<Damage>().ViewCurrent()[0].Amount == 3.0f);
	CHECK(copy->GetCommandBuffer().GetCommandCount() == 1);
	CHECK(copy->GetCommandBuffer().Flush().Executed == 1);
	CHECK(copy->ContainsEntity(pending));
	CHECK_FALSE(source.ContainsEntity(pending));
	source.GetCommandBuffer().Clear();

	// Deep: changing the copy leaves the source alone.
	const uint64_t sourceHash = source.ComputeStateHash();
	copiedChild.GetComponent<HealthComponent>().Current = 1.0f;
	REQUIRE(script.SetFieldValue(copy->TryGetEntity(UUID(300)), "Label", std::string("changed")));
	copy->SetEnabled(copiedChild, true);
	copy->DestroyEntity(copy->TryGetEntity(UUID(100)));
	CHECK(source.ComputeStateHash() == sourceHash);
	CHECK(source.TryGetEntity(UUID(200)).GetComponent<HealthComponent>().Current == 25.0f);
	CHECK(script.GetFieldValue(source.TryGetEntity(UUID(300)), "Label") == FieldValue(std::string("spin")));
	CHECK_FALSE(source.IsActive(source.TryGetEntity(UUID(300))));
}

TEST_CASE("ECS: the events of both buffers are part of the state hash")
{
	TestWorld first;
	TestWorld second;
	World& a = *first.Scene;
	World& b = *second.Scene;
	const uint64_t empty = a.ComputeStateHash();

	// A queue without events counts as absent.
	static_cast<void>(a.Events<Damage>());
	CHECK(a.ComputeStateHash() == empty);

	a.Events<Damage>().Emit({ UUID(1), 2.0f });
	const uint64_t withCurrent = a.ComputeStateHash();
	CHECK(withCurrent != empty);
	b.Events<Damage>().Emit({ UUID(1), 2.0f });
	CHECK(b.ComputeStateHash() == withCurrent);

	// Every field counts, enums, arrays and strings included.
	const auto hashWith = [](const Heal& heal) {
		TestWorld fixture;
		fixture.Scene->Events<Heal>().Emit(heal);
		return fixture.Scene->ComputeStateHash();
	};
	const Heal heal{ UUID(7), HealKind::Spell, { glm::vec3(1.0f) }, "Shrine" };
	const uint64_t healHash = hashWith(heal);
	CHECK(healHash != empty);
	Heal changed = heal;
	changed.Target = UUID(8);
	CHECK(hashWith(changed) != healHash);
	changed = heal;
	changed.Kind = HealKind::Potion;
	CHECK(hashWith(changed) != healHash);
	changed = heal;
	changed.Path.front().z = 2.0f;
	CHECK(hashWith(changed) != healHash);
	changed = heal;
	changed.Source = "Fountain";
	CHECK(hashWith(changed) != healHash);

	// The buffer holding an event counts, and events leave the hash two ticks after their emission.
	a.Tick(TimeStep(0.5f));
	b.Tick(TimeStep(0.5f));
	const uint64_t withPrevious = a.ComputeStateHash();
	CHECK(withPrevious == b.ComputeStateHash());
	TestWorld third;
	World& c = *third.Scene;
	c.Tick(TimeStep(0.5f));
	c.Events<Damage>().Emit({ UUID(1), 2.0f }); // the same event and tick, in the current buffer
	CHECK(c.ComputeStateHash() != withPrevious);
	b.Events<Damage>().Clear();
	CHECK(b.ComputeStateHash() != withPrevious);
	a.Tick(TimeStep(0.5f));
	b.Tick(TimeStep(0.5f));
	CHECK(a.ComputeStateHash() == b.ComputeStateHash());

	// Queues are hashed in an order that does not depend on which event type was used first.
	a.Events<Heal>().Emit(heal);
	a.Events<Damage>().Emit({ UUID(3), 4.0f });
	b.Events<Damage>().Emit({ UUID(3), 4.0f });
	b.Events<Heal>().Emit(heal);
	CHECK(a.ComputeStateHash() == b.ComputeStateHash());
	b.Events<Damage>().Emit({ UUID(3), 4.0f });
	CHECK(a.ComputeStateHash() != b.ComputeStateHash());
}

TEST_CASE("ECS: World::Copy keeps root order and storage order")
{
	TestWorld fixture;
	World& source = *fixture.Scene;
	std::vector<Entity> entities;
	for (int i = 0; i < 12; ++i)
	{
		entities.push_back(source.CreateEntity(std::to_string(i)));
		entities.back().AddComponent<HealthComponent>(static_cast<float>(i), 100.0f);
	}
	// Swap-and-pop removals shuffle the storage away from creation order.
	source.DestroyEntity(entities[2]);
	entities[5].RemoveComponent<HealthComponent>();
	source.DestroyEntity(entities[0]);
	REQUIRE(source.SetParent(entities[7], Entity()).HasValue());
	REQUIRE(source.SetParent(entities[3], entities[9]).HasValue());
	REQUIRE(source.SetParent(entities[3], Entity()).HasValue()); // back to the roots, now last

	const Scope<World> copy = World::Copy(source);
	const auto names = [](World& world) {
		std::vector<std::string> result;
		for (const Entity root : world.GetRoots())
		{
			result.push_back(root.GetName());
		}
		return result;
	};
	CHECK(names(*copy) == names(source));
	CHECK(names(source).back() == "3");

	const auto storageOrder = [](World& world) {
		std::vector<std::string> result;
		for (const auto [handle, health] : world.GetRegistry().view<HealthComponent>().each())
		{
			result.push_back(Entity(handle, &world).GetName());
		}
		return result;
	};
	CHECK(storageOrder(*copy) == storageOrder(source));
	CHECK(storageOrder(source) != std::vector<std::string>{ "11", "10", "9", "8", "7", "6", "4", "3", "1" });

	// The entity storage is reproduced with the destroyed identifiers awaiting reuse, so new entities get the same
	// (recycled) handles in both worlds.
	const auto entityStorage = [](World& world) {
		const auto& storage = world.GetRegistry().storage<entt::entity>();
		return std::pair(
			std::vector<entt::entity>(storage.data(), storage.data() + storage.size()), storage.free_list());
	};
	CHECK(entityStorage(*copy) == entityStorage(source));
	CHECK(entityStorage(source).second < entityStorage(source).first.size()); // there are identifiers to reuse
	for (int i = 0; i < 3; ++i)
	{
		const Entity created = source.CreateEntity("New");
		CHECK(copy->CreateEntity("New").GetHandle() == created.GetHandle());
	}
	CHECK(entityStorage(*copy) == entityStorage(source));
	CHECK(copy->ComputeStateHash() == source.ComputeStateHash());
}

TEST_CASE("ECS: derived components do not take part in the state hash, not even their presence")
{
	TestWorld without;
	TestWorld with;
	for (World* world : { without.Scene.get(), with.Scene.get() })
	{
		Entity entity = world->CreateEntityWithUUID(UUID(1), "Moving");
		entity.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	}
	REQUIRE(with.Scene->GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());

	const uint64_t before = with.Scene->ComputeStateHash();
	CHECK(before == without.Scene->ComputeStateHash());
	with.Scene->Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit); // computes WorldTransform, advances nothing
	REQUIRE(with.Scene->TryGetEntity(UUID(1)).HasComponent<WorldTransformComponent>());
	CHECK(with.Scene->ComputeStateHash() == before);

	// Its value changes with the transform, and still the hash only follows the transform.
	with.Scene->TryGetEntity(UUID(1)).GetComponent<TransformComponent>().Translation.x = 5.0f;
	without.Scene->TryGetEntity(UUID(1)).GetComponent<TransformComponent>().Translation.x = 5.0f;
	with.Scene->Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	CHECK(with.Scene->ComputeStateHash() == without.Scene->ComputeStateHash());

	// The copy reproduces the derived component even though it is not hashed.
	const Scope<World> copy = World::Copy(*with.Scene);
	CHECK(copy->TryGetEntity(UUID(1)).HasComponent<WorldTransformComponent>());
	CHECK(copy->ComputeStateHash() == with.Scene->ComputeStateHash());
}

TEST_CASE("ECS: the state hash covers identity, hierarchy and sibling order")
{
	const auto build = [](TestWorld& fixture, UUID parentOfB, bool bBeforeC) {
		World& world = *fixture.Scene;
		Entity a = world.CreateEntityWithUUID(UUID(1), "A");
		Entity b = world.CreateEntityWithUUID(UUID(2), "B");
		Entity c = world.CreateEntityWithUUID(UUID(3), "C");
		REQUIRE(world.SetParent(bBeforeC ? b : c, a).HasValue());
		REQUIRE(world.SetParent(bBeforeC ? c : b, a).HasValue());
		if (parentOfB.IsValid())
		{
			REQUIRE(world.SetParent(b, world.TryGetEntity(parentOfB)).HasValue());
		}
		return world.ComputeStateHash();
	};

	TestWorld base;
	TestWorld same;
	TestWorld otherOrder;
	TestWorld otherParent;
	TestWorld otherIdentity;
	const uint64_t hash = build(base, UUID(), true);
	CHECK(build(same, UUID(), true) == hash);
	CHECK(build(otherOrder, UUID(), false) != hash); // the order of A's children is hashed
	CHECK(build(otherParent, UUID(3), true) != hash);
	{
		World& world = *otherIdentity.Scene;
		Entity a = world.CreateEntityWithUUID(UUID(1), "A");
		Entity b = world.CreateEntityWithUUID(UUID(2), "B");
		Entity c = world.CreateEntityWithUUID(UUID(4), "C"); // another identity
		REQUIRE(world.SetParent(b, a).HasValue());
		REQUIRE(world.SetParent(c, a).HasValue());
		CHECK(world.ComputeStateHash() != hash);
	}
}
