#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/ECS/CommandBuffer.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("ECS: CommandBuffer defers structural changes until it is flushed")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	CommandBuffer& commands = world.GetCommandBuffer();
	CHECK(commands.IsEmpty());

	Entity parent = world.CreateEntity("Parent");
	const UUID created = commands.CreateEntity("Spawned", parent.GetUUID());
	CHECK(created.IsValid());
	CHECK_FALSE(world.ContainsEntity(created)); // not yet
	commands.AddComponent<HealthComponent>(created, { 5.0f, 10.0f });
	commands.AddComponent(created, fixture.Registry.GetComponent<PlayerTagComponent>());
	REQUIRE(commands.SetField(created, fixture.Registry.GetComponent<HealthComponent>(), "Current", 7.0f));
	CHECK(commands.GetCommandCount() == 4);

	const CommandBufferFlushResult result = commands.Flush();
	CHECK(result.Executed == 4);
	CHECK(result.Skipped == 0);
	CHECK(commands.IsEmpty());

	Entity spawned = world.TryGetEntity(created);
	REQUIRE(spawned.IsValid());
	CHECK(spawned.GetName() == "Spawned");
	CHECK(world.GetParent(spawned) == parent);
	CHECK(spawned.GetComponent<HealthComponent>().Current == 7.0f);
	CHECK(spawned.GetComponent<HealthComponent>().Maximum == 10.0f);
	CHECK(spawned.HasComponent<PlayerTagComponent>());

	// Removing and destroying.
	commands.RemoveComponent<PlayerTagComponent>(created);
	commands.RemoveComponent(created, fixture.Registry.GetComponent<HealthComponent>());
	commands.Flush();
	CHECK_FALSE(spawned.HasComponent<PlayerTagComponent>());
	CHECK_FALSE(spawned.HasComponent<HealthComponent>());
	commands.DestroyEntity(parent.GetUUID());
	CHECK(parent.IsValid());
	commands.Flush();
	CHECK_FALSE(parent.IsValid());
	CHECK_FALSE(spawned.IsValid()); // the child went with it
}

TEST_CASE("ECS: CommandBuffer runs commands in recording order and skips those that no longer apply")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	CommandBuffer& commands = world.GetCommandBuffer();
	const ComponentInfo& health = fixture.Registry.GetComponent<HealthComponent>();
	Entity entity = world.CreateEntity("Target");
	const UUID uuid = entity.GetUUID();

	commands.AddComponent<HealthComponent>(uuid, { 1.0f, 1.0f });
	commands.AddComponent<HealthComponent>(uuid, { 2.0f, 2.0f }); // replaces the value
	REQUIRE(commands.SetField(uuid, health, "Current", 3.0f));
	commands.AddComponent(uuid, health); // already there: skipped, value kept
	commands.DestroyEntity(uuid);
	commands.DestroyEntity(uuid);                              // gone already: skipped
	commands.AddComponent<PlayerTagComponent>(uuid);           // skipped
	REQUIRE(commands.SetField(uuid, health, "Maximum", 9.0f)); // skipped
	commands.SetParent(uuid, UUID());                          // skipped
	commands.RemoveComponent(UUID(12345), health);             // never existed: skipped

	std::vector<float> seen;
	commands.Execute(uuid, [&](Entity) { seen.push_back(1.0f); }); // skipped as well
	const CommandBufferFlushResult result = commands.Flush();
	CHECK(result.Executed == 4);
	CHECK(result.Skipped == 7);
	CHECK(seen.empty());
	CHECK_FALSE(world.ContainsEntity(uuid));

	// Invalid field writes are refused when recorded.
	CHECK_FALSE(commands.SetField(uuid, health, "Missing", 1.0f));
	CHECK_FALSE(commands.SetField(uuid, health, "Current", 1.0)); // a double for a float field
	CHECK(commands.IsEmpty());
}

TEST_CASE("ECS: CommandBuffer reserves distinct deterministic UUIDs and refuses engine-managed components")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.SetSeed(77);
	CommandBuffer& commands = world.GetCommandBuffer();

	Random reference(77);
	const UUID first = commands.CreateEntity("A");
	const UUID second = commands.CreateEntity("B");
	CHECK(first == UUID(reference.NextU64()));
	CHECK(second == UUID(reference.NextU64()));
	CHECK(first != second);

	// Reparenting through the buffer, including a cycle that is refused at flush time.
	commands.SetParent(second, first);
	commands.SetParent(first, second);
	commands.AddComponent(first, fixture.Registry.GetComponent<InactiveComponent>()); // Internal: refused
	commands.RemoveComponent(first, fixture.Registry.GetComponent<TagComponent>());   // not Removable
	const CommandBufferFlushResult result = commands.Flush();
	CHECK(result.Executed == 3);
	CHECK(result.Skipped == 3);
	CHECK(world.GetParent(world.TryGetEntity(second)).GetUUID() == first);
	CHECK_FALSE(world.GetParent(world.TryGetEntity(first)).IsValid());
	CHECK(world.TryGetEntity(first).HasComponent<TagComponent>());

	// Clear drops everything recorded.
	commands.CreateEntity("Never");
	commands.Clear();
	CHECK(commands.IsEmpty());
	CHECK(commands.Flush().Executed == 0);
	CHECK(world.GetEntityCount() == 2);
}

TEST_CASE("ECS: an entity created outside the buffer can take a reserved UUID first")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	CommandBuffer& commands = world.GetCommandBuffer();
	const UUID reserved = commands.CreateEntity("Deferred");
	REQUIRE(world.CreateEntityWithUUID(reserved, "Immediate").IsValid());

	const CommandBufferFlushResult result = commands.Flush();
	CHECK(result.Skipped == 1);
	CHECK(world.TryGetEntity(reserved).GetName() == "Immediate");
}

TEST_CASE("ECS: immediate creation never takes a UUID reserved by the command buffer")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	world.SetSeed(5);
	const RandomState before = world.GetRandom().GetState();
	const UUID reserved = world.GetCommandBuffer().CreateEntity("Deferred");

	// Replaying the generator would hand out the reserved UUID again: GenerateUUID skips it.
	REQUIRE(world.GetRandom().SetState(before));
	const Entity immediate = world.CreateEntity("Immediate");
	CHECK(immediate.GetUUID() != reserved);
	CHECK(world.GetCommandBuffer().Flush().Executed == 1);
	CHECK(world.TryGetEntity(reserved).GetName() == "Deferred");
}
