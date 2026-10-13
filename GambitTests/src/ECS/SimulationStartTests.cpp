#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/Reflection/FieldHashing.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct Ping
	{
		uint32_t Value = 0;
	};

	void HashEvent(Hasher& hasher, const Ping& ping)
	{
		HashEventFields(hasher, ping.Value);
	}

	// Randomizes health every tick and emits an event: its history depends on the state of the world RNG.
	class JitterSystem final : public ISystem
	{
	public:
		std::string_view GetName() const override { return "JitterSystem"; }
		SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnUpdate(World& world, TimeStep) override
		{
			for (auto [entity, health] : world.View<HealthComponent>().each())
			{
				health.Current = world.GetRandom().NextFloat01() * 100.0f;
			}
			world.Events<Ping>().Emit({ world.GetRandom().NextU32() });
		}
	};

	void AddSystems(World& world)
	{
		REQUIRE(world.GetScheduler().AddSystem(CreateScope<JitterSystem>()).HasValue());
		REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	}

	std::vector<uint64_t> Run(World& world, int ticks)
	{
		std::vector<uint64_t> hashes;
		for (int tick = 0; tick < ticks; ++tick)
		{
			world.Tick(TimeStep(1.0f / 60.0f));
			hashes.push_back(world.ComputeStateHash());
		}
		return hashes;
	}
}

TEST_CASE("ECS: BeginSimulation restarts the clock, the random generator and the events")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.CreateEntityWithUUID(UUID(1), "A").AddComponent<HealthComponent>();
	AddSystems(world);
	world.SetSeed(99);
	const uint64_t initial = world.ComputeStateHash();

	Run(world, 5);
	REQUIRE(world.GetTick() == 5);
	REQUIRE(world.GetTime() > 0.0);
	REQUIRE(world.ComputeStateHash() != initial);

	world.BeginSimulation(99);
	CHECK(world.GetTick() == 0);
	CHECK(world.GetTime() == 0.0);
	CHECK(world.GetDeltaTime() == 0.0f);
	CHECK(world.GetSeed() == 99);
	CHECK(world.Events<Ping>().View().empty());
	CHECK(world.Events<Ping>().ViewCurrent().empty());

	// Health was randomized by the run, so the hash is not the initial one, but the clock, generator and events are.
	TestWorld other;
	other.Scene->CreateEntityWithUUID(UUID(1), "A").AddComponent<HealthComponent>();
	other.Scene->SetSeed(99);
	world.GetRegistry().get<HealthComponent>(world.TryGetEntity(UUID(1)).GetHandle()) = HealthComponent{};
	CHECK(world.ComputeStateHash() == other.Scene->ComputeStateHash());

	world.BeginSimulation(7);
	CHECK(world.GetSeed() == 7);
}

TEST_CASE("ECS: playing a copy of an edited scene gives the hashes of the scene loaded from its file")
{
	// An editor world creates entities with CreateEntity, which draws UUIDs from the world's random generator; a file
	// stores only the seed. Without BeginSimulation the two worlds would draw different random numbers.
	TestWorld editor;
	World& edited = *editor.Scene;
	edited.SetSeed(2024);
	for (int index = 0; index < 5; ++index)
	{
		Entity entity = edited.CreateEntity("Unit " + std::to_string(index));
		entity.AddComponent<HealthComponent>(50.0f + static_cast<float>(index), 100.0f);
		entity.AddComponent<TransformComponent>().Translation.x = static_cast<float>(index);
	}
	REQUIRE(edited.GetEntityCount() == 5);

	TempDirectory directory;
	SceneMetadata metadata;
	metadata.Name = "Arena";
	REQUIRE(SceneSerializer::Save(edited, metadata, directory.GetVFS(), "Scenes/Arena.gscene").HasValue());

	TestWorld fromFile;
	LoadReport report;
	REQUIRE(SceneSerializer::Load(*fromFile.Scene, directory.GetVFS(), "Scenes/Arena.gscene", report).HasValue());
	AddSystems(*fromFile.Scene);
	fromFile.Scene->BeginSimulation(fromFile.Scene->GetSeed());

	Scope<World> played = World::Copy(edited);
	AddSystems(*played);
	played->BeginSimulation(edited.GetSeed());

	// Both start in the same state ...
	REQUIRE(played->ComputeStateHash() == fromFile.Scene->ComputeStateHash());
	// ... and stay identical.
	const std::vector<uint64_t> playedHashes = Run(*played, 120);
	const std::vector<uint64_t> loadedHashes = Run(*fromFile.Scene, 120);
	CHECK(playedHashes == loadedHashes);
	CHECK(playedHashes.front() != playedHashes.back());

	// The control: a copy that did not call BeginSimulation starts from the editing world's random generator state and
	// diverges (the five CreateEntity calls consumed random numbers).
	Scope<World> careless = World::Copy(edited);
	AddSystems(*careless);
	careless->Tick(TimeStep(1.0f / 60.0f));
	CHECK(careless->ComputeStateHash() != loadedHashes.front());
}

TEST_CASE("ECS: SetSiblingIndex moves children and roots")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity parent = world.CreateEntityWithUUID(UUID(1), "Parent");
	Entity a = world.CreateEntityWithUUID(UUID(2), "A");
	Entity b = world.CreateEntityWithUUID(UUID(3), "B");
	Entity c = world.CreateEntityWithUUID(UUID(4), "C");
	Entity other = world.CreateEntityWithUUID(UUID(5), "Other");
	for (Entity child : { a, b, c })
	{
		REQUIRE(world.SetParent(child, parent).HasValue());
	}
	const auto children = [&] {
		std::string names;
		for (const Entity child : world.GetChildren(parent))
		{
			names += child.GetName();
		}
		return names;
	};
	CHECK(children() == "ABC");
	CHECK(world.GetSiblingIndex(a) == 0);
	CHECK(world.GetSiblingIndex(c) == 2);

	world.SetSiblingIndex(c, 0);
	CHECK(children() == "CAB");
	world.SetSiblingIndex(c, 1);
	CHECK(children() == "ACB");
	world.SetSiblingIndex(a, 99); // beyond the last position: to the end
	CHECK(children() == "CBA");
	CHECK(world.GetSiblingIndex(a) == 2);
	world.SetSiblingIndex(b, 1); // already there
	CHECK(children() == "CBA");

	// Roots: Parent, Other.
	const auto roots = [&] {
		std::string names;
		for (const Entity root : world.GetRoots())
		{
			names += root.GetName() + ",";
		}
		return names;
	};
	CHECK(roots() == "Parent,Other,");
	world.SetSiblingIndex(other, 0);
	CHECK(roots() == "Other,Parent,");
	CHECK(world.GetSiblingIndex(other) == 0);
	CHECK(world.GetSiblingIndex(parent) == 1);
	world.SetSiblingIndex(other, 5);
	CHECK(roots() == "Parent,Other,");

	// Detaching and attaching still works after the roots were reordered.
	REQUIRE(world.SetParent(other, parent).HasValue());
	CHECK(roots() == "Parent,");
	REQUIRE(world.SetParent(other, Entity()).HasValue());
	CHECK(roots() == "Parent,Other,");

	// Sibling order is part of the state hash of children.
	TestWorld second;
	World& twin = *second.Scene;
	Entity twinParent = twin.CreateEntityWithUUID(parent.GetUUID(), "Parent");
	for (const Entity child : world.GetChildren(parent))
	{
		Entity copy = twin.CreateEntityWithUUID(child.GetUUID(), child.GetName());
		REQUIRE(twin.SetParent(copy, twinParent).HasValue());
	}
	REQUIRE(twin.CreateEntityWithUUID(other.GetUUID(), "Other").IsValid());
	CHECK(twin.ComputeStateHash() == world.ComputeStateHash());
	twin.SetSiblingIndex(twin.GetChildren(twinParent).front(), 2);
	CHECK(twin.ComputeStateHash() != world.ComputeStateHash());
}
