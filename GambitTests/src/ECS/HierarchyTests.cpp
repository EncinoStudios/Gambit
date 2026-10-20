#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestSupport.h"

#include "Gambit/ECS/Entity.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Math/DMath.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::vector<std::string> NamesOf(const std::vector<Entity>& entities)
	{
		std::vector<std::string> names;
		for (const Entity entity : entities)
		{
			names.push_back(entity.GetName());
		}
		return names;
	}
}

TEST_CASE("ECS: SetParent builds an ordered hierarchy")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity root = world.CreateEntity("Root");
	Entity a = world.CreateEntity("A");
	Entity b = world.CreateEntity("B");
	Entity c = world.CreateEntity("C");
	CHECK(NamesOf(world.GetRoots()) == std::vector<std::string>{ "Root", "A", "B", "C" });

	REQUIRE(world.SetParent(b, root).HasValue());
	REQUIRE(world.SetParent(a, root).HasValue());
	REQUIRE(world.SetParent(c, a).HasValue());
	CHECK(NamesOf(world.GetChildren(root)) == std::vector<std::string>{ "B", "A" }); // sibling order = attach order
	CHECK(NamesOf(world.GetChildren(a)) == std::vector<std::string>{ "C" });
	CHECK(NamesOf(world.GetRoots()) == std::vector<std::string>{ "Root" });
	CHECK(world.GetParent(c) == a);
	CHECK(world.GetParent(a) == root);
	CHECK_FALSE(world.GetParent(root).IsValid());
	CHECK(c.GetComponent<RelationshipComponent>().Parent == a.GetUUID());
	CHECK(root.GetComponent<RelationshipComponent>().Children == std::vector<UUID>{ b.GetUUID(), a.GetUUID() });

	CHECK(world.IsAncestorOf(root, c));
	CHECK(world.IsAncestorOf(a, c));
	CHECK_FALSE(world.IsAncestorOf(c, root));
	CHECK_FALSE(world.IsAncestorOf(b, c));
	CHECK_FALSE(world.IsAncestorOf(c, c));

	// Reparenting to the current parent changes nothing (no reordering).
	REQUIRE(world.SetParent(b, root).HasValue());
	CHECK(NamesOf(world.GetChildren(root)) == std::vector<std::string>{ "B", "A" });

	// Moving a subtree and back to the roots: roots keep the order in which they became roots.
	REQUIRE(world.SetParent(a, b).HasValue());
	CHECK(NamesOf(world.GetChildren(root)) == std::vector<std::string>{ "B" });
	CHECK(world.IsAncestorOf(b, c));
	REQUIRE(world.SetParent(b, Entity()).HasValue());
	REQUIRE(world.SetParent(c, Entity()).HasValue());
	CHECK(NamesOf(world.GetRoots()) == std::vector<std::string>{ "Root", "B", "C" });
	CHECK(world.GetChildren(root).empty());
	CHECK(c.GetComponent<RelationshipComponent>().Parent == UUID());
}

TEST_CASE("ECS: SetParent rejects cycles")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity a = world.CreateEntity("A");
	Entity b = world.CreateEntity("B");
	Entity c = world.CreateEntity("C");
	REQUIRE(world.SetParent(b, a).HasValue());
	REQUIRE(world.SetParent(c, b).HasValue());

	const Result<void> self = world.SetParent(a, a);
	REQUIRE_FALSE(self.HasValue());
	CHECK(self.GetError().Message.find("cycle") != std::string::npos);
	CHECK_FALSE(world.SetParent(a, c).HasValue()); // c is below a
	CHECK_FALSE(world.SetParent(a, b).HasValue());
	CHECK_FALSE(world.SetParent(b, c).HasValue());

	// Nothing changed.
	CHECK(world.GetParent(b) == a);
	CHECK(world.GetParent(c) == b);
	CHECK_FALSE(world.GetParent(a).IsValid());
	CHECK(NamesOf(world.GetRoots()) == std::vector<std::string>{ "A" });
}

TEST_CASE("ECS: destroying an entity destroys its subtree and detaches it")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity root = world.CreateEntity("Root");
	Entity keep = world.CreateEntity("Keep");
	Entity doomed = world.CreateEntity("Doomed");
	Entity child = world.CreateEntity("Child");
	Entity grandchild = world.CreateEntity("Grandchild");
	Entity other = world.CreateEntity("Other");
	REQUIRE(world.SetParent(keep, root).HasValue());
	REQUIRE(world.SetParent(doomed, root).HasValue());
	REQUIRE(world.SetParent(child, doomed).HasValue());
	REQUIRE(world.SetParent(grandchild, child).HasValue());
	const UUID grandchildUUID = grandchild.GetUUID();

	world.DestroyEntity(doomed);
	CHECK_FALSE(doomed.IsValid());
	CHECK_FALSE(child.IsValid());
	CHECK_FALSE(grandchild.IsValid());
	CHECK_FALSE(world.ContainsEntity(grandchildUUID));
	CHECK(world.GetEntityCount() == 3);
	CHECK(NamesOf(world.GetChildren(root)) == std::vector<std::string>{ "Keep" });

	world.DestroyEntity(root);
	CHECK(world.GetEntityCount() == 1);
	CHECK(NamesOf(world.GetRoots()) == std::vector<std::string>{ "Other" });
	CHECK(other.IsValid());
}

TEST_CASE("ECS: SetParent can keep the world transform")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity parent = world.CreateEntity("Parent");
	auto& parentTransform = parent.AddComponent<TransformComponent>();
	parentTransform.Translation = { 10.0f, 0.0f, 0.0f };
	parentTransform.Rotation = DMath::AngleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
	parentTransform.Scale = glm::vec3(2.0f);

	Entity child = world.CreateEntity("Child");
	child.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	const glm::mat4 before = world.ComputeWorldTransform(child);

	REQUIRE(world.SetParent(child, parent, true).HasValue());
	CHECK(NearlyEqual(world.ComputeWorldTransform(child), before, 1e-5f));
	const TransformComponent& local = child.GetComponent<TransformComponent>();
	CHECK(NearlyEqual(local.Scale, glm::vec3(0.5f), 1e-5f));

	// Without keepWorldTransform the local transform is kept instead.
	Entity other = world.CreateEntity("Other");
	other.AddComponent<TransformComponent>().Translation = { 1.0f, 0.0f, 0.0f };
	REQUIRE(world.SetParent(other, parent).HasValue());
	CHECK(other.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 0.0f, 0.0f));
	CHECK(NearlyEqual(glm::vec3(world.ComputeWorldTransform(other)[3]), glm::vec3(10.0f, 0.0f, -2.0f), 1e-5f));

	// And back to the root, keeping the world transform again.
	const glm::mat4 underParent = world.ComputeWorldTransform(child);
	REQUIRE(world.SetParent(child, Entity(), true).HasValue());
	CHECK(NearlyEqual(world.ComputeWorldTransform(child), underParent, 1e-5f));
}

TEST_CASE("ECS: keeping the world transform under a degenerate parent keeps the local transform and warns")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity flat = world.CreateEntity("Flat");
	flat.AddComponent<TransformComponent>().Scale = glm::vec3(1.0f, 0.0f, 1.0f); // not invertible
	Entity child = world.CreateEntity("Child");
	child.AddComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };

	const Tests::ScopedLogCapture log;
	REQUIRE(world.SetParent(child, flat, true).HasValue());
	CHECK(world.GetParent(child) == flat);
	CHECK(child.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(log.Contains(LogLevel::Warn, "cannot be kept"));
}

TEST_CASE("ECS: a disabled entity makes its whole subtree inactive")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	Entity root = world.CreateEntity("Root");
	Entity child = world.CreateEntity("Child");
	Entity grandchild = world.CreateEntity("Grandchild");
	Entity loner = world.CreateEntity("Loner");
	REQUIRE(world.SetParent(child, root).HasValue());
	REQUIRE(world.SetParent(grandchild, child).HasValue());
	for (Entity entity : { root, child, grandchild, loner })
	{
		CHECK(world.IsActive(entity));
		CHECK(world.IsEnabled(entity));
	}

	world.SetEnabled(root, false);
	CHECK_FALSE(world.IsEnabled(root));
	CHECK(world.IsEnabled(child)); // enabled itself...
	for (Entity entity : { root, child, grandchild })
	{
		CHECK_FALSE(world.IsActive(entity)); // ...but inactive through its ancestor
		CHECK(entity.HasComponent<InactiveComponent>());
	}
	CHECK(world.IsActive(loner));

	// Disabling a descendant too: re-enabling the root leaves that subtree inactive.
	world.SetEnabled(child, false);
	world.SetEnabled(root, true);
	CHECK(world.IsActive(root));
	CHECK_FALSE(world.IsActive(child));
	CHECK_FALSE(world.IsActive(grandchild));

	// Moving entities in and out of inactive subtrees updates them.
	REQUIRE(world.SetParent(grandchild, root).HasValue());
	CHECK(world.IsActive(grandchild));
	REQUIRE(world.SetParent(loner, child).HasValue());
	CHECK_FALSE(world.IsActive(loner));

	// The generic component operations (tools, scripts) keep the state consistent too.
	const ComponentInfo& disabled = fixture.Registry.GetComponent<DisabledComponent>();
	CHECK(disabled.Remove(child));
	CHECK(world.IsActive(child));
	CHECK(world.IsActive(loner));
	CHECK(disabled.Add(root) != nullptr);
	CHECK_FALSE(world.IsActive(grandchild));
	CHECK_FALSE(world.IsActive(loner));

	// World::View skips inactive entities.
	for (Entity entity : { root, child, grandchild, loner })
	{
		entity.AddComponent<HealthComponent>();
	}
	Entity active = world.CreateEntity("Active");
	active.AddComponent<HealthComponent>();
	std::vector<std::string> visited;
	for (const entt::entity handle : world.View<HealthComponent>())
	{
		visited.push_back(Entity(handle, &world).GetName());
	}
	CHECK(visited == std::vector<std::string>{ "Active" });
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
