#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Math/DMath.h"

#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct SpawnedEvent
	{
		UUID Entity;
		UUID Parent;
	};

	void HashEvent(Hasher& hasher, const SpawnedEvent& event)
	{
		HashEventFields(hasher, event.Entity, event.Parent);
	}

	// Gameplay-like churn driven only by the world RNG: damages and moves active entities in storage order, destroys
	// the dead, spawns new entities (some as children, some disabled, some with a script component), re-enables
	// disabled ones, emits events and reacts to the previous tick's. Every structural change goes through the command
	// buffer.
	class ChaosSystem final : public ISystem
	{
	public:
		explicit ChaosSystem(const ComponentInfo& script)
			: m_Script(script)
		{}

		[[nodiscard]] std::string_view GetName() const override { return "Chaos"; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnUpdate(World& world, TimeStep timeStep) override
		{
			Random& random = world.GetRandom();
			CommandBuffer& commands = world.GetCommandBuffer();
			const float seconds = timeStep.GetSeconds();

			// The previous tick's spawns get a random bonus: the outcome depends on events crossing a tick boundary.
			for (const SpawnedEvent& spawned : world.Events<SpawnedEvent>().View())
			{
				if (Entity entity = world.TryGetEntity(spawned.Entity))
				{
					if (auto* health = entity.TryGetComponent<HealthComponent>())
					{
						health->Current += random.RangeFloat(0.0f, 10.0f);
					}
				}
			}

			for (const auto [handle, id, health, transform] :
			     world.View<IDComponent, HealthComponent, TransformComponent>().each())
			{
				health.Current -= random.RangeFloat(0.0f, 6.0f);
				transform.Translation += random.InsideUnitSphere() * seconds;
				transform.Rotation =
					DMath::Normalize(transform.Rotation * DMath::AngleAxis(seconds, glm::vec3(0.0f, 1.0f, 0.0f)));
				if (health.Current <= 0.0f)
				{
					commands.DestroyEntity(id.ID);
				}
			}

			for (const auto [handle, id] : world.GetRegistry().view<IDComponent, DisabledComponent>().each())
			{
				if (random.NextBool(0.1f))
				{
					commands.Execute(id.ID, [](Entity entity) { entity.GetWorld()->SetEnabled(entity, true); });
				}
			}

			const std::vector<UUID> existing = world.GetEntityUUIDs();
			const int32_t spawns = random.RangeInt(0, 3);
			for (int32_t spawn = 0; spawn < spawns; ++spawn)
			{
				UUID parent;
				if (!existing.empty() && random.NextBool(0.3f))
				{
					parent = existing[random.NextBounded(static_cast<uint32_t>(existing.size()))];
				}
				const UUID uuid = commands.CreateEntity("Spawn", parent);
				TransformComponent transform;
				transform.Translation = random.InsideUnitSphere() * 10.0f;
				commands.AddComponent<TransformComponent>(uuid, transform);
				commands.AddComponent<HealthComponent>(uuid, { random.RangeFloat(20.0f, 120.0f), 120.0f });
				if (random.NextBool(0.3f))
				{
					commands.AddComponent(uuid, m_Script);
					static_cast<void>(commands.SetField(uuid, m_Script, "Count", random.RangeInt(0, 100)));
					static_cast<void>(commands.SetField(uuid, m_Script, "Axis", random.OnUnitSphere()));
				}
				if (random.NextBool(0.1f))
				{
					commands.AddComponent<DisabledComponent>(uuid);
				}
				world.Events<SpawnedEvent>().Emit({ uuid, parent });
			}
		}

	private:
		const ComponentInfo& m_Script;
	};

	void AddSystems(World& world, const ComponentInfo& script)
	{
		REQUIRE(world.GetScheduler().AddSystem(CreateScope<ChaosSystem>(script)).HasValue());
		REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	}

	Scope<World> MakeWorld(TypeRegistry& registry, const ComponentInfo& script, uint64_t seed)
	{
		auto world = CreateScope<World>(registry);
		world->SetSeed(seed);
		AddSystems(*world, script);
		return world;
	}
}

TEST_CASE("ECS: worlds driven by the world RNG stay identical for 1000 ticks" * doctest::test_suite("slow"))
{
	TypeRegistry registry;
	RegisterTestTypes(registry);
	const ComponentInfo& script = *registry.RegisterDynamicComponent(MakeScriptSchema()).Value();
	const Scope<World> first = MakeWorld(registry, script, 0xC0FFEE);
	const Scope<World> second = MakeWorld(registry, script, 0xC0FFEE);
	const Scope<World> otherSeed = MakeWorld(registry, script, 0xC0FFEF);
	Scope<World> rollback;

	constexpr uint64_t Ticks = 1000;
	constexpr uint64_t CopyAt = 500;
	const TimeStep step(1.0f / 60.0f);
	size_t maximumEntities = 0;
	size_t ticksWithDivergentSeed = 0;
	for (uint64_t tick = 1; tick <= Ticks; ++tick)
	{
		first->Tick(step);
		second->Tick(step);
		otherSeed->Tick(step);
		const uint64_t hash = first->ComputeStateHash();
		REQUIRE_MESSAGE(hash == second->ComputeStateHash(), "tick ", tick);
		ticksWithDivergentSeed += hash != otherSeed->ComputeStateHash() ? 1 : 0;
		maximumEntities = std::max(maximumEntities, first->GetEntityCount());

		if (tick == CopyAt)
		{
			// A copy taken between ticks continues exactly like the original (rollback, play mode).
			rollback = World::Copy(*first);
			AddSystems(*rollback, script);
			REQUIRE(rollback->ComputeStateHash() == hash);
		}
		else if (tick > CopyAt)
		{
			rollback->Tick(step);
			REQUIRE_MESSAGE(rollback->ComputeStateHash() == hash, "tick ", tick);
		}
	}
	CHECK(first->GetTick() == Ticks);
	CHECK(ticksWithDivergentSeed == Ticks);
	// The run exercised real churn.
	CHECK(maximumEntities > 20);
	CHECK(first->GetEntityCount() > 0);
}
