#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"
#include "TestSupport.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// The UUID map, the IDComponent storage and the entity count agree.
	void CheckConsistent(World& world)
	{
		const std::vector<UUID> uuids = world.GetEntityUUIDs();
		CHECK(uuids.size() == world.GetEntityCount());
		CHECK(std::ranges::is_sorted(uuids));
		size_t alive = 0;
		for (const auto [handle, id] : world.GetRegistry().view<IDComponent>().each())
		{
			++alive;
			CHECK(world.TryGetEntity(id.ID).GetHandle() == handle);
		}
		CHECK(alive == world.GetEntityCount());
		CHECK(world.GetRegistry().storage<entt::entity>().free_list() == world.GetEntityCount());
	}

	struct Hit
	{
		UUID Entity;
		float Damage = 0.0f;
	};

	void HashEvent(Hasher& hasher, const Hit& hit)
	{
		HashEventFields(hasher, hit.Entity, hit.Damage);
	}

	// Emits one Hit per tick (Damage = the tick) in the Update phase.
	class HitEmitter final : public ISystem
	{
	public:
		[[nodiscard]] std::string_view GetName() const override { return "HitEmitter"; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnUpdate(World& world, TimeStep timeStep) override
		{
			world.Events<Hit>().Emit({ UUID(1), static_cast<float>(timeStep.GetTick()) });
		}
	};

	// Records the damage of every Hit it sees, from View() or from ViewCurrent().
	class HitReader final : public ISystem
	{
	public:
		HitReader(std::string name, SystemPhase phase, bool current)
			: m_Name(std::move(name)), m_Phase(phase), m_Current(current)
		{}

		[[nodiscard]] std::string_view GetName() const override { return m_Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return m_Phase; }

		void OnUpdate(World& world, TimeStep) override
		{
			EventQueue<Hit>& hits = world.Events<Hit>();
			for (const Hit& hit : m_Current ? hits.ViewCurrent() : hits.View())
			{
				Seen.push_back(hit.Damage);
			}
		}

		std::vector<float> Seen;

	private:
		std::string m_Name;
		SystemPhase m_Phase;
		bool m_Current;
	};
}

TEST_CASE("ECS: entities are created with an identity, a name and a hierarchy slot")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	CHECK(world.GetEntityCount() == 0);

	Entity entity = world.CreateEntity("Crate");
	REQUIRE(entity.IsValid());
	CHECK(static_cast<bool>(entity));
	CHECK(entity.GetWorld() == &world);
	CHECK(entity.GetName() == "Crate");
	CHECK(entity.GetUUID().IsValid());
	CHECK(entity.HasComponent<IDComponent>());
	CHECK(entity.HasComponent<TagComponent>());
	CHECK(entity.HasComponent<RelationshipComponent>());
	CHECK_FALSE(entity.HasComponent<TransformComponent>()); // optional
	CHECK(world.GetEntityCount() == 1);
	CHECK(world.TryGetEntity(entity.GetUUID()) == entity);
	CHECK(world.ContainsEntity(entity.GetUUID()));

	CHECK(world.CreateEntity().GetName() == "Entity");
	entity.SetName("Box");
	CHECK(entity.GetName() == "Box");

	const Entity null;
	CHECK_FALSE(null.IsValid());
	CHECK_FALSE(static_cast<bool>(null));
	CHECK(null.GetWorld() == nullptr);
	CheckConsistent(world);
}

TEST_CASE("ECS: entities with given UUIDs")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	Entity entity = world.CreateEntityWithUUID(UUID(0x42), "Fixed");
	REQUIRE(entity.IsValid());
	CHECK(entity.GetUUID() == UUID(0x42));

	{
		const Tests::ScopedLogCapture log;
		CHECK_FALSE(world.CreateEntityWithUUID(UUID(0x42), "Duplicate").IsValid());
		CHECK(log.Contains(LogLevel::Error, "0x0000000000000042 is already used"));
		CHECK_FALSE(world.CreateEntityWithUUID(UUID(), "Null").IsValid());
		CHECK(log.Contains(LogLevel::Error, "null UUID"));
	}
	CHECK(world.GetEntityCount() == 1);
	CHECK(world.TryGetEntity(UUID(0x42)).GetName() == "Fixed");
	CHECK_FALSE(world.TryGetEntity(UUID(0x43)).IsValid());
	CHECK_FALSE(world.TryGetEntity(UUID()).IsValid());
	CheckConsistent(world);
}

TEST_CASE("ECS: components are added, read and removed through the entity")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	Entity entity = world.CreateEntity("Player");

	HealthComponent& health = entity.AddComponent<HealthComponent>(50.0f, 80.0f);
	CHECK(health.Current == 50.0f);
	CHECK(entity.GetComponent<HealthComponent>().Maximum == 80.0f);
	CHECK(entity.TryGetComponent<HealthComponent>() == &health);
	CHECK(std::as_const(entity).TryGetComponent<TransformComponent>() == nullptr);

	entity.AddOrReplaceComponent<HealthComponent>(10.0f, 20.0f);
	CHECK(entity.GetComponent<HealthComponent>().Current == 10.0f);

	entity.AddComponent<PlayerTagComponent>();
	CHECK(entity.HasComponent<PlayerTagComponent>());
	entity.RemoveComponent<PlayerTagComponent>();
	CHECK_FALSE(entity.HasComponent<PlayerTagComponent>());
	entity.RemoveComponent<HealthComponent>();
	CHECK(entity.TryGetComponent<HealthComponent>() == nullptr);
}

TEST_CASE("ECS: destroying entities keeps the UUID map consistent")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	std::vector<Entity> entities;
	for (int i = 0; i < 20; ++i)
	{
		entities.push_back(world.CreateEntity(std::to_string(i)));
		entities.back().AddComponent<HealthComponent>();
	}
	CheckConsistent(world);

	for (int i = 0; i < 20; i += 3)
	{
		const UUID uuid = entities[static_cast<size_t>(i)].GetUUID();
		world.DestroyEntity(entities[static_cast<size_t>(i)]);
		CHECK_FALSE(entities[static_cast<size_t>(i)].IsValid());
		CHECK_FALSE(world.ContainsEntity(uuid));
		CHECK_FALSE(world.TryGetEntity(uuid).IsValid());
	}
	CHECK(world.GetEntityCount() == 13);
	CheckConsistent(world);

	// Recycled handles do not revive old ones, and new entities get new UUIDs.
	Entity fresh = world.CreateEntity("fresh");
	CHECK(fresh.IsValid());
	CHECK_FALSE(entities[0].IsValid());
	CHECK(world.GetEntityCount() == 14);
	CheckConsistent(world);
}

TEST_CASE("ECS: FindEntityByName prefers the smallest UUID among equal names")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	REQUIRE(world.CreateEntityWithUUID(UUID(30), "Enemy").IsValid());
	REQUIRE(world.CreateEntityWithUUID(UUID(10), "Enemy").IsValid());
	REQUIRE(world.CreateEntityWithUUID(UUID(20), "Enemy").IsValid());
	REQUIRE(world.CreateEntityWithUUID(UUID(5), "Player").IsValid());

	CHECK(world.FindEntityByName("Enemy").GetUUID() == UUID(10));
	CHECK(world.FindEntityByName("Player").GetUUID() == UUID(5));
	CHECK_FALSE(world.FindEntityByName("enemy").IsValid());
	CHECK_FALSE(world.FindEntityByName("").IsValid());
	world.DestroyEntity(world.TryGetEntity(UUID(10)));
	CHECK(world.FindEntityByName("Enemy").GetUUID() == UUID(20));
}

TEST_CASE("ECS: UUIDs are drawn from the world random generator")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World a(registry);
	World b(registry);
	a.SetSeed(1234);
	b.SetSeed(1234);
	CHECK(a.GetSeed() == 1234);

	std::set<UUID> seen;
	for (int i = 0; i < 100; ++i)
	{
		const UUID fromA = a.CreateEntity().GetUUID();
		CHECK(fromA == b.CreateEntity().GetUUID());
		CHECK(fromA.IsValid());
		CHECK(seen.insert(fromA).second);
	}
	CHECK(a.GetRandom().GetState() == b.GetRandom().GetState());

	// The draw is exactly one NextU64 of the world generator.
	Random reference(99);
	World c(registry);
	c.SetSeed(99);
	CHECK(c.GenerateUUID() == UUID(reference.NextU64()));

	World other(registry);
	other.SetSeed(1235);
	CHECK(other.CreateEntity().GetUUID() != World(registry).CreateEntity().GetUUID());
}

TEST_CASE("ECS: events are double-buffered: View is the previous tick, ViewCurrent the current one")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);

	EventQueue<Hit>& hits = world.Events<Hit>();
	CHECK(&hits == &world.Events<Hit>());
	CHECK(hits.View().empty());
	CHECK(hits.ViewCurrent().empty());

	// Emitted between ticks: part of the next tick's View.
	hits.Emit({ UUID(1), 5.0f });
	hits.Emplace(UUID(2), 7.5f);
	CHECK(hits.View().empty());
	REQUIRE(hits.ViewCurrent().size() == 2);
	CHECK(hits.ViewCurrent()[0].Entity == UUID(1));
	CHECK(hits.ViewCurrent()[1].Damage == 7.5f);

	world.Tick(TimeStep(1.0f / 60.0f));
	REQUIRE(hits.View().size() == 2);
	CHECK(hits.View()[0].Entity == UUID(1));
	CHECK(hits.View()[1].Entity == UUID(2));
	CHECK(hits.ViewCurrent().empty());

	// Emitting does not disturb the previous tick's events.
	hits.Emit({ UUID(3), 1.0f });
	CHECK(hits.View().size() == 2);
	CHECK(hits.ViewCurrent().size() == 1);

	// Edit ticks are ticks too: they swap.
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	REQUIRE(hits.View().size() == 1);
	CHECK(hits.View()[0].Entity == UUID(3));
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(hits.View().empty());
	CHECK(hits.ViewCurrent().empty());

	hits.Emit({ UUID(4), 1.0f });
	world.Tick(TimeStep(1.0f / 60.0f));
	hits.Emit({ UUID(5), 1.0f });
	world.ClearEvents(); // both buffers
	CHECK(hits.View().empty());
	CHECK(hits.ViewCurrent().empty());
}

TEST_CASE("ECS: every system sees each event exactly once, whatever its phase")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	SystemScheduler& scheduler = world.GetScheduler();
	REQUIRE(scheduler.AddSystem(CreateScope<HitEmitter>()).HasValue());
	auto before = CreateScope<HitReader>("Before", SystemPhase::Input, false);
	auto after = CreateScope<HitReader>("After", SystemPhase::LateUpdate, false);
	auto sameTick = CreateScope<HitReader>("SameTick", SystemPhase::PostPhysics, true);
	const HitReader& beforeRef = *before;
	const HitReader& afterRef = *after;
	const HitReader& sameTickRef = *sameTick;
	REQUIRE(scheduler.AddSystem(std::move(before)).HasValue());
	REQUIRE(scheduler.AddSystem(std::move(after)).HasValue());
	REQUIRE(scheduler.AddSystem(std::move(sameTick)).HasValue());

	for (int tick = 0; tick < 4; ++tick)
	{
		world.Tick(TimeStep(1.0f / 60.0f));
	}
	// Readers of View get ticks 1-3 one tick late, before and after the emitter alike; tick 4's events are pending.
	CHECK(beforeRef.Seen == std::vector<float>{ 1.0f, 2.0f, 3.0f });
	CHECK(afterRef.Seen == std::vector<float>{ 1.0f, 2.0f, 3.0f });
	// A reader of ViewCurrent after the emitter gets each event in its own tick.
	CHECK(sameTickRef.Seen == std::vector<float>{ 1.0f, 2.0f, 3.0f, 4.0f });
	CHECK(world.Events<Hit>().ViewCurrent().size() == 1);
}

TEST_CASE("ECS: ticks advance the tick counter and simulated time in play mode only")
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	World world(registry);
	CHECK(world.GetTick() == 0);
	CHECK(world.GetTime() == 0.0);

	world.Tick(TimeStep(0.25f), SimulationMode::Play);
	world.Tick(TimeStep(0.25f), SimulationMode::Play);
	CHECK(world.GetTick() == 2);
	CHECK(world.GetTime() == 0.5);
	CHECK(world.GetDeltaTime() == 0.25f);

	world.Tick(TimeStep(0.25f), SimulationMode::Edit);
	CHECK(world.GetTick() == 2);
	CHECK(world.GetTime() == 0.5);
}

TEST_CASE("ECS: a default world uses the process-wide registry")
{
	World world;
	CHECK(&world.GetTypeRegistry() == &TypeRegistry::Get());
	Entity entity = world.CreateEntity("Default");
	entity.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	CHECK(TypeRegistry::Get().GetComponent<TransformComponent>().Has(entity));
	CHECK(world.ComputeStateHash() != World().ComputeStateHash());
}
