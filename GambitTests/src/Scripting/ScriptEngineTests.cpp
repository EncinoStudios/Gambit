#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"
#include "TestSupport.h"

#include "Gambit/Math/DMath.h"
#include "Gambit/Math/Random.h"

#include <bit>
#include <cmath>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Scripting: the sandbox has no unsafe or non-deterministic libraries")
{
	ScriptFixture fixture;
	fixture.Create();
	for (const char* name :
	     { "io", "os", "debug", "package", "dofile", "loadfile", "collectgarbage", "string.dump", "utf8.nothing" })
	{
		INFO(name);
		CHECK(fixture.Eval(std::string("return ") + name + " == nil") == ScriptValue(true));
	}
	for (const char* name : { "string", "table", "math", "utf8", "coroutine", "require", "load", "print", "pcall" })
	{
		INFO(name);
		CHECK(fixture.Eval(std::string("return ") + name + " ~= nil") == ScriptValue(true));
	}
	CHECK(fixture.Eval("return string.format('%d-%s', 4, 'x')") == ScriptValue(std::string("4-x")));
	CHECK(fixture.Eval("return utf8.len('h\\u{E9}llo')") == ScriptValue(int64_t{ 5 }));
	CHECK(
		fixture.Eval("local co = coroutine.wrap(function() coroutine.yield(3) end) return co()")
		== ScriptValue(int64_t{ 3 }));
}

TEST_CASE("Scripting: load accepts text chunks only")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return load('return 1 + 1')()") == ScriptValue(int64_t{ 2 }));
	CHECK(fixture.Eval("return load('return x', 'chunk', 't', { x = 5 })()") == ScriptValue(int64_t{ 5 }));

	// A binary chunk (the signature of precompiled Lua) is refused, whatever the mode says.
	const ScriptValue binary = fixture.Eval("local f, message = load('\\27Lua\\x54\\0') return { f == nil, message }");
	REQUIRE(binary.Is<ScriptTable>());
	CHECK(binary.TryGet<ScriptTable>()->at(0).Value == ScriptValue(true));
	CHECK(Contains(*binary.TryGet<ScriptTable>()->at(1).Value.TryGet<std::string>(), "binary"));
	const ScriptValue mode = fixture.Eval("local f, message = load('return 1', 'x', 'b') return { f == nil, message }");
	CHECK(mode.TryGet<ScriptTable>()->at(0).Value == ScriptValue(true));
	CHECK(Contains(*mode.TryGet<ScriptTable>()->at(1).Value.TryGet<std::string>(), "text only"));
	CHECK(Contains(fixture.EvalError("return load(function() return nil end)"), "must be a string"));

	const ScriptValue syntax = fixture.Eval("local f, message = load('return +', 'Bad') return message");
	CHECK(Contains(*syntax.TryGet<std::string>(), "[string \"Bad\"]:1:"));
}

TEST_CASE("Scripting: the globals are frozen once the scripts are loaded")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(
		engine.LoadScript("Scripts/Globals.lua", "Counter = 1\nfunction Helper() return Counter + 1 end").HasValue());
	CHECK(fixture.Eval("Other = 2 return Other") == ScriptValue(int64_t{ 2 })); // still loading
	engine.FinishLoading();
	CHECK(engine.IsLoadingFinished());

	CHECK(fixture.Eval("return Helper()") == ScriptValue(int64_t{ 2 }));
	CHECK(Contains(fixture.EvalError("Counter = 5"), "cannot assign the global 'Counter'"));
	CHECK(Contains(fixture.EvalError("NewGlobal = 5"), "cannot assign the global 'NewGlobal'"));
	CHECK(Contains(fixture.EvalError("Helper = nil"), "(evaluate):1:"));
	CHECK(Contains(fixture.EvalError("rawset(_G, 'Sneaky', 1)"), "cannot assign the global 'Sneaky'"));
	CHECK(Contains(fixture.EvalError("setmetatable(_G, {})"), "protected metatable"));
	CHECK(fixture.Eval("return rawget(_G, 'Counter')") == ScriptValue(int64_t{ 1 }));
	CHECK(fixture.Eval("local t = {} rawset(t, 'a', 1) return rawget(t, 'a')") == ScriptValue(int64_t{ 1 }));
	CHECK(
		fixture.Eval("local count = 0 for k in pairs(_G) do count = count + 1 end return count > 10")
		== ScriptValue(true));

	// Locals and tables still work: only globals are frozen.
	CHECK(fixture.Eval("local t = { 1 } t[2] = 2 return #t") == ScriptValue(int64_t{ 2 }));
	CHECK_FALSE(engine.LoadScript("Scripts/Late.lua", "local x = 1").HasValue());
}

TEST_CASE("Scripting: pairs over the globals shows no sol2 internals")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	const auto listGlobals = [&] {
		return Strings(fixture.Eval("local names = {} for k in pairs(_G) do names[#names + 1] = tostring(k) end "
		                            "table.sort(names) return names"));
	};
	for (int pass = 0; pass < 2; ++pass)
	{
		const std::vector<std::string> names = listGlobals();
		CHECK(std::ranges::find(names, "print") != names.end());
		CHECK(std::ranges::find(names, "Vec3") != names.end());
		CHECK(std::ranges::none_of(names, [](const std::string& name) { return name.starts_with("sol."); }));
		engine.FinishLoading();
	}
}

TEST_CASE("Scripting: metatables with finalizers or weak keys are refused")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(Contains(fixture.EvalError("setmetatable({}, { __gc = function() end })"), "__gc"));
	CHECK(Contains(fixture.EvalError("setmetatable({}, { __mode = 'k' })"), "__mode"));
	CHECK(
		fixture.Eval("return getmetatable(setmetatable({}, { __index = { a = 1 } })).__index.a")
		== ScriptValue(int64_t{ 1 }));
}

TEST_CASE("Scripting: errors carry the file and line and are recorded")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();

	SUBCASE("runtime error")
	{
		const Result<void> loaded = engine.LoadScript("Scripts/Bad.lua", "local x = nil\nlocal y = x.field\n");
		REQUIRE_FALSE(loaded.HasValue());
		CHECK(Contains(loaded.GetError().Message, "Scripts/Bad.lua:2:"));
		REQUIRE(engine.GetErrors().size() == 1);
		const ScriptError& error = engine.GetErrors().front();
		CHECK(error.File == "Scripts/Bad.lua");
		CHECK(error.Line == 2);
		CHECK(error.Context == "loading Scripts/Bad.lua");
		CHECK(Contains(error.Message, "attempt to index a nil value"));
		CHECK(Contains(error.Traceback, "Scripts/Bad.lua:2"));
	}
	SUBCASE("syntax error")
	{
		REQUIRE_FALSE(engine.LoadScript("Scripts/Syntax.lua", "\n\nlocal = 1").HasValue());
		REQUIRE(engine.GetErrors().size() == 1);
		CHECK(engine.GetErrors().front().File == "Scripts/Syntax.lua");
		CHECK(engine.GetErrors().front().Line == 3);
	}
	SUBCASE("an error raised by a C++ binding")
	{
		REQUIRE_FALSE(engine.LoadScript("Scripts/Binding.lua", "\nlocal v = Vec3(1, 2)\n").HasValue());
		REQUIRE(engine.GetErrors().size() == 1);
		const ScriptError& error = engine.GetErrors().front();
		CHECK(error.File == "Scripts/Binding.lua");
		CHECK(error.Line == 2);
		CHECK(Contains(error.Message, "Vec3: expected 0 or 3 numbers"));
	}
	SUBCASE("an argument of the wrong type")
	{
		REQUIRE_FALSE(engine.LoadScript("Scripts/Type.lua", "local d = Vec3(1, 2, 3):Dot('x')").HasValue());
		REQUIRE(engine.GetErrors().size() == 1);
		CHECK(engine.GetErrors().front().File == "Scripts/Type.lua");
		CHECK(engine.GetErrors().front().Line == 1);
	}
	SUBCASE("an error object that is not a string")
	{
		REQUIRE_FALSE(engine.LoadScript("Scripts/Table.lua", "\nerror({ code = 1 })").HasValue());
		REQUIRE(engine.GetErrors().size() == 1);
		CHECK(Contains(engine.GetErrors().front().Message, "Scripts/Table.lua:2: (an error object of type table)"));
		CHECK(engine.GetErrors().front().Line == 2);
	}
	// The engine keeps working after an error.
	CHECK(fixture.Eval("return 40 + 2") == ScriptValue(int64_t{ 42 }));
}

TEST_CASE("Scripting: the watchdog stops endless loops")
{
	ScriptFixture fixture;
	ScriptEngineSpecification specification = fixture.MakeSpecification();
	specification.Limits.InstructionsPerCall = 200'000;
	ScriptEngine& engine = fixture.Create(std::move(specification));

	const Result<void> loop = engine.LoadScript("Scripts/Loop.lua", "local n = 0\nwhile true do\n  n = n + 1\nend");
	REQUIRE_FALSE(loop.HasValue());
	CHECK(Contains(loop.GetError().Message, "instruction limit of 200000 exceeded"));
	REQUIRE(engine.GetErrors().size() == 1);
	CHECK(engine.GetErrors().front().File == "Scripts/Loop.lua");
	CHECK(engine.GetErrors().front().Line >= 2);

	// Catching the error does not help a runaway script.
	CHECK(
		Contains(fixture.EvalError("while true do pcall(function() while true do end end) end"), "instruction limit"));
	CHECK(Contains(
		fixture.EvalError("local co = coroutine.wrap(function() while true do end end) co()"), "instruction limit"));
	// Every call gets a fresh budget.
	CHECK(fixture.Eval("local n = 0 for i = 1, 1000 do n = n + i end return n") == ScriptValue(int64_t{ 500500 }));
}

TEST_CASE("Scripting: the memory limit stops runaway allocations")
{
	ScriptFixture fixture;
	ScriptEngineSpecification specification = fixture.MakeSpecification();
	specification.Limits.MemoryBytes = size_t{ 8 } << 20;
	ScriptEngine& engine = fixture.Create(std::move(specification));
	REQUIRE(engine.GetMemoryUsage() < (size_t{ 8 } << 20));

	const std::string message =
		fixture.EvalError("local t = {} for i = 1, 100000000 do t[i] = { i, tostring(i) } end return #t");
	CHECK(Contains(message, "ran out of memory"));
	CHECK(Contains(message, std::to_string(size_t{ 8 } << 20)));
	CHECK(fixture.Eval("return 'still alive'") == ScriptValue(std::string("still alive")));
	CHECK(Contains(fixture.EvalError("return string.rep('x', 64 * 1024 * 1024)"), "ran out of memory"));
}

TEST_CASE("Scripting: the limits must not be zero and a simulation engine needs a world")
{
	ScriptFixture fixture;
	ScriptEngineSpecification noWorld = fixture.MakeSpecification();
	noWorld.Scene = nullptr;
	CHECK_FALSE(ScriptEngine::Create(noWorld).HasValue());
	ScriptEngineSpecification schemaWithWorld = fixture.MakeSpecification(ScriptMode::Schema);
	schemaWithWorld.Scene = fixture.Scene.get();
	CHECK_FALSE(ScriptEngine::Create(schemaWithWorld).HasValue());
	ScriptEngineSpecification zero = fixture.MakeSpecification();
	zero.Limits.InstructionsPerCall = 0;
	CHECK_FALSE(ScriptEngine::Create(zero).HasValue());
}

TEST_CASE("Scripting: math.random draws from the world's random generator")
{
	ScriptFixture fixture;
	fixture.Scene->SetSeed(42);
	fixture.Create();
	Random expected(42);

	const ScriptValue values =
		fixture.Eval("return { math.random(), math.random(1, 6), math.random(10), math.random(0) }");
	const ScriptTable& table = *values.TryGet<ScriptTable>();
	REQUIRE(table.size() == 4);
	CHECK(
		std::bit_cast<uint64_t>(*table[0].Value.TryGet<double>()) == std::bit_cast<uint64_t>(expected.NextDouble01()));
	CHECK(*table[1].Value.TryGet<int64_t>() == expected.RangeInt(int64_t{ 1 }, int64_t{ 6 }));
	CHECK(*table[2].Value.TryGet<int64_t>() == expected.RangeInt(int64_t{ 1 }, int64_t{ 10 }));
	CHECK(*table[3].Value.TryGet<int64_t>() == static_cast<int64_t>(expected.NextU64()));
	CHECK(fixture.Scene->GetRandom().GetState() == expected.GetState());

	fixture.Eval("math.randomseed(7)");
	CHECK(fixture.Scene->GetRandom().GetState() == Random(7).GetState());
	CHECK(Contains(fixture.EvalError("math.randomseed()"), "needs a seed"));
	CHECK(Contains(fixture.EvalError("return math.random(3, 1)"), "interval is empty"));
}

TEST_CASE("Scripting: the math library is DMath, bit for bit")
{
	ScriptFixture fixture;
	fixture.Create();
	const std::vector<double> inputs = { 0.0, 0.5, -1.25, 3.0, 100.75, 1e-7, 12345.678 };
	const auto bits = [](double value) { return std::bit_cast<uint64_t>(value); };
	for (const double x : inputs)
	{
		INFO("x = ", x);
		const std::string literal = std::format("{:.17g}", x);
		const auto eval = [&](const std::string& code) { return Number(fixture.Eval("return " + code)); };
		CHECK(bits(eval("math.sin(" + literal + ")")) == bits(DMath::Sin(x)));
		CHECK(bits(eval("math.cos(" + literal + ")")) == bits(DMath::Cos(x)));
		CHECK(bits(eval("math.tan(" + literal + ")")) == bits(DMath::Tan(x)));
		CHECK(bits(eval("math.exp(" + literal + ")")) == bits(DMath::Exp(x)));
		CHECK(bits(eval("math.atan(" + literal + ", 2.5)")) == bits(DMath::Atan2(x, 2.5)));
		CHECK(bits(eval("(" + literal + ") ^ 1.5")) == bits(DMath::Pow(x, 1.5)));
		if (x > 0.0)
		{
			CHECK(bits(eval("math.log(" + literal + ")")) == bits(DMath::Log(x)));
			CHECK(bits(eval("math.log(" + literal + ", 2)")) == bits(DMath::Log2(x)));
		}
		if (std::abs(x) <= 1.0)
		{
			CHECK(bits(eval("math.asin(" + literal + ")")) == bits(DMath::Asin(x)));
			CHECK(bits(eval("math.acos(" + literal + ")")) == bits(DMath::Acos(x)));
		}
	}
	CHECK(fixture.Eval("return 3 ^ 2") == ScriptValue(9.0));
}

TEST_CASE("Scripting: print and warn go to the log")
{
	ScopedLogCapture capture;
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine.LoadScript("Scripts/Print.lua", "\nprint('hello', 1, true)\nwarn('careful', ' now')").HasValue());
	CHECK(capture.Contains(LogLevel::Info, "Scripts/Print.lua:2: hello\t1\ttrue"));
	CHECK(capture.Contains(LogLevel::Warn, "[Lua] careful now"));
}

TEST_CASE("Scripting: require finds project modules and the engine library")
{
	ScriptFixture fixture;
	fixture.Write(
		"Scripts/Util/Counter.lua", "local Counter = { Loads = 0 }\nCounter.Loads = Counter.Loads + 1\nreturn Counter");
	fixture.Write("Scripts/Main.lua", "local Counter = require('Util.Counter')\nreturn Counter");
	fixture.Write("Scripts/Second.lua", "assert(require('Util.Counter') == require('Util.Counter'))");
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine.LoadScripts().HasValue());
	CHECK(fixture.Eval("return require('Util.Counter').Loads") == ScriptValue(int64_t{ 1 }));
	CHECK(fixture.Eval("return require('Gambit.SortedPairs') == SortedPairs") == ScriptValue(true));
	CHECK(fixture.Eval("return require('Gambit.Table') == Table") == ScriptValue(true));

	CHECK(Contains(
		fixture.EvalError("require('Missing.Module')"),
		"module 'Missing.Module' not found (Scripts/Missing/Module.lua"));
	CHECK(Contains(fixture.EvalError("require('Gambit.Nothing')"), "no module 'Gambit.Nothing'"));
	CHECK(Contains(fixture.EvalError("require('../etc')"), "is not a module name"));
}

TEST_CASE("Scripting: require reports cycles and errors in modules")
{
	ScriptFixture fixture;
	fixture.Write("Scripts/A.lua", "return require('B')");
	fixture.Write("Scripts/B.lua", "return require('A')");
	fixture.Write("Scripts/Broken.lua", "\n\nerror('broken module')");
	ScriptEngine& engine = fixture.Create();
	const Result<void> loaded = engine.LoadScripts();
	REQUIRE_FALSE(loaded.HasValue());
	CHECK(Contains(loaded.GetError().Message, "3 scripts failed"));
	REQUIRE(engine.GetErrors().size() == 3);
	// The error is where the loaded script failed (its require), and the message has the chain of locations.
	CHECK(engine.GetErrors()[0].File == "Scripts/A.lua");
	CHECK(engine.GetErrors()[0].Line == 1);
	CHECK(Contains(
		engine.GetErrors()[0].Message, "loading module 'B': Scripts/B.lua:1: require: module 'A' requires itself"));
	CHECK(engine.GetErrors()[2].File == "Scripts/Broken.lua");
	CHECK(engine.GetErrors()[2].Line == 3);
	CHECK(Contains(fixture.EvalError("require('Broken')"), "Scripts/Broken.lua:3: broken module"));
}

TEST_CASE("Scripting: scripts load once each, in path order")
{
	ScriptFixture fixture;
	fixture.Write("Scripts/Order.lua", "return {}");
	fixture.Write("Scripts/B.lua", "table.insert(require('Order'), 'B')");
	fixture.Write("Scripts/A.lua", "table.insert(require('Order'), 'A')");
	fixture.Write("Scripts/Sub/C.lua", "table.insert(require('Order'), 'C')");
	fixture.Write("Scripts/Notes.txt", "not a script");
	fixture.Write("Scripts/My Script.lua", "table.insert(require('Order'), 'D')");
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine.LoadScripts().HasValue());
	CHECK(Strings(fixture.Eval("return require('Order')")) == std::vector<std::string>{ "A", "B", "D", "C" });
}

TEST_CASE("Scripting: Evaluate returns plain data")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(fixture.Eval("return nil").IsNil());
	CHECK(fixture.Eval("return 1.5") == ScriptValue(1.5));
	CHECK(fixture.Eval("return 7") == ScriptValue(int64_t{ 7 }));
	CHECK(fixture.Eval("return 'text'") == ScriptValue(std::string("text")));
	CHECK(fixture.Eval("return Vec3(1, 2, 3)") == ScriptValue(glm::vec3(1.0f, 2.0f, 3.0f)));
	CHECK(fixture.Eval("return Color(1, 0, 0)") == ScriptValue(ScriptColorValue{ glm::vec4(1.0f, 0.0f, 0.0f, 1.0f) }));
	const ScriptValue table = fixture.Eval("return { b = 2, a = 1, [3] = 'x', [true] = false, n = { 1, 2 } }");
	CHECK(ToString(table) == "{ [true] = false, [3] = \"x\", a = 1, b = 2, n = { [1] = 1, [2] = 2 } }");
	CHECK(Contains(fixture.EvalError("return function() end"), "not plain data"));
	CHECK(Contains(fixture.EvalError("local t = {} t.self = t return t"), "nested deeper"));
	CHECK(Contains(fixture.EvalError("return { [{}] = 1 }"), "key of type table"));
}

TEST_CASE("Scripting: SortedPairs and Table visit keys in a fixed order")
{
	ScriptFixture fixture;
	fixture.Create();
	CHECK(
		Strings(fixture.Eval(R"(
		local keys = {}
		for key, value in SortedPairs({ b = 1, a = 2, [2] = 3, [1] = 4, [true] = 5 }) do
			keys[#keys + 1] = tostring(key) .. "=" .. tostring(value)
		end
		return keys)"))
		== std::vector<std::string>{ "true=5", "1=4", "2=3", "a=2", "b=1" });
	CHECK(Contains(fixture.EvalError("for k in SortedPairs({ [{}] = 1 }) do end"), "has no natural order"));
	CHECK(
		fixture.Eval("local original = { a = { b = 1 } } local copy = Table.Copy(original) copy.a.b = 2 "
	                 "return original.a.b")
		== ScriptValue(int64_t{ 1 }));
	CHECK(
		fixture.Eval("local t = { 1 } t.self = t local copy = Table.Copy(t) return copy.self == copy")
		== ScriptValue(true));
	CHECK(Strings(fixture.Eval("return Table.Keys({ z = 1, y = 2 })")) == std::vector<std::string>{ "y", "z" });
	CHECK(fixture.Eval("return Table.Count({ 1, 2, x = 3 })") == ScriptValue(int64_t{ 3 }));
}
