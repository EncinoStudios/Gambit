#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"
#include "TestSupport.h"

#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/Math/Random.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Scripting: World:Spawn creates entities with components")
{
	ScriptFixture fixture;
	fixture.Start("Component('Spin', { Speed = 1.0, Axis = Vec3(0, 1, 0) })");

	CHECK(fixture.Eval(R"lua(
local world = ...
local bullet = world:Spawn("Bullet", {
	Transform = { Translation = Vec3(0, 1, 0) }, Spin = { Speed = 3 }, PlayerTag = {}, Tag = { Name = "Renamed" },
})
local anonymous = world:Spawn()
local unnamed = world:Spawn({ Health = { Current = 5 } })
local spin = bullet:Get("Spin")
local health = unnamed:Get("Health")
return {
	bullet.Name, anonymous.Name, unnamed.Name, bullet:Has("Spin"), bullet:Has("PlayerTag"), bullet:Has("Health"),
	spin.Speed, spin.Axis.y, health.Current, health.Maximum, anonymous:Get("Health") == nil,
}
)lua") == fixture.Eval("return { 'Renamed', 'Entity', 'Entity', true, true, false, 3.0, 1.0, 5.0, 100.0, true }"));
	Entity bullet = fixture.Scene->FindEntityByName("Renamed");
	REQUIRE(bullet);
	CHECK(bullet.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 1.0f, 0.0f));
	CHECK(bullet.HasComponent<PlayerTagComponent>());
	CHECK(fixture.Scene->GetEntityCount() == 3);

	// A wrong component or value spawns nothing.
	const auto check = [&](std::string_view code, std::string_view expected) {
		INFO(code);
		const std::string message = fixture.EvalError("local world = ... " + std::string(code));
		CHECK_MESSAGE(Contains(message, expected), message);
		CHECK_MESSAGE(Contains(message, "(evaluate):1:"), message);
		CHECK(fixture.Scene->GetEntityCount() == 3);
	};
	check("world:Spawn('X', { Nope = {} })", "there is no component type 'Nope'");
	check("world:Spawn('X', { Health = { Current = 'full' } })", "Health.Current: expected a number, got string");
	check("world:Spawn('X', { Health = { Nope = 1 } })", "Health has no field 'Nope' (fields: Current, Maximum)");
	check("world:Spawn('X', { Health = 5 })", "the values of a Health component must be a table of its fields");
	check("world:Spawn('X', { Spin = {}, Health = { Current = 0 / 0 } })", "Health.Current: nan");
	check("world:Spawn('X', { ID = {} })", "the ID component is managed by the engine");
	check("world:Spawn('X', { [1] = {} })", "components are named by strings");
	check("world:Spawn('\\xff')", "World:Spawn: the name is not valid UTF-8");
	check("world:Spawn(5)", "expected a name and/or a table of components, got a number as argument 1");
	check("world:Spawn('X', {}, {})", "expected a name and/or a table of components, got 3 arguments");
	check("world:Spawn('X', 'Y')", "got a string as argument 2");
}

TEST_CASE("Scripting: entities spawned by scripts get their UUIDs from the world's random generator")
{
	const auto spawn = [](uint64_t seed) {
		ScriptFixture fixture;
		fixture.Scene->SetSeed(seed);
		fixture.Start("");
		return Strings(fixture.Eval("local world = ... return { world:Spawn('A').UUID, world:Spawn('B').UUID }"));
	};
	const std::vector<std::string> first = spawn(7);
	REQUIRE(first.size() == 2);
	CHECK(first[0] != first[1]);
	CHECK(first[0].starts_with("0x"));
	CHECK(first[0].size() == 18);
	CHECK(spawn(7) == first);
	CHECK(spawn(8) != first);
}

TEST_CASE("Scripting: World finds and destroys entities")
{
	ScriptFixture fixture;
	fixture.Start("");
	Entity parent = fixture.Scene->CreateEntity("Parent");
	Entity child = fixture.Scene->CreateEntity("Child");
	REQUIRE(fixture.Scene->SetParent(child, parent).HasValue());
	const std::string id = parent.GetUUID().ToString();

	CHECK(fixture.Eval("local world = ... return world:Find('Parent').UUID") == ScriptValue(id));
	CHECK(fixture.Eval("local world = ... return world:Find('Nobody') == nil") == ScriptValue(true));
	CHECK(
		fixture.Eval("local world = ... return world:FindByUUID('" + id + "') == world:Find('Parent')")
		== ScriptValue(true));
	CHECK(fixture.Eval("local world = ... return world:FindByUUID('0x1') == nil") == ScriptValue(true));
	CHECK(Contains(fixture.EvalError("local world = ... world:FindByUUID('one')"), "'one' is not a UUID"));

	// Destroying is immediate and takes the descendants along; handles to them say so.
	CHECK(
		fixture.Eval(R"lua(
local world = ...
local parent, child = world:Find("Parent"), world:Find("Child")
world:Destroy(parent)
local ok, message = pcall(function() return child.Name end)
return { parent:IsValid(), child:IsValid(), world:Find("Child") == nil, ok, message, tostring(parent), parent.UUID }
)lua")
		== fixture.Eval(
			"return { false, false, true, false, '(evaluate):5: the entity " + child.GetUUID().ToString()
			+ " no longer exists', 'Entity(" + id + ", destroyed)', '" + id + "' }"));
	CHECK(fixture.Scene->GetEntityCount() == 0);

	CHECK(Contains(
		fixture.EvalError("local world = ... local e = world:Spawn('E') e:Destroy() world:Destroy(e)"),
		"no longer exists"));
	CHECK(Contains(fixture.EvalError("local world = ... world:Destroy('E')"), "(evaluate):1:"));
}

TEST_CASE("Scripting: Entity reads and changes its entity")
{
	ScriptFixture fixture;
	fixture.Start("");
	Entity entity = fixture.Scene->CreateEntity("E");
	entity.AddComponent<TransformComponent>().Translation = glm::vec3(1.0f, 2.0f, 3.0f);
	const std::string prelude = "local world = ... local e = world:Find('E') ";
	const auto run = [&](std::string_view code) { return fixture.Eval(prelude + std::string(code)); };
	const auto error = [&](std::string_view code) { return fixture.EvalError(prelude + std::string(code)); };

	// Name and UUID.
	CHECK(run("e.Name = 'Renamed' return e.Name") == ScriptValue(std::string("Renamed")));
	CHECK(entity.GetName() == "Renamed");
	entity.SetName("E");
	CHECK(Contains(error("e.Name = '\\xff'"), "Entity.Name: the name is not valid UTF-8"));
	CHECK(Contains(error("e.UUID = '0x5'"), "(evaluate):1:")); // read-only
	CHECK(entity.GetName() == "E");
	CHECK(run("return e.UUID") == ScriptValue(entity.GetUUID().ToString()));
	CHECK(run("return tostring(e)") == ScriptValue("Entity(" + entity.GetUUID().ToString() + ", 'E')"));
	CHECK(run("return e == world:Find('E') and e ~= world:Spawn('Other')") == ScriptValue(true));

	// Components.
	CHECK(
		run("local health = e:Add('Health', { Current = 50 }) return { health.Current, health.Maximum, "
	        "e:Has('Health'), e:Get('Health') == health }")
		== fixture.Eval("return { 50.0, 100.0, true, true }"));
	CHECK(entity.GetComponent<HealthComponent>().Current == 50.0f);
	CHECK(Contains(error("e:Add('Health')"), "('E') already has a Health component"));
	CHECK(Contains(error("e:Add('Nope')"), "there is no component type 'Nope'"));
	CHECK(Contains(error("e:Add('Relationship')"), "the Relationship component is managed by the engine"));
	CHECK(
		run("return { e:Remove('Health'), e:Remove('Health'), e:Has('Health') }")
		== fixture.Eval("return { true, false, false }"));
	CHECK_FALSE(entity.HasComponent<HealthComponent>());
	// A bad value adds nothing.
	CHECK(Contains(error("e:Add('Health', { Current = 'x' })"), "Health.Current: expected a number"));
	CHECK_FALSE(entity.HasComponent<HealthComponent>());
	CHECK(Contains(error("e:Remove('Tag')"), "the Tag component cannot be removed"));
	CHECK(Contains(error("e:Get('Nope')"), "there is no component type 'Nope'"));
	CHECK(run("e:Add('PlayerTag') return e:Has('PlayerTag')") == ScriptValue(true));

	// Hierarchy.
	CHECK(run(R"lua(
local a, b = world:Spawn("A"), world:Spawn("B")
local before = e.Parent == nil
a:SetParent(e)
b:SetParent(e)
local names = {}
for index, child in ipairs(e:Children()) do names[index] = child.Name end
local parented = a.Parent == e
b:SetParent(nil)
return { before, parented, names[1], names[2], #e:Children(), b.Parent == nil }
)lua") == fixture.Eval("return { true, true, 'A', 'B', 1, true }"));
	CHECK(Contains(error("e:SetParent(world:Find('A'))"), "Entity:SetParent:")); // a cycle
	CHECK(Contains(error("e:SetParent(5)"), "Entity:SetParent: parent: expected an Entity or nil, got number"));
	CHECK(Contains(
		error("world:Find('B'):SetParent(e, 'yes')"),
		"Entity:SetParent: keepWorldTransform: expected a boolean or nil, got string"));

	// World transforms follow the hierarchy; keepWorldTransform keeps a child where it is.
	CHECK(run(R"lua(
local a = world:Find("A")
a:Add("Transform", { Translation = Vec3(1, 0, 0) })
local b = world:Find("B")
b:Add("Transform", { Translation = Vec3(5, 5, 5) })
b:SetParent(e, true)
local rotation = a:WorldRotation()
return { a:WorldPosition(), b:WorldPosition(), b:Get("Transform").Translation, rotation.w }
)lua") == fixture.Eval("return { Vec3(2, 2, 3), Vec3(5, 5, 5), Vec3(4, 3, 2), 1.0 }"));

	// Enabled state: a disabled entity is still valid, and so is its subtree, which is inactive.
	CHECK(
		run("e:SetEnabled(false) return { e:IsEnabled(), world:Find('A'):IsEnabled(), e:IsValid() }")
		== fixture.Eval("return { false, true, true }"));
	CHECK_FALSE(fixture.Scene->IsActive(fixture.Scene->FindEntityByName("A")));
	CHECK(run("e:SetEnabled(true) return e:IsEnabled()") == ScriptValue(true));

	// Destroy takes the subtree.
	CHECK(
		run("local a = world:Find('A') e:Destroy() return { e:IsValid(), a:IsValid() }")
		== fixture.Eval("return { false, false }"));
}

TEST_CASE("Scripting: queries iterate a snapshot and skip what no longer matches")
{
	ScriptFixture fixture;
	fixture.Start("");
	for (int index = 1; index <= 5; ++index)
	{
		Entity entity = fixture.Scene->CreateEntity("E" + std::to_string(index));
		entity.AddComponent<HealthComponent>().Current = static_cast<float>(index);
		entity.AddComponent<TransformComponent>();
	}

	// The first entity visited is E1 or E5 (storage order); it spawns a new match and makes E2, E3 and E4 stop
	// matching in three ways. The loop then visits only the other of E1 and E5: never the new entity, never one that
	// no longer matches.
	std::vector<double> seen = Numbers(fixture.Eval(R"lua(
local world = ...
local seen = {}
for entity, health, transform in world:Query("Health", "Transform") do
	seen[#seen + 1] = health.Current
	if #seen == 1 then
		world:Spawn("Late", { Health = { Current = 99 }, Transform = {} })
		world:Find("E2"):Destroy()
		world:Find("E3"):Remove("Health")
		world:Find("E4"):SetEnabled(false)
	end
	transform.Translation.x = health.Current
end
return seen
)lua"));
	std::ranges::sort(seen);
	CHECK(seen == std::vector<double>{ 1.0, 5.0 });
	CHECK(fixture.Scene->FindEntityByName("E1").GetComponent<TransformComponent>().Translation.x == 1.0f);
	CHECK(fixture.Scene->FindEntityByName("E5").GetComponent<TransformComponent>().Translation.x == 5.0f);
	CHECK(fixture.Scene->FindEntityByName("E4").GetComponent<TransformComponent>().Translation.x == 0.0f);

	// The next query sees the world as it is now; Count and QueryFirst agree with it.
	seen = Numbers(fixture.Eval("local world = ... local seen = {} for _, health in world:Query('Health') do "
	                            "seen[#seen + 1] = health.Current end return seen"));
	std::ranges::sort(seen);
	CHECK(seen == std::vector<double>{ 1.0, 5.0, 99.0 });
	CHECK(
		fixture.Eval("local world = ... return { world:Count('Health'), world:Count('Health', 'Transform'), "
	                 "world:Count('Transform'), world:Count('PlayerTag') }")
		== fixture.Eval("return { 3, 3, 4, 0 }"));
	CHECK(
		fixture.Eval("local world = ... local entity, health = world:QueryFirst('Health') local first for e in "
	                 "world:Query('Health') do first = e break end return { entity == first, health.Current > 0, "
	                 "world:QueryFirst('PlayerTag') == nil }")
		== fixture.Eval("return { true, true, true }"));

	// Two loops over the same components do not disturb each other, and a query can run inside another.
	CHECK(
		fixture.Eval("local world = ... local pairs = 0 for a in world:Query('Health') do for b in "
	                 "world:Query('Health') do pairs = pairs + 1 end end return pairs")
		== ScriptValue(int64_t{ 9 }));

	// The order is the same whenever the same operations built the world.
	const auto order = [] {
		ScriptFixture other;
		other.Start("");
		return Strings(other.Eval(R"lua(
local world = ...
for index = 1, 6 do world:Spawn("E" .. index, { Health = {} }) end
world:Find("E2"):Destroy()
world:Spawn("E7", { Health = {} })
local names = {}
for entity in world:Query("Health") do names[#names + 1] = entity.Name end
return names
)lua"));
	};
	const std::vector<std::string> names = order();
	CHECK(names.size() == 6);
	CHECK(order() == names);

	const auto check = [&](std::string_view code, std::string_view expected) {
		INFO(code);
		const std::string message = fixture.EvalError("local world = ... " + std::string(code));
		CHECK_MESSAGE(Contains(message, expected), message);
		CHECK_MESSAGE(Contains(message, "(evaluate):1:"), message);
	};
	check("world:Count()", "World:Count: name at least one component");
	check("world:Query()", "World:Query: name at least one component");
	check("world:QueryFirst()", "World:QueryFirst: name at least one component");
	check("world:Query('Nope')", "there is no component type 'Nope'");
	check("world:Query(5)", "World:Query: components are named by strings");
}

TEST_CASE("Scripting: World exposes the tick, the time and the random generator")
{
	ScriptFixture fixture;
	fixture.Scene->SetSeed(99);
	fixture.Start("");
	fixture.Tick(3);
	CHECK(
		fixture.Eval("local world = ... return { world.Tick, world.Time, world.DeltaTime, math.type(world.Tick) }")
		== ScriptValue(ScriptTable{
			{ ScriptValue(int64_t{ 1 }), ScriptValue(int64_t{ 3 }) },
			{ ScriptValue(int64_t{ 2 }), ScriptValue(fixture.Scene->GetTime()) },
			{ ScriptValue(int64_t{ 3 }), ScriptValue(static_cast<double>(1.0f / 60.0f)) },
			{ ScriptValue(int64_t{ 4 }), ScriptValue(std::string("integer")) },
		}));
	CHECK(Contains(fixture.EvalError("local world = ... world.Tick = 5"), "(evaluate):1:"));

	// world.Random is the world's generator: the same draws as the C++ API, in the same order.
	Random expected = fixture.Scene->GetRandom();
	const ScriptValue drawn = fixture.Eval(R"lua(
local random = (...).Random
return {
	random:Float(), random:Range(-2, 2), random:Int(1, 6), random:Bool(), random:Bool(0.9), random:Gaussian(),
	random:Gaussian(10, 2), random:InsideUnitSphere(),
}
)lua");
	const ScriptTable& values = *drawn.TryGet<ScriptTable>();
	REQUIRE(values.size() == 8);
	CHECK(values[0].Value == ScriptValue(static_cast<double>(expected.NextFloat01())));
	CHECK(values[1].Value == ScriptValue(static_cast<double>(expected.RangeFloat(-2.0f, 2.0f))));
	CHECK(values[2].Value == ScriptValue(expected.RangeInt(int64_t{ 1 }, int64_t{ 6 })));
	CHECK(values[3].Value == ScriptValue(expected.NextBool()));
	CHECK(values[4].Value == ScriptValue(expected.NextBool(0.9f)));
	CHECK(values[5].Value == ScriptValue(static_cast<double>(expected.NextGaussian())));
	CHECK(values[6].Value == ScriptValue(static_cast<double>(expected.NextGaussian(10.0f, 2.0f))));
	CHECK(values[7].Value == ScriptValue(expected.InsideUnitSphere()));
	// The script's draws advanced the world's generator.
	CHECK(fixture.Scene->GetRandom().NextFloat01() == expected.NextFloat01());

	CHECK(Contains(fixture.EvalError("(...).Random:Range(2, 1)"), "Random:Range: min (2) is greater than max (1)"));
	CHECK(Contains(fixture.EvalError("(...).Random:Int(2, 1)"), "Random:Int: min (2) is greater than max (1)"));
	CHECK(Contains(
		fixture.EvalError("(...).Random:Bool('often')"),
		"Random:Bool: probability: expected a number or nil, got string"));
}
