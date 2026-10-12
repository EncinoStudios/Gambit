#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/Serialization/SceneSerializer.h"

#include <string>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Serialization: a large scene round-trips" * doctest::test_suite("slow"))
{
	// A wide tree (every entity has up to 8 children) and a deep chain: no step may depend on the depth or recurse.
	TestWorld source;
	World& world = *source.Scene;
	world.SetSeed(5);
	std::vector<Entity> entities;
	constexpr size_t Count = 5000;
	for (size_t index = 0; index < Count; ++index)
	{
		Entity entity = world.CreateEntityWithUUID(UUID(index + 1), "E" + std::to_string(index));
		entity.AddComponent<TransformComponent>().Translation = { static_cast<float>(index), 0.5f, -1.0f };
		entity.AddComponent<HealthComponent>(static_cast<float>(index % 100), 100.0f);
		if (index > 0)
		{
			REQUIRE(world.SetParent(entity, entities[(index - 1) / 8]).HasValue());
		}
		entities.push_back(entity);
	}
	Entity previous = world.CreateEntityWithUUID(UUID(Count + 1), "ChainRoot");
	for (size_t depth = 0; depth < 1000; ++depth)
	{
		Entity link = world.CreateEntityWithUUID(UUID(Count + 2 + depth), "Link");
		REQUIRE(world.SetParent(link, previous).HasValue());
		previous = link;
	}

	const Result<std::string> text = SceneSerializer::Serialize(world, {});
	REQUIRE(text.HasValue());
	TestWorld target;
	LoadReport report;
	REQUIRE(SceneSerializer::Deserialize(*target.Scene, *text, "large.gscene", report).HasValue());
	CHECK(report.IsEmpty());
	CHECK(target.Scene->GetEntityCount() == world.GetEntityCount());
	CHECK(target.Scene->ComputeStateHash() == world.ComputeStateHash());
	CHECK(*SceneSerializer::Serialize(*target.Scene, {}) == *text);
}
