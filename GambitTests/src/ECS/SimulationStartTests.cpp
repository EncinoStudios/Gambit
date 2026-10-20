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
	fromFile.Scene->BeginSimulation(fromFile.Scene->GetSeed());
	AddSystems(*fromFile.Scene);

	// Editing also leaves commands in the buffer (a host that queued some, a RunsInEditMode system): a file stores
	// none.
	static_cast<void>(edited.GetCommandBuffer().CreateEntity("Pending"));
	Scope<World> played = World::Copy(edited);
	REQUIRE(played->GetCommandBuffer().GetCommandCount() == 1); // copied with the world
	played->BeginSimulation(edited.GetSeed());
	CHECK(played->GetCommandBuffer().IsEmpty());
	AddSystems(*played);

	// Both start in the same state ...
	REQUIRE(played->ComputeStateHash() == fromFile.Scene->ComputeStateHash());
	// ... and stay identical.
	const std::vector<uint64_t> playedHashes = Run(*played, 120);
	const std::vector<uint64_t> loadedHashes = Run(*fromFile.Scene, 120);
	CHECK(playedHashes == loadedHashes);
	CHECK(playedHashes.front() != playedHashes.back());

	CHECK_FALSE(played->FindEntityByName("Pending").IsValid());

	// The control: a copy that did not call BeginSimulation starts from the editing world's random generator state and
	// diverges (the five CreateEntity calls consumed random numbers), and spawns what was pending.
	Scope<World> careless = World::Copy(edited);
	AddSystems(*careless);
	careless->Tick(TimeStep(1.0f / 60.0f));
	CHECK(careless->ComputeStateHash() != loadedHashes.front());
	CHECK(careless->FindEntityByName("Pending").IsValid());
}

TEST_CASE("ECS: BeginSimulation drops pending commands and their reserved UUIDs")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.SetSeed(5);
	const UUID pending = world.GetCommandBuffer().CreateEntity("Pending");
	REQUIRE(world.GetCommandBuffer().GetCommandCount() == 1);

	world.BeginSimulation(5);
	CHECK(world.GetCommandBuffer().IsEmpty());
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK_FALSE(world.ContainsEntity(pending));
	CHECK(world.GetEntityCount() == 0);
	// The generator restarted, so the first new UUID is the one a fresh world with the seed draws first: the UUID the
	// dropped command had reserved is free again.
	TestWorld fresh;
	fresh.Scene->SetSeed(5);
	CHECK(world.CreateEntity("Next").GetUUID() == fresh.Scene->CreateEntity("Next").GetUUID());
}
