#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"
#include "TestSupport.h"

#include "Gambit/ECS/Components/CoreComponents.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// A started engine and an entity "E" with the AllTypes and Structs test components.
	struct ProxyFixture : ScriptFixture
	{
		Entity Target;

		ProxyFixture()
		{
			Start("");
			Target = Scene->CreateEntity("E");
			Target.AddComponent<AllTypesComponent>();
			Target.AddComponent<StructsComponent>();
		}

		[[nodiscard]] AllTypesComponent& All() { return Target.GetComponent<AllTypesComponent>(); }
		[[nodiscard]] StructsComponent& Structs() { return Target.GetComponent<StructsComponent>(); }

		// Evaluates `code` with the locals world, e (the entity), c (its AllTypes) and s (its Structs).
		ScriptValue Run(std::string_view code) { return Eval(Prelude + std::string(code)); }
		std::string RunError(std::string_view code) { return EvalError(Prelude + std::string(code)); }

		static constexpr const char* Prelude =
			"local world = ... local e = world:Find('E') local c = e:Get('AllTypes') local s = e:Get('Structs') ";
	};
}

TEST_CASE("Scripting: component proxies read every field type")
{
	ProxyFixture fixture;
	fixture.All().UInt64 = uint64_t{ 1 } << 60;

	CHECK(fixture.Run("return c.Bool") == ScriptValue(true));
	CHECK(fixture.Run("return c.Int32") == ScriptValue(int64_t{ -7 }));
	CHECK(fixture.Run("return c.UInt32") == ScriptValue(int64_t{ 7 }));
	CHECK(fixture.Run("return c.Int64") == ScriptValue(-(int64_t{ 1 } << 40)));
	CHECK(fixture.Run("return c.UInt64") == ScriptValue(int64_t{ 1 } << 60));
	CHECK(fixture.Run("return c.Float") == ScriptValue(1.5));
	CHECK(fixture.Run("return c.Double") == ScriptValue(2.25));
	CHECK(fixture.Run("return math.type(c.Int32) .. math.type(c.Float)") == ScriptValue(std::string("integerfloat")));
	CHECK(fixture.Run("return c.Vec2") == ScriptValue(glm::vec2(1.0f, 2.0f)));
	CHECK(fixture.Run("return c.Vec3") == ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)));
	CHECK(fixture.Run("return c.Vec4") == ScriptValue(glm::vec4(1.0f, 2.0f, 3.0f, 4.0f)));
	CHECK(fixture.Run("return c.Quat") == ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
	CHECK(fixture.Run("return c.Color3") == ScriptValue(ScriptColorValue{ glm::vec4(0.5f, 0.25f, 1.0f, 1.0f) }));
	CHECK(fixture.Run("return c.Color4") == ScriptValue(ScriptColorValue{ glm::vec4(0.5f, 0.25f, 1.0f, 0.75f) }));
	CHECK(fixture.Run("return c.String") == ScriptValue(std::string("hello")));
	CHECK(fixture.Run("return c.Target") == ScriptValue(ScriptEntityValue{ UUID(0x1234) }));
	CHECK(fixture.Run("return c.Texture") == ScriptValue(std::string("0x000000000000ABCD")));
	CHECK(fixture.Run("return c.Mode") == ScriptValue(std::string("Slow")));

	// A UInt64 beyond the integers of Lua is a float; null references are nil.
	fixture.All().UInt64 = uint64_t{ 1 } << 63;
	fixture.All().Target = UUID();
	fixture.All().Texture = UUID();
	CHECK(fixture.Run("return c.UInt64") == ScriptValue(9223372036854775808.0));
	CHECK(fixture.Run("return c.Target == nil and c.Texture == nil") == ScriptValue(true));

	// Arrays: a length and elements from 1; past the end is nil, so ipairs works.
	CHECK(
		fixture.Run("return { #c.Bools, c.Bools[1], c.Bools[2], c.Bools[3] == nil }")
		== fixture.Eval("return { 2, true, false, true }"));
	CHECK(
		fixture.Run("return { c.Int32s[2], c.UInt32s[1], c.Int64s[1], c.UInt64s[2], c.Floats[1], c.Doubles[1] }")
		== fixture.Eval("return { -2, 3, -4, 5, 0.5, 0.25 }"));
	CHECK(fixture.Run("return c.Vec2s[1]") == ScriptValue(glm::vec2(1.0f, 2.0f)));
	CHECK(fixture.Run("return c.Vec3s[1]") == ScriptValue(glm::vec3(3.0f)));
	CHECK(fixture.Run("return c.Vec4s[1]") == ScriptValue(glm::vec4(4.0f)));
	CHECK(fixture.Run("return c.Quats[1]") == ScriptValue(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
	CHECK(fixture.Run("return c.Color3s[1]") == ScriptValue(ScriptColorValue{ glm::vec4(glm::vec3(0.1f), 1.0f) }));
	CHECK(fixture.Run("return c.Color4s[1]") == ScriptValue(ScriptColorValue{ glm::vec4(0.2f) }));
	CHECK(fixture.Run("return c.Targets[1]") == ScriptValue(ScriptEntityValue{ UUID(1) }));
	CHECK(fixture.Run("return c.Textures[1]") == ScriptValue(std::string("0x0000000000000002")));
	CHECK(
		Strings(fixture.Run("local names = {} for i, name in ipairs(c.Strings) do names[i] = name end return names"))
		== std::vector<std::string>{ "a", "b" });
}

TEST_CASE("Scripting: component proxies write every field type")
{
	ProxyFixture fixture;
	fixture.Run(R"lua(
c.Bool = false
c.Int32 = 42
c.UInt32 = 4000000000
c.Int64 = -9007199254740993
c.UInt64 = 123
c.Float = 2.5
c.Double = 1e300
c.Vec2 = Vec2(3, 4)
c.Vec3 = Vec3(4, 5, 6)
c.Vec4 = Vec4(5, 6, 7, 8)
c.Quat = Quat(0, 1, 0, 0)
c.Color3 = Color(0.25, 0.5, 0.75)
c.Color4 = Color(0.25, 0.5, 0.75, 0.125)
c.String = "w\u{F6}rld"
c.Target = e
c.Texture = "0x10"
c.Mode = "Fast"
c.Bools = { false, true, true }
c.Int32s = { 5 }
c.UInt32s = {}
c.Int64s = { 1, 2 }
c.UInt64s = { 7 }
c.Floats = { 1.5, 2.5 }
c.Doubles = { 0.125 }
c.Vec2s = { Vec2(1, 1), Vec2(2, 2) }
c.Vec3s = { Vec3(1, 2, 3) }
c.Vec4s = { Vec4(1, 2, 3, 4) }
c.Quats = { Quat(1, 0, 0, 0) }
c.Color3s = { Color(1, 0, 0) }
c.Color4s = { Color(0, 1, 0, 0.5) }
c.Strings = { "x", "y", "z" }
c.Targets = { e }
c.Textures = { "0x20", "0x30" }
)lua");

	const AllTypesComponent& all = fixture.All();
	CHECK_FALSE(all.Bool);
	CHECK(all.Int32 == 42);
	CHECK(all.UInt32 == 4000000000u);
	CHECK(all.Int64 == -9007199254740993);
	CHECK(all.UInt64 == 123);
	CHECK(all.Float == 2.5f);
	CHECK(all.Double == 1e300);
	CHECK(all.Vec2 == glm::vec2(3.0f, 4.0f));
	CHECK(all.Vec3 == glm::vec3(4.0f, 5.0f, 6.0f));
	CHECK(all.Vec4 == glm::vec4(5.0f, 6.0f, 7.0f, 8.0f));
	CHECK(all.Quat == glm::quat(0.0f, 0.0f, 1.0f, 0.0f)); // Quat(x, y, z, w)
	CHECK(all.Color3 == glm::vec3(0.25f, 0.5f, 0.75f));
	CHECK(all.Color4 == glm::vec4(0.25f, 0.5f, 0.75f, 0.125f));
	CHECK(all.String == "w\xC3\xB6rld");
	CHECK(all.Target == fixture.Target.GetUUID());
	CHECK(all.Texture == UUID(0x10));
	CHECK(all.Mode == TestMode::Fast);
	CHECK(all.Bools == std::vector<bool>{ false, true, true });
	CHECK(all.Int32s == std::vector<int32_t>{ 5 });
	CHECK(all.UInt32s.empty());
	CHECK(all.Int64s == std::vector<int64_t>{ 1, 2 });
	CHECK(all.UInt64s == std::vector<uint64_t>{ 7 });
	CHECK(all.Floats == std::vector<float>{ 1.5f, 2.5f });
	CHECK(all.Doubles == std::vector<double>{ 0.125 });
	CHECK(all.Vec2s == std::vector<glm::vec2>{ glm::vec2(1.0f), glm::vec2(2.0f) });
	CHECK(all.Vec3s == std::vector<glm::vec3>{ glm::vec3(1.0f, 2.0f, 3.0f) });
	CHECK(all.Vec4s == std::vector<glm::vec4>{ glm::vec4(1.0f, 2.0f, 3.0f, 4.0f) });
	CHECK(all.Quats == std::vector<glm::quat>{ glm::quat(0.0f, 1.0f, 0.0f, 0.0f) });
	CHECK(all.Color3s == std::vector<glm::vec3>{ glm::vec3(1.0f, 0.0f, 0.0f) });
	CHECK(all.Color4s == std::vector<glm::vec4>{ glm::vec4(0.0f, 1.0f, 0.0f, 0.5f) });
	CHECK(all.Strings == std::vector<std::string>{ "x", "y", "z" });
	CHECK(all.Targets == std::vector<UUID>{ fixture.Target.GetUUID() });
	CHECK(all.Textures == std::vector<UUID>{ UUID(0x20), UUID(0x30) });

	// nil clears references; a float with an integral value is an integer; vectors convert to colors.
	fixture.Run("c.Target = nil c.Texture = nil c.Int32 = 8.0 c.Color3 = Vec3(1, 2, 3) c.Color4 = Vec4(1, 2, 3, 4)");
	CHECK(fixture.All().Target == UUID());
	CHECK(fixture.All().Texture == UUID());
	CHECK(fixture.All().Int32 == 8);
	CHECK(fixture.All().Color3 == glm::vec3(1.0f, 2.0f, 3.0f));
	CHECK(fixture.All().Color4 == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST_CASE("Scripting: vectors, quaternions and colors of a component write through")
{
	ProxyFixture fixture;
	fixture.Run("c.Vec2.y = 9 c.Vec3.x = 7 c.Vec4.w = 1 c.Quat.w = 0.5 c.Color3.g = 0.75 c.Color4.a = 0.25 "
	            "c.Vec3s[1].z = 8 c.Color4s[1].r = 1");
	CHECK(fixture.All().Vec2 == glm::vec2(1.0f, 9.0f));
	CHECK(fixture.All().Vec3 == glm::vec3(7.0f, 2.0f, 3.0f));
	CHECK(fixture.All().Vec4 == glm::vec4(1.0f, 2.0f, 3.0f, 1.0f));
	CHECK(fixture.All().Quat == glm::quat(0.5f, 0.0f, 0.0f, 0.0f));
	CHECK(fixture.All().Color3 == glm::vec3(0.5f, 0.75f, 1.0f));
	CHECK(fixture.All().Color4 == glm::vec4(0.5f, 0.25f, 1.0f, 0.25f));
	CHECK(fixture.All().Vec3s == std::vector<glm::vec3>{ glm::vec3(3.0f, 3.0f, 8.0f) });
	CHECK(fixture.All().Color4s == std::vector<glm::vec4>{ glm::vec4(1.0f, 0.2f, 0.2f, 0.2f) });

	// The Transform of the brief: transform.Translation.x = 5.
	fixture.Target.AddComponent<TransformComponent>();
	fixture.Run("local t = e:Get('Transform') t.Translation = Vec3(1, 2, 3) t.Translation.x = 5 "
	            "t.Rotation = t.Rotation * Quat.AngleAxis(0, Vec3(0, 1, 0)) t.Scale.y = 2");
	CHECK(fixture.Target.GetComponent<TransformComponent>().Translation == glm::vec3(5.0f, 2.0f, 3.0f));
	CHECK(fixture.Target.GetComponent<TransformComponent>().Scale == glm::vec3(1.0f, 2.0f, 1.0f));

	// A value read from a field stays bound to it: it reads the field as it is now.
	CHECK(fixture.Run("local v = c.Vec3 c.Vec3 = Vec3(1, 1, 1) return v.x") == ScriptValue(1.0));
	fixture.All().Vec3 = glm::vec3(4.0f);
	CHECK(fixture.Run("local v = c.Vec3 return v:Length() == Vec3(4, 4, 4):Length()") == ScriptValue(true));
	// Arithmetic yields plain values, which write nowhere.
	fixture.Run("local sum = c.Vec3 + Vec3(1, 0, 0) sum.x = 100 local scaled = c.Vec3 * 2 scaled.y = 100");
	CHECK(fixture.All().Vec3 == glm::vec3(4.0f));
	// Assigning a bound value copies what it holds now.
	fixture.Run("c.Vec3s = { c.Vec3 } c.Vec3.x = 0");
	CHECK(fixture.All().Vec3s == std::vector<glm::vec3>{ glm::vec3(4.0f) });

	// A Color3 field has no alpha to write; its alpha reads as 1.
	CHECK(fixture.Run("return c.Color3.a") == ScriptValue(1.0));
	CHECK(Contains(fixture.RunError("c.Color3.a = 0.5"), "the color is a Color3 field: it has no alpha"));

	// Components written through a bound value are checked like assigned ones.
	CHECK(Contains(fixture.RunError("c.Vec3.x = 0 / 0"), "AllTypes.Vec3: component 1 (nan) is not a finite number"));
	CHECK(Contains(fixture.RunError("c.Vec3s[1].y = 1 / 0"), "AllTypes.Vec3s[1]: component 2 (inf)"));
	CHECK(fixture.All().Vec3.x == 0.0f);
}

TEST_CASE("Scripting: component proxies check what they are given")
{
	ProxyFixture fixture;
	const AllTypesComponent before = fixture.All();

	const auto check = [&](std::string_view code, std::string_view expected) {
		INFO(code);
		const std::string message = fixture.RunError(code);
		CHECK_MESSAGE(Contains(message, expected), message);
		CHECK_MESSAGE(Contains(message, "(evaluate):1:"), message); // the script line, though C++ raised it
	};
	check("c.Bool = 1", "AllTypes.Bool: expected a boolean, got number");
	check("c.Int32 = 'x'", "AllTypes.Int32: expected an integer, got string");
	check("c.Int32 = 1.5", "AllTypes.Int32: 1.5 is not an integer from -2147483648 to 2147483647");
	check("c.Int32 = 2147483648", "AllTypes.Int32: 2147483648 is not an integer");
	check("c.UInt32 = -1", "AllTypes.UInt32: -1 is not an integer from 0 to 4294967295");
	check("c.Int64 = 2^63", "AllTypes.Int64:");
	check("c.UInt64 = -1", "AllTypes.UInt64: -1 is not an integer from 0 to 18446744073709551615");
	check("c.UInt64 = 2^64", "AllTypes.UInt64:");
	check("c.Float = 'x'", "AllTypes.Float: expected a number, got string");
	check("c.Float = 0 / 0", "AllTypes.Float: nan is not a finite number");
	check("c.Float = -1 / 0", "AllTypes.Float: -inf is not a finite number");
	check("c.Float = 1e300", "AllTypes.Float: 1e+300 is too large for a Float");
	check("c.Double = math.huge", "AllTypes.Double: inf is not a finite number");
	check("c.Vec2 = Vec3(1, 2, 3)", "AllTypes.Vec2: expected a Vec2, got userdata");
	check("c.Vec3 = { 1, 2, 3 }", "AllTypes.Vec3: expected a Vec3, got table");
	check("c.Vec3 = Vec3(0, 0 / 0, 0)", "AllTypes.Vec3: component 2 (nan) is not a finite number");
	check("c.Vec4 = 5", "AllTypes.Vec4: expected a Vec4, got number");
	check("c.Quat = Vec4(0, 0, 0, 1)", "AllTypes.Quat: expected a Quat, got userdata");
	check("c.Quat = Quat(0, 0, 0, 1 / 0)", "AllTypes.Quat: component 4 (inf) is not a finite number");
	check("c.Color3 = 'red'", "AllTypes.Color3: expected a Color, got string");
	check("c.Color4 = Vec3(1, 1, 1)", "AllTypes.Color4: expected a Color, got userdata");
	check("c.String = 5", "AllTypes.String: expected a string, got number");
	check("c.String = '\\xff\\xfe'", "AllTypes.String: the string is not valid UTF-8");
	check("c.Target = 'E'", "AllTypes.Target: expected an Entity or nil, got string");
	check("c.Texture = 16", "AllTypes.Texture: expected a UUID string (\"0x...\") or nil, got number");
	check("c.Texture = 'sixteen'", "AllTypes.Texture: 'sixteen' is not a UUID");
	check("c.Mode = 'Sideways'", "AllTypes.Mode: 'Sideways' is not one of Reverse, Off, Slow, Fast");
	check("c.Mode = 3", "AllTypes.Mode: expected the name of an enum value, got number");
	check("c.Floats = 5", "AllTypes.Floats: expected an array of Float (a table), got number");
	check("c.Floats = { 1, 'x' }", "AllTypes.Floats[2]: expected a number, got string");
	check("c.Floats = { 1, 0 / 0 }", "AllTypes.Floats[2]: nan is not a finite number");
	check("c.Floats = { 1, x = 2 }", "AllTypes.Floats: an array is a sequence");
	check("c.Floats = c.Int32s", "AllTypes.Floats: expected an array of Float, got another kind of array");
	check("c.Strings = { 'ok', '\\xff' }", "AllTypes.Strings[2]: the string is not valid UTF-8");
	check("c.Nope = 1", "AllTypes has no field 'Nope' (fields: Bool, Int32");
	check("return c.Nope", "AllTypes has no field 'Nope'");
	check("return c[1]", "AllTypes: fields are named by strings");
	check("c.Floats[0] = 1", "AllTypes.Floats: index 0 is out of range (arrays count from 1)");
	check("c.Floats[3] = 1", "AllTypes.Floats: index 3 is out of range (1 to 2 to append)");
	check("c.Floats.x = 1", "AllTypes.Floats: arrays are indexed by integers from 1");
	check("return c.Floats[1.5]", "AllTypes.Floats: arrays are indexed by integers from 1");
	// A float with an integral value is an integer, as everywhere in Lua.
	CHECK(fixture.Run("return c.Floats[1.0]") == ScriptValue(0.5));

	// A refused write changes nothing.
	const AllTypesComponent& after = fixture.All();
	CHECK(after.Int32 == before.Int32);
	CHECK(after.Float == before.Float);
	CHECK(after.Vec3 == before.Vec3);
	CHECK(after.String == before.String);
	CHECK(after.Mode == before.Mode);
	CHECK(after.Floats == before.Floats);
	CHECK(after.Strings == before.Strings);
}

TEST_CASE("Scripting: array proxies are live lists")
{
	ProxyFixture fixture;
	fixture.Run("local a = c.Floats a[#a + 1] = 2 a[#a + 1] = 3 a[1] = 1 c.Strings[#c.Strings + 1] = 'c' "
	            "c.Bools[1] = false c.Targets[2] = e c.Textures[1] = nil");
	CHECK(fixture.All().Floats == std::vector<float>{ 1.0f, 2.0f, 3.0f });
	CHECK(fixture.All().Strings == std::vector<std::string>{ "a", "b", "c" });
	CHECK(fixture.All().Bools == std::vector<bool>{ false, false });
	CHECK(fixture.All().Targets == std::vector<UUID>{ UUID(1), fixture.Target.GetUUID() });
	CHECK(fixture.All().Textures == std::vector<UUID>{ UUID() });

	// The proxy is live in both directions and one array can be assigned to another field of its kind.
	CHECK(fixture.Run("local a = c.Floats c.Floats = { 9 } return { #a, a[1] }") == fixture.Eval("return { 1, 9.0 }"));
	fixture.Run("c.Doubles = { 1, 2 } local d = c.Doubles c.Doubles = d c.Vec3s = { Vec3(1, 1, 1), Vec3(2, 2, 2) }");
	CHECK(fixture.All().Doubles == std::vector<double>{ 1.0, 2.0 });
	CHECK(
		fixture.Run("local sum = 0 for _, v in ipairs(c.Vec3s) do sum = sum + v.x end return sum") == ScriptValue(3.0));
	CHECK(Contains(
		*fixture.Run("return tostring(c.Floats)").TryGet<std::string>(), "Array(AllTypes.Floats, 1 elements)"));

	// An element that is gone raises a clear error instead of reading something else.
	const std::string gone = fixture.RunError("local v = c.Vec3s[2] c.Vec3s = { Vec3(0, 0, 0) } return v.x");
	CHECK_MESSAGE(Contains(gone, "AllTypes.Vec3s[2] no longer exists (the array has 1 elements)"), gone);
}

TEST_CASE("Scripting: struct proxies read, write and merge")
{
	ProxyFixture fixture;
	fixture.Structs().Main.Unreflected = 77;

	// Reading: members, nested structs, enums, colors and arrays inside a struct.
	CHECK(
		fixture.Run("local m = s.Main return { m.Clip, m.Weight, m.Mode, m.Stats.Level, m.Stats.Label, #m.Curve, "
	                "m.Curve[2], #m.History, #s.Layers, s.Stats[1].Label, s.Source, s.Sources[2] }")
		== fixture.Eval("return { 'idle', 1.0, 'Slow', 3, 'stat', 2, 1.0, 0, 0, 'nine', '0x000000000000BEEF', "
	                    "'0x0000000000000002' }"));
	CHECK(fixture.Run("return s.Main.Tint") == ScriptValue(ScriptColorValue{ glm::vec4(1.0f, 0.5f, 0.25f, 1.0f) }));
	CHECK(fixture.Run("return s.Main.Target") == ScriptValue(ScriptEntityValue{ UUID(0x77) }));

	// Writing members, at any depth.
	fixture.Run("s.Main.Clip = 'run' s.Main.Weight = 0.5 s.Main.Mode = 'Fast' s.Main.Tint.g = 1 "
	            "s.Main.Stats.Level = 7 s.Main.Stats.Label = 'seven' s.Main.Curve[3] = 2 s.Main.Curve[1] = 0.5 "
	            "s.Main.Target = e s.Stats[1].Speed = 4 s.Source = '0x1' s.Sources = { '0x5' }");
	const TestLayer& main = fixture.Structs().Main;
	CHECK(main.Clip == "run");
	CHECK(main.Weight == 0.5f);
	CHECK(main.Mode == TestMode::Fast);
	CHECK(main.Tint == glm::vec3(1.0f, 1.0f, 0.25f));
	CHECK(main.Stats.Level == 7);
	CHECK(main.Stats.Label == "seven");
	CHECK(main.Curve == std::vector<float>{ 0.5f, 1.0f, 2.0f });
	CHECK(main.Target == fixture.Target.GetUUID());
	CHECK(main.Unreflected == 77); // what reflection does not know stays
	CHECK(fixture.Structs().Stats[0].Speed == 4.0f);
	CHECK(fixture.Structs().Stats[0].Level == 9);
	CHECK(fixture.Structs().Source == UUID(1));
	CHECK(fixture.Structs().Sources == std::vector<UUID>{ UUID(5) });

	// A table merges into the struct: only the members it names change (D-055), nested ones too.
	fixture.Run("s.Main = { Weight = 0.25, Stats = { Speed = 9 } }");
	CHECK(main.Clip == "run");
	CHECK(main.Weight == 0.25f);
	CHECK(main.Stats.Level == 7);
	CHECK(main.Stats.Speed == 9.0f);
	CHECK(main.Unreflected == 77);

	// Arrays of structs: append with a table (defaults for the rest), change in place, replace whole.
	fixture.Run("s.Layers[1] = { Clip = 'a' } s.Layers[2] = { Clip = 'b', Stats = { Level = 2 } } "
	            "s.Layers[1].Weight = 0.75 s.Layers[2].Stats.Label = 'two' s.Layers[1] = { Mode = 'Off' } "
	            "s.Layers[2].History[1] = { Level = 5 }");
	REQUIRE(fixture.Structs().Layers.size() == 2);
	CHECK(fixture.Structs().Layers[0].Clip == "a"); // merged into the element it replaces
	CHECK(fixture.Structs().Layers[0].Weight == 0.75f);
	CHECK(fixture.Structs().Layers[0].Mode == TestMode::Off);
	CHECK(fixture.Structs().Layers[1].Clip == "b");
	CHECK(fixture.Structs().Layers[1].Stats.Level == 2);
	CHECK(fixture.Structs().Layers[1].Stats.Label == "two");
	REQUIRE(fixture.Structs().Layers[1].History.size() == 1);
	CHECK(fixture.Structs().Layers[1].History[0].Level == 5);
	CHECK(fixture.Structs().Layers[1].History[0].Label == "stat");

	// One struct can be assigned from another of its kind: a copy.
	fixture.Run("s.Layers[1] = s.Layers[2] s.Layers[2].Clip = 'changed' s.Main.Stats = s.Stats[1]");
	CHECK(fixture.Structs().Layers[0].Clip == "b");
	CHECK(fixture.Structs().Layers[0].Stats.Label == "two");
	CHECK(fixture.Structs().Layers[1].Clip == "changed");
	CHECK(main.Stats.Level == 9);
	fixture.Run("s.Layers = { { Clip = 'only' } }");
	REQUIRE(fixture.Structs().Layers.size() == 1);
	CHECK(fixture.Structs().Layers[0].Clip == "only");
	CHECK(fixture.Structs().Layers[0].Stats.Label == "two"); // a table merges into the element at its place

	const auto check = [&](std::string_view code, std::string_view expected) {
		INFO(code);
		const std::string message = fixture.RunError(code);
		CHECK_MESSAGE(Contains(message, expected), message);
	};
	check("s.Main.Nope = 1", "Structs.Main has no field 'Nope' (fields: Clip, Weight");
	check("return s.Main.Stats.Nope", "Structs.Main.Stats has no field 'Nope'");
	check("s.Main = { Nope = 1 }", "Structs.Main: the struct TestLayer has no field 'Nope'");
	check("s.Main = { Weight = 'heavy' }", "Structs.Main.Weight: expected a number, got string");
	check("s.Main = { 1, 2 }", "Structs.Main: struct fields are named by strings, got a number key");
	check("s.Main = 5", "Structs.Main: expected a TestLayer struct (a table of its fields), got number");
	check("s.Main = s.Main.Stats", "Structs.Main: expected a TestLayer struct, got a TestStats");
	check("s.Layers[1].Stats.Level = 'x'", "Structs.Layers[1].Stats.Level: expected an integer, got string");
	check("s.Layers[3] = {}", "Structs.Layers: index 3 is out of range (1 to 2 to append)");
	check("local layer = s.Layers[1] s.Layers = {} return layer.Clip", "Structs.Layers[1] no longer exists");
	CHECK(Contains(*fixture.Run("return tostring(s.Main.Stats)").TryGet<std::string>(), "Struct(Structs.Main.Stats)"));
}

TEST_CASE("Scripting: proxies of a destroyed entity or a removed component raise an error")
{
	ProxyFixture fixture;
	const std::string id = fixture.Target.GetUUID().ToString();
	const auto stale = [&](std::string_view setup, std::string_view use) {
		return fixture.RunError(std::string(setup) + " " + std::string(use));
	};

	const std::string removed = stale("e:Remove('AllTypes')", "return c.Int32");
	CHECK_MESSAGE(Contains(removed, "entity " + id + " ('E') no longer has a AllTypes component"), removed);
	CHECK_MESSAGE(Contains(removed, "(evaluate):1:"), removed);
	fixture.Target.AddComponent<AllTypesComponent>();

	CHECK(Contains(stale("e:Remove('AllTypes')", "c.Int32 = 1"), "no longer has a AllTypes component"));
	fixture.Target.AddComponent<AllTypesComponent>();
	CHECK(Contains(stale("local v = c.Vec3 e:Remove('AllTypes')", "return v.x"), "no longer has a AllTypes component"));
	fixture.Target.AddComponent<AllTypesComponent>();
	CHECK(Contains(stale("local v = c.Vec3 e:Remove('AllTypes')", "v.x = 1"), "no longer has a AllTypes component"));
	fixture.Target.AddComponent<AllTypesComponent>();
	CHECK(Contains(stale("local a = c.Floats e:Remove('AllTypes')", "return #a"), "no longer has a AllTypes"));
	fixture.Target.AddComponent<AllTypesComponent>();
	CHECK(Contains(stale("local m = s.Main e:Remove('Structs')", "return m.Clip"), "no longer has a Structs"));
	fixture.Target.AddComponent<StructsComponent>();

	// The component came back: the proxy works again, on the new component.
	CHECK(
		fixture.Run("e:Remove('AllTypes') e:Add('AllTypes', { Int32 = 5 }) return c.Int32")
		== ScriptValue(int64_t{ 5 }));

	const std::string destroyed = stale("local v = c.Vec3 e:Destroy()", "return v.x");
	CHECK_MESSAGE(Contains(destroyed, "AllTypes of entity " + id + ": the entity no longer exists"), destroyed);
	CHECK_FALSE(fixture.Scene->ContainsEntity(UUID::Parse(id).value()));
}

TEST_CASE("Scripting: engine-managed components are read-only and proxies compare by component")
{
	ProxyFixture fixture;
	const std::string id = fixture.Target.GetUUID().ToString();
	CHECK(fixture.Run("return e:Get('ID').ID == e") == ScriptValue(true));
	CHECK(Contains(fixture.RunError("e:Get('ID').ID = '0x5'"), "the ID component is managed by the engine"));
	CHECK(Contains(
		fixture.RunError("local r = e:Get('Relationship') r.Children[1] = '0x5'"),
		"the Relationship component is managed by the engine"));
	CHECK(fixture.Target.GetUUID().ToString() == id);

	// The name is the Tag component.
	fixture.Run("e:Get('Tag').Name = 'Renamed'");
	CHECK(fixture.Target.GetName() == "Renamed");
	fixture.Target.SetName("E");

	CHECK(fixture.Run("return e:Get('AllTypes') == c and e:Get('Structs') ~= c") == ScriptValue(true));
	CHECK(fixture.Run("return tostring(c)") == ScriptValue("Component(AllTypes of " + id + ")"));

	// Script components go through the same proxies.
	ScriptFixture scripted;
	scripted.Start("Component('Spin', { Speed = 1.5, Axis = Vec3(0, 1, 0), Mode = Enum({ 'Local', 'World' }), "
	               "Weights = Array(Float, { 1 }), Target = EntityRef() })");
	CHECK(
		scripted.Eval(
			"local world = ... local e = world:Spawn('S', { Spin = { Speed = 2 } }) local spin = e:Get('Spin') "
			"spin.Axis.x = 3 spin.Mode = 'World' spin.Weights[2] = 4 spin.Target = e "
			"return { spin.Speed, spin.Axis:Length() > 3, spin.Mode, #spin.Weights, spin.Target == e }")
		== scripted.Eval("return { 2.0, true, 'World', 2, true }"));
	CHECK(
		Contains(scripted.EvalError("local world = ... world:Find('S'):Get('Spin').Speed = 0 / 0"), "Spin.Speed: nan"));
}
