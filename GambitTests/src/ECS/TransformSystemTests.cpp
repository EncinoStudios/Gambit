#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/Math/DMath.h"

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	glm::vec3 WorldPosition(Entity entity)
	{
		return glm::vec3(entity.GetComponent<WorldTransformComponent>().Matrix[3]);
	}
}

TEST_CASE("ECS: the TransformSystem propagates transforms down the hierarchy")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());

	Entity root = world.CreateEntity("Root");
	auto& rootTransform = root.AddComponent<TransformComponent>();
	rootTransform.Translation = { 0.0f, 10.0f, 0.0f };
	rootTransform.Rotation = DMath::AngleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
	Entity arm = world.CreateEntity("Arm");
	arm.AddComponent<TransformComponent>().Translation = { 2.0f, 0.0f, 0.0f };
	Entity hand = world.CreateEntity("Hand");
	auto& handTransform = hand.AddComponent<TransformComponent>();
	handTransform.Translation = { 1.0f, 0.0f, 0.0f };
	handTransform.Scale = glm::vec3(3.0f);
	REQUIRE(world.SetParent(arm, root).HasValue());
	REQUIRE(world.SetParent(hand, arm).HasValue());

	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(NearlyEqual(WorldPosition(root), glm::vec3(0.0f, 10.0f, 0.0f)));
	CHECK(NearlyEqual(WorldPosition(arm), glm::vec3(0.0f, 12.0f, 0.0f)));
	CHECK(NearlyEqual(WorldPosition(hand), glm::vec3(0.0f, 13.0f, 0.0f)));
	for (Entity entity : { root, arm, hand })
	{
		// Bit-identical to the on-demand computation (same products in the same order).
		CHECK(entity.GetComponent<WorldTransformComponent>().Matrix == world.ComputeWorldTransform(entity));
	}

	// Moving the root moves the subtree on the next tick; it also runs in edit mode.
	root.GetComponent<TransformComponent>().Translation = { 5.0f, 0.0f, 0.0f };
	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	CHECK(NearlyEqual(WorldPosition(hand), glm::vec3(5.0f, 3.0f, 0.0f)));
}

TEST_CASE("ECS: entities without a transform pass their parent's transform through")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	REQUIRE(world.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());

	Entity root = world.CreateEntity("Root");
	root.AddComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 0.0f };
	Entity group = world.CreateEntity("Group"); // no transform
	Entity leaf = world.CreateEntity("Leaf");
	leaf.AddComponent<TransformComponent>().Translation = { 0.0f, 1.0f, 0.0f };
	REQUIRE(world.SetParent(group, root).HasValue());
	REQUIRE(world.SetParent(leaf, group).HasValue());
	Entity inactive = world.CreateEntity("Inactive");
	inactive.AddComponent<TransformComponent>().Translation = { 0.0f, 0.0f, 4.0f };
	REQUIRE(world.SetParent(inactive, root).HasValue());
	world.SetEnabled(inactive, false);

	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK_FALSE(group.HasComponent<WorldTransformComponent>());
	CHECK(WorldPosition(leaf) == glm::vec3(1.0f, 1.0f, 0.0f));
	CHECK(WorldPosition(inactive) == glm::vec3(1.0f, 0.0f, 4.0f)); // kept up to date while inactive

	// Losing the transform drops the derived matrix.
	leaf.RemoveComponent<TransformComponent>();
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK_FALSE(leaf.HasComponent<WorldTransformComponent>());
}

TEST_CASE("ECS: TransformComponent helpers")
{
	TransformComponent transform;
	CHECK(transform.GetMatrix() == glm::mat4(1.0f));
	transform.Translation = { 1.0f, 2.0f, 3.0f };
	transform.Scale = { 2.0f, 2.0f, 2.0f };
	const glm::mat4 matrix = transform.GetMatrix();
	CHECK(glm::vec3(matrix[3]) == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(matrix[0][0] == 2.0f);
}
