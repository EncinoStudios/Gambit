#include <doctest/doctest.h>

#include "AutomationFixture.h"

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::vector<std::string> Strings(const Json& array)
	{
		std::vector<std::string> strings;
		for (const Json& item : array)
		{
			strings.push_back(item.get<std::string>());
		}
		return strings;
	}

	std::vector<std::string> Names(const Json& entities)
	{
		std::vector<std::string> names;
		for (const Json& entity : entities)
		{
			names.push_back(entity.at("name").get<std::string>());
		}
		return names;
	}

	// The names of the roots, then of each parent's children, as "Parent[A,B]" text: the hierarchy at a glance.
	std::string Shape(World& world)
	{
		std::string text;
		const auto describe = [&](auto&& self, Entity entity) -> void {
			text += entity.GetName();
			const std::vector<Entity> children = world.GetChildren(entity);
			if (!children.empty())
			{
				text += "[";
				for (size_t index = 0; index < children.size(); ++index)
				{
					text += index == 0 ? "" : ",";
					self(self, children[index]);
				}
				text += "]";
			}
		};
		for (const Entity root : world.GetRoots())
		{
			text += text.empty() ? "" : " ";
			describe(describe, root);
		}
		return text;
	}
}

TEST_CASE("Automation: entity.create makes an entity with components and a place in the hierarchy")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	REQUIRE(fixture.Scene().GetEntityCount() == 0);

	const Json crate = fixture.Succeed(
		"entity.create",
		J(R"({"name": "Crate", "components": {"Transform": {"Translation": [1, 2, 3]}, "Health": {"Maximum": 50}, "PlayerTag": {}}})"));
	CHECK(crate.at("name") == "Crate");
	CHECK(crate.at("parent").is_null());
	CHECK(crate.at("index") == 0);
	CHECK(Strings(crate.at("components")) == std::vector<std::string>{ "Health", "PlayerTag", "Tag", "Transform" });

	World& world = fixture.Scene();
	const Entity entity = world.TryGetEntity(*UUID::Parse(crate.at("uuid").get<std::string>()));
	REQUIRE(entity.IsValid());
	CHECK(entity.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(entity.GetComponent<TransformComponent>().Scale == glm::vec3(1.0f)); // not named: the default
	CHECK(entity.GetComponent<HealthComponent>().Maximum == 50.0f);
	CHECK(entity.GetComponent<HealthComponent>().Current == 100.0f);
	CHECK(entity.HasComponent<PlayerTagComponent>());
	CHECK(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));

	// Defaults: a name of "Entity" and a root.
	const Json plain = fixture.Succeed("entity.create");
	CHECK(plain.at("name") == "Entity");
	CHECK(Strings(plain.at("components")) == std::vector<std::string>{ "Tag" });

	// Parent and position; the parent can be named by UUID or by name.
	const Json child = fixture.Succeed("entity.create", J(R"({"name": "Child", "parent": "Crate"})"));
	CHECK(child.at("parent") == crate.at("uuid"));
	fixture.Succeed(
		"entity.create",
		J(R"({"name": "First", "parent": ")" + crate.at("uuid").get<std::string>() + R"(", "index": 0})"));
	CHECK(Shape(world) == "Crate[First,Child] Entity");

	// The name can be given as the Tag's field instead.
	const Json tagged = fixture.Succeed("entity.create", J(R"({"components": {"Tag": {"Name": "ViaTag"}}})"));
	CHECK(tagged.at("name") == "ViaTag");
}

TEST_CASE("Automation: entity.create is undoable")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Existing");
	fixture.CheckUndoRedo([&] {
		fixture.Succeed(
			"entity.create", J(R"({"name": "New", "parent": "Existing", "components": {"Health": {"Current": 3}}})"));
	});
	CHECK(fixture.Scene().GetEntityCount() == 2);
	fixture.Succeed("undo");
	CHECK(fixture.Scene().GetEntityCount() == 1);
	CHECK(fixture.Context.GetUndoStack().GetRedoDescription() == "Create entity 'New'");
}

TEST_CASE("Automation: entity.create rejects what does not fit and creates nothing")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Parent");
	const uint64_t before = fixture.Hash();

	const CommandResult unknown =
		fixture.Reject("entity.create", J(R"({"components": {"Helth": {}}})"), CommandErrorCode::NotFound);
	CHECK(unknown.Message.starts_with("there is no component type 'Helth' (did you mean 'Health'?)"));
	fixture.Reject("entity.create", J(R"({"components": {"Relationship": {}}})"), CommandErrorCode::Conflict);
	fixture.Reject("entity.create", J(R"({"components": {"WorldTransform": {}}})"), CommandErrorCode::Conflict);
	const CommandResult field = fixture.Reject(
		"entity.create", J(R"({"components": {"Health": {"Curent": 5}}})"), CommandErrorCode::InvalidArguments);
	CHECK(field.Message.starts_with("component 'Health' has no field 'Curent' (did you mean 'Current'?)"));
	const CommandResult type = fixture.Reject(
		"entity.create", J(R"({"components": {"Health": {"Current": "full"}}})"), CommandErrorCode::InvalidArguments);
	CHECK(type.Message == "/Health/Current: expected a number, found a string");
	fixture.Reject(
		"entity.create", J(R"({"components": {"AllTypes": {"Textures": []}}})"),
		CommandErrorCode::InvalidArguments); // read-only
	fixture.Reject(
		"entity.create", J(R"({"components": {"AllTypes": {"Cache": 1}}})"),
		CommandErrorCode::InvalidArguments); // runtime state
	fixture.Reject(
		"entity.create", J(R"({"components": {"AllTypes": {"Mode": "Warp"}}})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.create", J(R"({"components": [1]})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"entity.create", J(R"({"name": "A", "components": {"Tag": {"Name": "B"}}})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.create", J(R"({"name": ""})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.create", J(R"({"parent": "Nobody"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.create", J(R"({"parent": "0x1234"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.create", J(R"({"parent": "0xZZ"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.create", J(R"({"index": -1})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.create", J(R"({"colour": 1})"), CommandErrorCode::InvalidArguments);
	CHECK(fixture.Hash() == before);
	CHECK(fixture.Context.GetUndoStack().GetUndoHistory().size() == 1); // only the creation of "Parent"
}

TEST_CASE("Automation: entities are found by UUID or by a unique name")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string a = fixture.Create("Twin");
	const std::string b = fixture.Create("Twin");
	fixture.Create("Single");

	CHECK(fixture.Succeed("entity.get", J(R"({"entity": "Single"})")).at("name") == "Single");
	CHECK(fixture.Succeed("entity.get", J(R"({"entity": ")" + a + R"("})")).at("uuid") == a);
	const CommandResult ambiguous =
		fixture.Reject("entity.get", J(R"({"entity": "Twin"})"), CommandErrorCode::Conflict);
	CHECK(ambiguous.Message.starts_with("2 entities are named 'Twin' ("));
	CHECK(ambiguous.Message.find(a) != std::string::npos);
	CHECK(ambiguous.Message.find(b) != std::string::npos);
	CHECK(ambiguous.Message.ends_with("): use the UUID"));
	const CommandResult missing = fixture.Reject("entity.get", J(R"({"entity": "Ghost"})"), CommandErrorCode::NotFound);
	CHECK(missing.Message == "there is no entity named 'Ghost' in the active scene (entity.list shows the entities)");
	fixture.Reject("entity.get", J(R"({"entity": "0x00000000DEADBEEF"})"), CommandErrorCode::NotFound);
}

TEST_CASE("Automation: entity.destroy removes the subtree and undo brings it back exactly")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Before");
	const std::string tree = fixture.Create(
		"Tree", J(R"({"components": {"Health": {"Current": 5}, "Transform": {"Translation": [1, 0, 0]}}})"));
	fixture.Create("Branch", J(R"({"parent": "Tree"})"));
	fixture.Create("Leaf", J(R"({"parent": "Branch", "components": {"AllTypes": {"Target": ")" + tree + R"("}}})"));
	fixture.Create("Twig", J(R"({"parent": "Tree", "index": 0})"));
	fixture.Create("After");
	REQUIRE(Shape(fixture.Scene()) == "Before Tree[Twig,Branch[Leaf]] After");
	fixture.Succeed("selection.set", J(R"({"entities": ["Leaf", "After"]})"));

	const uint64_t before = fixture.Hash();
	const std::string fileBefore =
		*SceneSerializer::Serialize(fixture.Scene(), fixture.Context.GetActiveScene()->Metadata);

	const Json destroyed = fixture.Succeed("entity.destroy", J(R"({"entity": "Tree"})"));
	CHECK(destroyed.at("destroyed") == tree);
	CHECK(destroyed.at("entityCount") == 4);
	CHECK(Shape(fixture.Scene()) == "Before After");
	// The destroyed entities leave the selection.
	CHECK(Names(fixture.Succeed("selection.get").at("entities")) == std::vector<std::string>{ "After" });

	fixture.Succeed("undo");
	CHECK(fixture.Hash() == before);
	CHECK(Shape(fixture.Scene()) == "Before Tree[Twig,Branch[Leaf]] After"); // same places among the siblings
	CHECK(*SceneSerializer::Serialize(fixture.Scene(), fixture.Context.GetActiveScene()->Metadata) == fileBefore);
	fixture.Succeed("redo");
	CHECK(Shape(fixture.Scene()) == "Before After");
	fixture.Succeed("undo");
	CHECK(fixture.Hash() == before);

	// A child goes back to its parent, between its siblings.
	fixture.CheckUndoRedo([&] { fixture.Succeed("entity.destroy", J(R"({"entity": "Branch"})")); });
	CHECK(Shape(fixture.Scene()) == "Before Tree[Twig] After");

	fixture.Reject("entity.destroy", J(R"({"entity": "Nothing"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.destroy", J(R"({})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: entity.rename changes the name and undoes")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Old");
	fixture.CheckUndoRedo([&] {
		const Json renamed = fixture.Succeed("entity.rename", J(R"({"entity": "Old", "name": "New name"})"));
		CHECK(renamed.at("name") == "New name");
	});
	CHECK(fixture.Scene().FindEntityByName("New name").IsValid());
	CHECK_FALSE(fixture.Scene().FindEntityByName("Old").IsValid());
	CHECK(fixture.Context.GetUndoStack().GetUndoDescription() == "Rename 'Old' to 'New name'");

	fixture.Reject("entity.rename", J(R"({"entity": "New name", "name": ""})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.rename", J(R"({"entity": "Nobody", "name": "X"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.rename", J(R"({"entity": "New name"})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: entity.reparent moves entities, keeps their place and can keep the world transform")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("A");
	fixture.Create("B");
	fixture.Create("C");
	fixture.Create("Platform", J(R"({"components": {"Transform": {"Translation": [10, 0, 0]}}})"));
	fixture.Create("Crate", J(R"({"components": {"Transform": {"Translation": [1, 2, 3]}}})"));
	REQUIRE(Shape(fixture.Scene()) == "A B C Platform Crate");

	fixture.CheckUndoRedo([&] {
		const Json moved = fixture.Succeed("entity.reparent", J(R"({"entity": "B", "parent": "A"})"));
		CHECK(moved.at("parent") == fixture.Succeed("entity.get", J(R"({"entity": "A"})")).at("uuid"));
		CHECK(moved.at("index") == 0);
	});
	CHECK(Shape(fixture.Scene()) == "A[B] C Platform Crate");

	// At a position among the siblings.
	fixture.Succeed("entity.reparent", J(R"({"entity": "C", "parent": "A", "index": 0})"));
	CHECK(Shape(fixture.Scene()) == "A[C,B] Platform Crate");
	// Back to the roots, at a position.
	fixture.CheckUndoRedo([&] { fixture.Succeed("entity.reparent", J(R"({"entity": "B", "index": 1})")); });
	CHECK(Shape(fixture.Scene()) == "A[C] B Platform Crate");
	fixture.Succeed("undo");
	CHECK(Shape(fixture.Scene()) == "A[C,B] Platform Crate");
	fixture.Succeed("undo");
	CHECK(Shape(fixture.Scene()) == "A[B] C Platform Crate");

	// Without keepWorldTransform the local transform stays; with it the entity stays where it is in the world.
	const auto translation = [&](std::string_view name) {
		return fixture.Scene().FindEntityByName(name).GetComponent<TransformComponent>().Translation;
	};
	fixture.Succeed("entity.reparent", J(R"({"entity": "Crate", "parent": "Platform"})"));
	CHECK(translation("Crate") == glm::vec3(1.0f, 2.0f, 3.0f));
	fixture.Succeed("undo");
	fixture.CheckUndoRedo([&] {
		fixture.Succeed(
			"entity.reparent", J(R"({"entity": "Crate", "parent": "Platform", "keepWorldTransform": true})"));
	});
	CHECK(translation("Crate") == glm::vec3(-9.0f, 2.0f, 3.0f));
	fixture.Succeed("undo");
	CHECK(translation("Crate") == glm::vec3(1.0f, 2.0f, 3.0f)); // the local transform comes back too
	fixture.Succeed("redo");
	CHECK(translation("Crate") == glm::vec3(-9.0f, 2.0f, 3.0f));
}

TEST_CASE("Automation: entity.reparent refuses cycles and unknown parents")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Top");
	fixture.Create("Middle", J(R"({"parent": "Top"})"));
	fixture.Create("Bottom", J(R"({"parent": "Middle"})"));
	const uint64_t before = fixture.Hash();

	const CommandResult cycle =
		fixture.Reject("entity.reparent", J(R"({"entity": "Top", "parent": "Bottom"})"), CommandErrorCode::Conflict);
	CHECK(cycle.Message == "cannot make 'Top' a child of 'Bottom': the hierarchy would contain a cycle");
	fixture.Reject("entity.reparent", J(R"({"entity": "Top", "parent": "Top"})"), CommandErrorCode::Conflict);
	fixture.Reject("entity.reparent", J(R"({"entity": "Top", "parent": "Nobody"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.reparent", J(R"({"entity": "Nobody"})"), CommandErrorCode::NotFound);
	CHECK(fixture.Hash() == before);
}

TEST_CASE("Automation: entity.duplicate copies a subtree next to the original")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Other");
	const std::string root = fixture.Create("Root", J(R"({"components": {"Health": {"Current": 7}}})"));
	const std::string child = fixture.Create("Child", J(R"({"parent": "Root"})"));
	fixture.Create("Last");
	// References inside the subtree follow the copy, references outside stay.
	fixture.Succeed(
		"component.add",
		J(R"({"entity": ")" + child + R"(", "component": "AllTypes", "values": {"Target": ")" + root
	      + R"(", "Targets": []}})"));
	fixture.Succeed(
		"component.set",
		J(R"({"entity": ")" + child + R"(", "component": "AllTypes", "values": {"Target": ")" + root + R"("}})"));
	REQUIRE(Shape(fixture.Scene()) == "Other Root[Child] Last");

	Json copy;
	fixture.CheckUndoRedo(
		[&] { copy = fixture.Succeed("entity.duplicate", J(R"({"entity": "Root", "name": "Copy"})")); });
	CHECK(Shape(fixture.Scene()) == "Other Root[Child] Copy[Child] Last"); // right after the original
	CHECK(copy.at("name") == "Copy");
	CHECK(copy.at("entityCount") == 2);
	CHECK(copy.at("index") == 2);
	CHECK(copy.at("uuid") != root);

	World& world = fixture.Scene();
	const Entity copied = world.TryGetEntity(*UUID::Parse(copy.at("uuid").get<std::string>()));
	REQUIRE(copied.IsValid());
	CHECK(copied.GetComponent<HealthComponent>().Current == 7.0f);
	const Entity copiedChild = world.GetChildren(copied).front();
	CHECK(copiedChild.GetUUID() != UUID::Parse(child).value());
	CHECK(copiedChild.GetComponent<AllTypesComponent>().Target == copied.GetUUID()); // follows the copy
	CHECK(world.FindEntityByName("Root").GetUUID() == UUID::Parse(root).value());

	// Without a name the copy has the original's name.
	const Json same = fixture.Succeed("entity.duplicate", J(R"({"entity": "Last"})"));
	CHECK(same.at("name") == "Last");
	CHECK(same.at("index") == 4);
	fixture.Reject("entity.duplicate", J(R"({"entity": "Nobody"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.duplicate", J(R"({"entity": "Other", "name": ""})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"entity.duplicate", J(R"({"entity": "Last"})"), CommandErrorCode::Conflict); // two entities have this name now
}

TEST_CASE("Automation: entity.list shows the scene flat or as a tree, filtered and paged")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Player", J(R"({"components": {"Health": {}, "PlayerTag": {}}})"));
	fixture.Create("Sword", J(R"({"parent": "Player"})"));
	fixture.Create("Shield", J(R"({"parent": "Player", "components": {"Health": {"Current": 1}}})"));
	fixture.Create("Enemy", J(R"({"components": {"Health": {}}})"));
	fixture.Create("Crate");

	const Json flat = fixture.Succeed("entity.list");
	CHECK(Names(flat.at("entities")) == std::vector<std::string>{ "Player", "Sword", "Shield", "Enemy", "Crate" });
	CHECK(flat.at("count") == 5);
	CHECK(flat.at("total") == 5);
	CHECK(flat.at("truncated") == false);
	CHECK(flat.at("entities")[1].at("depth") == 1);
	CHECK(flat.at("entities")[1].at("parent") == flat.at("entities")[0].at("uuid"));
	CHECK(flat.at("entities")[0].at("parent").is_null());
	CHECK(Strings(flat.at("entities")[0].at("components")) == std::vector<std::string>{ "Health", "PlayerTag", "Tag" });

	CHECK(
		Names(fixture.Succeed("entity.list", J(R"({"component": "Health"})")).at("entities"))
		== std::vector<std::string>{ "Player", "Shield", "Enemy" });
	CHECK(
		Names(fixture.Succeed("entity.list", J(R"({"name": "S"})")).at("entities"))
		== std::vector<std::string>{ "Sword", "Shield" });
	CHECK(
		Names(fixture.Succeed("entity.list", J(R"({"name": "sh", "component": "Health"})")).at("entities"))
		== std::vector<std::string>{ "Shield" });
	CHECK(
		Names(fixture.Succeed("entity.list", J(R"({"parent": "Player"})")).at("entities"))
		== std::vector<std::string>{ "Sword", "Shield" });

	const Json page = fixture.Succeed("entity.list", J(R"({"limit": 2, "offset": 1})"));
	CHECK(Names(page.at("entities")) == std::vector<std::string>{ "Sword", "Shield" });
	CHECK(page.at("total") == 5);
	CHECK(page.at("truncated") == true);
	CHECK(fixture.Succeed("entity.list", J(R"({"limit": 2, "offset": 4})")).at("truncated") == false);

	// A tree keeps the ancestors of the entities that match.
	const Json tree = fixture.Succeed("entity.list", J(R"({"layout": "tree"})"));
	REQUIRE(tree.at("roots").size() == 3);
	CHECK(tree.at("roots")[0].at("name") == "Player");
	CHECK(Names(tree.at("roots")[0].at("children")) == std::vector<std::string>{ "Sword", "Shield" });
	CHECK(tree.at("roots")[1].at("children").empty());
	const Json filtered = fixture.Succeed("entity.list", J(R"({"layout": "tree", "name": "sword"})"));
	REQUIRE(filtered.at("roots").size() == 1);
	CHECK(filtered.at("roots")[0].at("name") == "Player"); // the ancestor is kept
	CHECK(Names(filtered.at("roots")[0].at("children")) == std::vector<std::string>{ "Sword" });
	CHECK(filtered.at("count") == 2);
	const Json ancestors = fixture.Succeed("entity.list", J(R"({"layout": "tree", "component": "PlayerTag"})"));
	CHECK(ancestors.at("count") == 1);

	fixture.Reject("entity.list", J(R"({"layout": "grid"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.list", J(R"({"component": "Nope"})"), CommandErrorCode::NotFound);
	fixture.Reject("entity.list", J(R"({"limit": 0})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: entity.get shows every component as in a scene file")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Parent");
	const std::string uuid = fixture.Create(
		"Subject",
		J(R"({"parent": "Parent", "components": {"Health": {"Current": 12.5}, "Transform": {"Scale": [2, 2, 2]}}})"));
	fixture.Create("Kid", J(R"({"parent": "Subject"})"));
	fixture.Succeed("component.add", J(R"({"entity": "Subject", "component": "Disabled"})"));

	const Json entity = fixture.Succeed("entity.get", J(R"({"entity": "Subject"})"));
	CHECK(entity.at("uuid") == uuid);
	CHECK(entity.at("name") == "Subject");
	CHECK(entity.at("parent") == fixture.Succeed("entity.get", J(R"({"entity": "Parent"})")).at("uuid"));
	CHECK(entity.at("children").size() == 1);
	CHECK(entity.at("enabled") == false);
	CHECK(entity.at("active") == false);
	CHECK(entity.at("index") == 0);
	const Json& components = entity.at("components");
	CHECK(components.at("Tag") == J(R"({"Name": "Subject"})"));
	CHECK(components.at("Health") == J(R"({"Current": 12.5, "Maximum": 100.0})"));
	CHECK(components.at("Transform").at("Scale") == J("[2.0, 2.0, 2.0]"));
	CHECK(components.at("Disabled") == Json::object());
	CHECK_FALSE(components.contains("ID"));
	CHECK_FALSE(components.contains("Relationship"));
	CHECK_FALSE(components.contains("WorldTransform"));
	CHECK_FALSE(components.contains("Inactive"));
}

TEST_CASE("Automation: entity.find searches by name, component and field value")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Boss", J(R"({"components": {"Health": {"Current": 10}}})"));
	fixture.Create("Minion", J(R"({"parent": "Boss", "components": {"Health": {"Current": 10, "Maximum": 20}}})"));
	fixture.Create("Minion", J(R"({"components": {"Health": {"Current": 5}}})"));
	fixture.Create("Barrel");

	const auto find = [&](std::string_view arguments) { return fixture.Succeed("entity.find", J(arguments)); };
	CHECK(find(R"({"name": "Minion"})").at("total") == 2);
	CHECK(find(R"({"name": "minion"})").at("total") == 0); // exact
	CHECK(find(R"({"nameContains": "MINI"})").at("total") == 2);
	CHECK(find(R"({"component": "Health"})").at("total") == 3);
	CHECK(find(R"({"component": "Health", "field": "Current", "equals": 10})").at("total") == 2);
	CHECK(
		find(R"({"component": "Health", "field": "Current", "equals": 10.0, "nameContains": "boss"})").at("total")
		== 1);
	CHECK(
		find(R"({"component": "Health", "field": "Maximum", "equals": 20})").at("entities")[0].at("path")
		== "Boss/Minion");
	CHECK(find(R"({"component": "Health", "limit": 1})").at("count") == 1);
	CHECK(find(R"({"component": "Health", "limit": 1})").at("total") == 3);
	CHECK(find(R"({"component": "AllTypes"})").at("total") == 0);
	CHECK(
		find(R"({"component": "Transform", "field": "Translation", "equals": [0, 0, 0]})").at("total")
		== 0); // none has a transform

	fixture.Reject("entity.find", J(R"({})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.find", J(R"({"field": "Current", "equals": 1})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"entity.find", J(R"({"component": "Health", "field": "Current"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"entity.find", J(R"({"component": "Health", "field": "Nope", "equals": 1})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject("entity.find", J(R"({"component": "Nope"})"), CommandErrorCode::NotFound);
}

TEST_CASE("Automation: scene commands need an active scene")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	REQUIRE(fixture.Context.CloseScene("Scenes/Main.gscene", true).HasValue());
	for (const char* name : { "entity.create", "entity.list" })
	{
		fixture.Reject(name, Json::object(), CommandErrorCode::NoScene);
	}
	fixture.Reject("entity.get", J(R"({"entity": "x"})"), CommandErrorCode::NoScene);
	fixture.Reject(
		"component.set", J(R"({"entity": "x", "component": "Tag", "values": {}})"), CommandErrorCode::NoScene);
}
