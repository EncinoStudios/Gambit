#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"
#include "TestSupport.h"

#include "Gambit/Reflection/TypeRegistry.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	const FieldInfo& Field(const ComponentInfo& info, std::string_view name)
	{
		const FieldInfo* field = info.FindField(name);
		REQUIRE_MESSAGE(field != nullptr, name);
		return *field;
	}

	// Loads `source` as a script that must fail and returns the recorded message.
	std::string LoadError(ScriptEngine& engine, std::string_view source)
	{
		const size_t before = engine.GetErrors().size();
		const Result<void> loaded = engine.LoadScript("Scripts/Bad.lua", source);
		REQUIRE_FALSE(loaded.HasValue());
		REQUIRE(engine.GetErrors().size() == before + 1);
		return engine.GetErrors().back().Message;
	}
}

TEST_CASE("Scripting: Component infers field types from defaults")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine
	            .LoadScript("Scripts/Spin.lua", R"lua(
Component("Spin", {
	Speed = 1.5,
	Whole = 3,
	Alive = true,
	Label = "spin",
	Offset = Vec2(1, 2),
	Axis = Vec3(0, 1, 0),
	Plane = Vec4(1, 2, 3, 4),
	Facing = Quat(),
	Tint = Color(1, 0.5, 0.25),
})
)lua")
	            .HasValue());

	const ComponentInfo* info = fixture.Types.FindComponent("Spin");
	REQUIRE(info != nullptr);
	CHECK(info->IsDynamic());
	CHECK(info->HasFlag(ComponentFlags::Serializable));

	// Lua tables have no order: the fields are ordered by name.
	std::vector<std::string> names;
	for (const FieldInfo& field : info->GetFields())
	{
		names.push_back(field.Name);
	}
	CHECK(
		names
		== std::vector<std::string>{ "Alive", "Axis", "Facing", "Label", "Offset", "Plane", "Speed", "Tint", "Whole" });

	CHECK(Field(*info, "Speed").Type == FieldType::Float);
	CHECK(Field(*info, "Speed").Default == FieldValue(1.5f));
	CHECK(Field(*info, "Whole").Type == FieldType::Float); // a number is a Float, whatever its spelling
	CHECK(Field(*info, "Whole").Default == FieldValue(3.0f));
	CHECK(Field(*info, "Alive").Type == FieldType::Bool);
	CHECK(Field(*info, "Alive").Default == FieldValue(true));
	CHECK(Field(*info, "Label").Type == FieldType::String);
	CHECK(Field(*info, "Label").Default == FieldValue(std::string("spin")));
	CHECK(Field(*info, "Offset").Type == FieldType::Vec2);
	CHECK(Field(*info, "Offset").Default == FieldValue(glm::vec2(1.0f, 2.0f)));
	CHECK(Field(*info, "Axis").Type == FieldType::Vec3);
	CHECK(Field(*info, "Axis").Default == FieldValue(glm::vec3(0.0f, 1.0f, 0.0f)));
	CHECK(Field(*info, "Plane").Type == FieldType::Vec4);
	CHECK(Field(*info, "Plane").Default == FieldValue(glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)));
	CHECK(Field(*info, "Facing").Type == FieldType::Quat);
	CHECK(Field(*info, "Facing").Default == FieldValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
	CHECK(Field(*info, "Tint").Type == FieldType::Color4);
	CHECK(Field(*info, "Tint").Default == FieldValue(glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)));

	REQUIRE(engine.GetComponents().size() == 1);
	CHECK(engine.GetComponents()[0].Name == "Spin");
	CHECK(engine.GetComponents()[0].Location.File == "Scripts/Spin.lua");
	CHECK(engine.GetComponents()[0].Location.Line == 2);
}

TEST_CASE("Scripting: Component takes explicit field types")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine
	            .LoadScript("Scripts/Door.lua", R"lua(
Component("Door", {
	Count = Int(-3),
	Ammo = UInt(30),
	Ratio = Float(),
	Elapsed = Double(2.5),
	Open = Bool(true),
	Label = String("door"),
	Rgb = Color3(1, 0.5, 0),
	Rgba = Color4(1, 1, 0, 0.5),
	White = Color4(),
	Target = EntityRef(),
	Icon = Asset("Texture"),
	Mode = Enum({ "Local", "World" }, "World"),
	State = Enum({ "Idle", "Run" }),
	Weights = Array(Float, { 0.5, 0.25 }),
	Waypoints = Array(Vec3, { Vec3(1, 1, 1) }),
	Names = Array("String"),
	Icons = Array(Asset("Texture")),
	Ids = Array(Int, { 1, 2 }),
	Tints = Array(Color, { Color(1, 0, 0) }),
	Others = Array(EntityRef),
})
Component("Marker", {})
)lua")
	            .HasValue());

	const ComponentInfo* info = fixture.Types.FindComponent("Door");
	REQUIRE(info != nullptr);
	CHECK(Field(*info, "Count").Type == FieldType::Int32);
	CHECK(Field(*info, "Count").Default == FieldValue(int32_t{ -3 }));
	CHECK(Field(*info, "Ammo").Type == FieldType::UInt32);
	CHECK(Field(*info, "Ammo").Default == FieldValue(uint32_t{ 30 }));
	CHECK(Field(*info, "Ratio").Type == FieldType::Float);
	CHECK(Field(*info, "Ratio").Default == FieldValue(0.0f));
	CHECK(Field(*info, "Elapsed").Type == FieldType::Double);
	CHECK(Field(*info, "Elapsed").Default == FieldValue(2.5));
	CHECK(Field(*info, "Open").Type == FieldType::Bool);
	CHECK(Field(*info, "Open").Default == FieldValue(true));
	CHECK(Field(*info, "Label").Type == FieldType::String);
	CHECK(Field(*info, "Label").Default == FieldValue(std::string("door")));
	CHECK(Field(*info, "Rgb").Type == FieldType::Color3);
	CHECK(Field(*info, "Rgb").Default == FieldValue(glm::vec3(1.0f, 0.5f, 0.0f)));
	CHECK(Field(*info, "Rgba").Type == FieldType::Color4);
	CHECK(Field(*info, "Rgba").Default == FieldValue(glm::vec4(1.0f, 1.0f, 0.0f, 0.5f)));
	CHECK(Field(*info, "White").Default == FieldValue(glm::vec4(1.0f)));
	CHECK(Field(*info, "Target").Type == FieldType::Entity);
	CHECK(Field(*info, "Target").Default == FieldValue(UUID()));
	CHECK(Field(*info, "Icon").Type == FieldType::AssetHandle);
	CHECK(Field(*info, "Icon").Attributes.AssetType == "Texture");

	const FieldInfo& mode = Field(*info, "Mode");
	CHECK(mode.Type == FieldType::Enum);
	REQUIRE(mode.Attributes.Enum != nullptr);
	CHECK(mode.Attributes.Enum->FindName(0) == "Local");
	CHECK(mode.Attributes.Enum->FindName(1) == "World");
	CHECK(mode.Default == FieldValue(std::in_place_type<int64_t>, 1));
	CHECK(Field(*info, "State").Default == FieldValue(std::in_place_type<int64_t>, 0)); // the first name

	CHECK(Field(*info, "Weights").Type == FieldType::Array);
	CHECK(Field(*info, "Weights").ElementType == FieldType::Float);
	CHECK(Field(*info, "Weights").Default == FieldValue(std::vector<float>{ 0.5f, 0.25f }));
	CHECK(Field(*info, "Waypoints").ElementType == FieldType::Vec3);
	CHECK(Field(*info, "Waypoints").Default == FieldValue(std::vector<glm::vec3>{ glm::vec3(1.0f) }));
	CHECK(Field(*info, "Names").ElementType == FieldType::String);
	CHECK(Field(*info, "Names").Default == FieldValue(std::vector<std::string>{}));
	CHECK(Field(*info, "Icons").ElementType == FieldType::AssetHandle);
	CHECK(Field(*info, "Icons").Attributes.AssetType == "Texture");
	CHECK(Field(*info, "Ids").ElementType == FieldType::Int32);
	CHECK(Field(*info, "Ids").Default == FieldValue(std::vector<int32_t>{ 1, 2 }));
	CHECK(Field(*info, "Tints").ElementType == FieldType::Color4);
	CHECK(Field(*info, "Tints").Default == FieldValue(std::vector<glm::vec4>{ glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }));
	CHECK(Field(*info, "Others").ElementType == FieldType::Entity);

	// A component without fields is a tag.
	const ComponentInfo* marker = fixture.Types.FindComponent("Marker");
	REQUIRE(marker != nullptr);
	CHECK(marker->GetFields().empty());

	// The declared defaults are what a new component holds.
	engine.FinishLoading();
	CHECK(
		fixture.Eval("local world = ... local door = world:Spawn('D', { Door = {} }):Get('Door') "
	                 "return { door.Count, door.Mode, door.Weights[2], door.Rgb.g, door.Label }")
		== fixture.Eval("return { -3, 'World', 0.25, 0.5, 'door' }"));
}

TEST_CASE("Scripting: Component rejects bad declarations with the script line")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	const ScopedLogCapture capture;

	const auto check = [&](std::string_view source, std::string_view expected) {
		INFO(source);
		const std::string message = LoadError(engine, source);
		CHECK_MESSAGE(Contains(message, expected), message);
		// Errors that a C++ binding raises name the script line too.
		CHECK_MESSAGE(Contains(message, "Scripts/Bad.lua:1:"), message);
		CHECK(engine.GetErrors().back().File == "Scripts/Bad.lua");
		CHECK(engine.GetErrors().back().Line == 1);
	};
	check("Component('Bad name', {})", "invalid component name 'Bad name'");
	check("Component('Transform', { X = 1 })", "already registered by the engine");
	check("Component('A', { 1, 2 })", "fields are named by strings");
	check("Component('A', { ['not valid'] = 1 })", "invalid field name 'not valid'");
	check("Component('A', { T = {} })", "a table is not a field type");
	check("Component('A', { F = function() end })", "cannot declare a field from a function");
	check("Component('A', { M = Mat4() })", "cannot declare a field from a userdata");
	check("Component('A', { N = 0 / 0 })", "A.N: nan is not a finite number");
	check("Component('A', { S = '\\xff' })", "A.S: the string is not valid UTF-8");
	check("Component('A', { V = Vec3(1 / 0, 0, 0) })", "is not a finite number");
	check("Component('A', { I = Int(1.5) })", "Int: 1.5 is not an integer");
	check("Component('A', { I = Int('x') })", "Int: expected an integer, got string");
	check("Component('A', { I = Int(2147483648) })", "is not an integer from -2147483648 to 2147483647");
	check("Component('A', { U = UInt(-1) })", "UInt: -1 is not an integer from 0 to 4294967295");
	check("Component('A', { F = Float(1 / 0) })", "Float: inf is not a finite number");
	check("Component('A', { F = Float(1e300) })", "Float: 1e+300 is too large for a Float");
	check("Component('A', { B = Bool(1) })", "Bool: expected a boolean, got number");
	check("Component('A', { S = String(5) })", "String: expected a string, got number");
	check("Component('A', { C = Color3(1, 2) })", "Color3: expected 0 or 3 numbers, got 2");
	check("Component('A', { C = Color4(1, 2) })", "Color4: expected 0, 3 or 4 numbers, got 2");
	check("Component('A', { E = Enum({}) })", "Enum: expected a list of value names");
	check("Component('A', { E = Enum({ 'A', 'A' }) })", "the value 'A' is listed twice");
	check("Component('A', { E = Enum({ 'A', 'not valid' }) })", "value 2 is not an identifier");
	check("Component('A', { E = Enum({ 'A' }, 'B') })", "the default 'B' is not one of the values");
	check("Component('A', { T = Asset('Nope') })", "'Nope' is not an asset type");
	check("Component('A', { L = Array(Enum({ 'A' })) })", "Enum cannot be an array element");
	check("Component('A', { L = Array(Array(Float)) })", "Array cannot be an array element");
	check("Component('A', { L = Array('Nope') })", "'Nope' is not an element type");
	check("Component('A', { L = Array(print) })", "the element type must be a type constructor");
	check("Component('A', { L = Array(Float, { 1, 'x' }) })", "Array[2]: expected a number, got string");
	check("Component('A', { L = Array(Float, { a = 1 }) })", "an array is a sequence");
	check("Component('A', { L = Array(Float, 5) })", "expected an array of Float (a table), got number");

	// Nothing was registered by the failed declarations.
	CHECK(fixture.Types.FindComponent("A") == nullptr);
	CHECK(engine.GetComponents().empty());

	// A component is declared once.
	REQUIRE(engine.LoadScript("Scripts/First.lua", "\n\nComponent('Twice', { X = 1 })").HasValue());
	const std::string twice = LoadError(engine, "Component('Twice', { X = 1 })");
	CHECK_MESSAGE(Contains(twice, "'Twice' is declared twice (first at Scripts/First.lua:3)"), twice);
}

TEST_CASE("Scripting: declarations are only allowed while the scripts load")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine.LoadScript("Scripts/Main.lua", "Component('Early', { X = 1 })").HasValue());
	engine.FinishLoading();

	for (const char* code : { "Component('Late', { X = 1 })", "System('Late', { OnUpdate = function() end })",
	                          "Test('Late', { Run = function() end })" })
	{
		INFO(code);
		const std::string message = fixture.EvalError(code);
		CHECK_MESSAGE(Contains(message, "declarations are only allowed while the scripts load"), message);
	}
	CHECK(fixture.Types.FindComponent("Late") == nullptr);
	CHECK(engine.GetSystems().empty());
	CHECK(engine.GetTests().empty());
}

TEST_CASE("Scripting: a script component that the registry has already is declared again unchanged")
{
	ScriptFixture fixture;
	const char* script = "Component('Spin', { Speed = 1.5, Count = Int(2) })";
	REQUIRE(fixture.Create(ScriptMode::Schema).LoadScript("Scripts/Spin.lua", script).HasValue());
	const ComponentInfo* first = fixture.Types.FindComponent("Spin");
	REQUIRE(first != nullptr);

	// A new engine on the same registry (the next play session) declares the same schema: the same type.
	REQUIRE(fixture.Create().LoadScript("Scripts/Spin.lua", script).HasValue());
	CHECK(fixture.Types.FindComponent("Spin") == first);

	// A different schema under the name is an error (the scripts changed: the host registers them again).
	const ScopedLogCapture capture;
	const std::string message =
		LoadError(fixture.Create(), "Component('Spin', { Speed = 1.5, Count = Int(2), More = true })");
	CHECK_MESSAGE(Contains(message, "already registered with a different schema"), message);
}

TEST_CASE("Scripting: System declarations are validated and recorded")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine
	            .LoadScript("Scripts/Systems.lua", R"lua(
System("Mover", {
	OnUpdate = function(world, dt) end,
})
System("Early", {
	Phase = "PreUpdate", Order = -5, RunInEditMode = true,
	OnCreate = function(world) end,
	OnUpdate = function(world, dt) end,
	OnDestroy = function(world) end,
})
)lua")
	            .HasValue());

	REQUIRE(engine.GetSystems().size() == 2);
	const ScriptSystemDeclaration& mover = engine.GetSystems()[0];
	CHECK(mover.Name == "Mover");
	CHECK(mover.Phase == SystemPhase::Update);
	CHECK(mover.Order == 0);
	CHECK_FALSE(mover.RunInEditMode);
	CHECK(mover.Location.File == "Scripts/Systems.lua");
	CHECK(mover.Location.Line == 2);
	const ScriptSystemDeclaration& early = engine.GetSystems()[1];
	CHECK(early.Name == "Early");
	CHECK(early.Phase == SystemPhase::PreUpdate);
	CHECK(early.Order == -5);
	CHECK(early.RunInEditMode);
	CHECK(early.Location.Line == 5);

	const ScopedLogCapture capture;
	const auto check = [&](std::string_view source, std::string_view expected) {
		INFO(source);
		const std::string message = LoadError(engine, source);
		CHECK_MESSAGE(Contains(message, expected), message);
		CHECK_MESSAGE(Contains(message, "Scripts/Bad.lua:1:"), message);
	};
	check("System('NoUpdate', {})", "System 'NoUpdate': OnUpdate (a function of world and dt) is required");
	check("System('NoUpdate', { OnCreate = function() end })", "OnUpdate (a function of world and dt) is required");
	check("System('Bad', { OnUpdate = 5 })", "OnUpdate must be a function");
	check("System('Bad', { OnUpdate = function() end, OnCreate = 'x' })", "OnCreate must be a function");
	check("System('Bad', { OnUpdate = function() end, Phase = 'Sometime' })", "Phase must be one of Input, PreUpdate");
	check("System('Bad', { OnUpdate = function() end, Phase = 'Count' })", "Phase must be one of");
	check("System('Bad', { OnUpdate = function() end, Phase = 3 })", "Phase must be one of");
	check("System('Bad', { OnUpdate = function() end, Order = 1.5 })", "Order: 1.5 is not an integer");
	check("System('Bad', { OnUpdate = function() end, RunInEditMode = 1 })", "RunInEditMode must be a boolean");
	check("System('Bad', { OnUpdate = function() end, OnTick = function() end })", "unknown key 'OnTick'");
	check("System('not valid', { OnUpdate = function() end })", "'not valid' is not a valid name");
	check(
		"System('Mover', { OnUpdate = function() end })", "'Mover' is declared twice (first at Scripts/Systems.lua:2)");
	CHECK(engine.GetSystems().size() == 2);
}

TEST_CASE("Scripting: Test declarations are validated and recorded")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine
	            .LoadScript("Scripts/Tests.lua", R"lua(
Test("Spin rotates", { Run = function(t) end })
Test("Door opens", {
	Scene = "Scenes/Door.gscene", Tags = { "door", "slow" }, Timeout = 600, Seed = 7,
	Run = function(t) end,
})
)lua")
	            .HasValue());

	REQUIRE(engine.GetTests().size() == 2);
	const ScriptTestDeclaration& spin = engine.GetTests()[0];
	CHECK(spin.Name == "Spin rotates");
	CHECK(spin.Scene.empty());
	CHECK(spin.Tags.empty());
	CHECK_FALSE(spin.Timeout.has_value());
	CHECK_FALSE(spin.Seed.has_value());
	CHECK(spin.Location.File == "Scripts/Tests.lua");
	CHECK(spin.Location.Line == 2);
	const ScriptTestDeclaration& door = engine.GetTests()[1];
	CHECK(door.Name == "Door opens");
	CHECK(door.Scene == "Scenes/Door.gscene");
	CHECK(door.Tags == std::vector<std::string>{ "door", "slow" });
	CHECK(door.Timeout == uint64_t{ 600 });
	CHECK(door.Seed == uint64_t{ 7 });
	CHECK(door.Location.Line == 3);

	const ScopedLogCapture capture;
	const auto check = [&](std::string_view source, std::string_view expected) {
		INFO(source);
		const std::string message = LoadError(engine, source);
		CHECK_MESSAGE(Contains(message, expected), message);
		CHECK_MESSAGE(Contains(message, "Scripts/Bad.lua:1:"), message);
	};
	check("Test('', { Run = function() end })", "the name must be a non-empty UTF-8 string");
	check("Test('NoRun', {})", "Test 'NoRun': Run (a function of the test context) is required");
	check("Test('Bad', { Run = function() end, Scene = 5 })", "Scene must be the path of a scene");
	check("Test('Bad', { Run = function() end, Tags = { 1 } })", "Tags[1]: expected a string, got number");
	check("Test('Bad', { Run = function() end, Timeout = 0 })", "Timeout must be at least 1 tick");
	check("Test('Bad', { Run = function() end, Timeout = -1 })", "Timeout: -1 is not an integer from 0");
	check("Test('Bad', { Run = function() end, Seed = 1.5 })", "Seed: 1.5 is not an integer");
	check("Test('Bad', { Run = function() end, Setup = function() end })", "unknown key 'Setup'");
	check("Test('Spin rotates', { Run = function() end })", "is declared twice (first at Scripts/Tests.lua:2)");
	CHECK(engine.GetTests().size() == 2);
}

TEST_CASE("Scripting: a schema engine registers components and only records systems and tests")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create(ScriptMode::Schema);
	fixture.Write("Scripts/Game.lua", R"lua(
Component("Score", { Points = Int(0) })
System("Scoring", {
	OnCreate = function(world) error("systems never run in schema mode") end,
	OnUpdate = function(world, dt) error("systems never run in schema mode") end,
})
Test("Scores", { Run = function(t) error("tests never run in schema mode") end })
)lua");
	REQUIRE(engine.LoadScripts().HasValue());
	engine.FinishLoading();

	CHECK(engine.GetMode() == ScriptMode::Schema);
	CHECK(engine.GetWorld() == nullptr);
	CHECK(fixture.Types.FindComponent("Score") != nullptr);
	REQUIRE(engine.GetComponents().size() == 1);
	REQUIRE(engine.GetSystems().size() == 1);
	CHECK(engine.GetSystems()[0].Name == "Scoring");
	REQUIRE(engine.GetTests().size() == 1);
	CHECK(engine.GetErrors().empty());

	// There is no world to attach to, and no world object for chunks.
	const Result<void> attached = engine.AttachToWorld();
	REQUIRE_FALSE(attached.HasValue());
	CHECK(Contains(attached.GetError().Message, "only a simulation script engine attaches to a world"));
	CHECK(fixture.Eval("return select('#', ...)") == ScriptValue(int64_t{ 0 }));
}
