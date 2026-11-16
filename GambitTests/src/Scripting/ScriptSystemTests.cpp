#include <doctest/doctest.h>

#include "Scripting/ScriptTestSupport.h"
#include "TestSupport.h"

#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Scripting/ScriptSystem.h"

#include <string>
#include <utility>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// A C++ system that appends its name to the Strings of every AllTypes component, as the script systems of these
	// tests do: the list is the order in which the systems ran.
	class RecordingSystem final : public ISystem
	{
	public:
		RecordingSystem(std::string name, SystemPhase phase, int32_t order = 0)
			: m_Name(std::move(name)), m_Phase(phase), m_Order(order)
		{}

		[[nodiscard]] std::string_view GetName() const override { return m_Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return m_Phase; }
		[[nodiscard]] int32_t GetOrder() const override { return m_Order; }

		void OnUpdate(World& world, TimeStep) override
		{
			for (auto [entity, all] : world.View<AllTypesComponent>().each())
			{
				all.Strings.push_back(m_Name);
			}
		}

	private:
		std::string m_Name;
		SystemPhase m_Phase;
		int32_t m_Order;
	};

	constexpr const char* RecordScript = R"lua(
local function Record(name)
	return function(world, dt)
		for _, all in world:Query("AllTypes") do
			all.Strings[#all.Strings + 1] = name
		end
	end
end
)lua";

	// The entity whose AllTypes.Strings the systems write, with an empty list.
	Entity MakeRecorder(World& world)
	{
		Entity entity = world.CreateEntity("Recorder");
		entity.AddComponent<AllTypesComponent>().Strings.clear();
		return entity;
	}

	std::vector<std::string> TakeRecord(Entity recorder)
	{
		return std::exchange(recorder.GetComponent<AllTypesComponent>().Strings, {});
	}

	// The integer field of the first Counter component (a script component of these tests).
	int64_t Counter(ScriptFixture& fixture, std::string_view field)
	{
		const ScriptValue value =
			fixture.Eval("local _, counter = (...):QueryFirst('Counter') return counter." + std::string(field));
		REQUIRE(value.Is<int64_t>());
		return *value.TryGet<int64_t>();
	}
}

TEST_CASE("Scripting: script systems run in phase, order and name order with C++ systems")
{
	ScriptFixture fixture;
	Entity recorder = MakeRecorder(*fixture.Scene);
	SystemScheduler& scheduler = fixture.Scene->GetScheduler();
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("C_Cpp", SystemPhase::Update)).HasValue());
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("Late_Cpp", SystemPhase::LateUpdate, -10)).HasValue());

	ScriptEngine& engine = fixture.Start(std::string(RecordScript) + R"lua(
System("D_Lua", { OnUpdate = Record("D_Lua") })
System("B_Lua", { OnUpdate = Record("B_Lua") })
System("Early_Lua", { Phase = "PreUpdate", Order = 100, OnUpdate = Record("Early_Lua") })
System("First_Lua", { Order = -1, OnUpdate = Record("First_Lua") })
System("Last_Lua", { Phase = "PreRender", OnUpdate = Record("Last_Lua") })
)lua");
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("A_Cpp", SystemPhase::Update)).HasValue());

	fixture.Tick();
	CHECK(
		TakeRecord(recorder)
		== std::vector<std::string>{ "Early_Lua", "First_Lua", "A_Cpp", "B_Lua", "C_Cpp", "D_Lua", "Late_Cpp",
	                                 "Last_Lua" });

	// The scheduler knows them like any system; the runtime system comes first of all.
	const std::vector<ISystem*> systems = scheduler.GetSystems();
	REQUIRE(systems.size() == 9);
	CHECK(systems.front()->GetName() == ScriptRuntimeSystem::Name);
	CHECK(systems.front()->GetPhase() == SystemPhase::Input);
	const ISystem* early = scheduler.FindSystem("Early_Lua");
	REQUIRE(early != nullptr);
	CHECK(early->GetPhase() == SystemPhase::PreUpdate);
	CHECK(early->GetOrder() == 100);
	CHECK(dynamic_cast<const ScriptSystem*>(early) != nullptr);
	CHECK(ScriptEngine::Find(*fixture.Scene) == &engine);

	// A disabled script system is skipped like a C++ one.
	REQUIRE(scheduler.SetSystemEnabled("B_Lua", false));
	fixture.Tick();
	CHECK(
		TakeRecord(recorder)
		== std::vector<std::string>{ "Early_Lua", "First_Lua", "A_Cpp", "C_Cpp", "D_Lua", "Late_Cpp", "Last_Lua" });
	CHECK(engine.GetErrors().empty());
}

TEST_CASE("Scripting: only systems that ask for it run in edit mode")
{
	ScriptFixture fixture;
	Entity recorder = MakeRecorder(*fixture.Scene);
	fixture.Start(
		std::string(RecordScript)
		+ "System('Play', { OnUpdate = Record('Play') })\n"
		  "System('Edit', { RunInEditMode = true, OnUpdate = Record('Edit') })");

	fixture.Scene->Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	CHECK(TakeRecord(recorder) == std::vector<std::string>{ "Edit" });
	CHECK(fixture.Scene->GetTick() == 0);
	fixture.Tick();
	CHECK(TakeRecord(recorder) == std::vector<std::string>{ "Edit", "Play" });
}

TEST_CASE("Scripting: systems receive the world and the time step, and their callbacks run once each")
{
	ScriptFixture fixture;
	fixture.Start(R"lua(
Component("Life", { Lines = Array(String), Elapsed = Double(0), Same = Bool(true) })
System("Life", {
	OnCreate = function(world)
		world:Spawn("Life", { Life = { Lines = { "create" } } })
	end,
	OnUpdate = function(world, dt)
		local _, life = world:QueryFirst("Life")
		life.Lines[#life.Lines + 1] = "update " .. world.Tick
		life.Elapsed = life.Elapsed + dt
		life.Same = life.Same and dt == world.DeltaTime and math.type(dt) == "float"
	end,
	OnDestroy = function(world)
		local _, life = world:QueryFirst("Life")
		life.Lines[#life.Lines + 1] = "destroy"
	end,
})
)lua");
	const ComponentInfo* info = fixture.Types.FindComponent("Life");
	REQUIRE(info != nullptr);
	Entity life = fixture.Scene->FindEntityByName("Life");
	REQUIRE(life); // OnCreate ran when the system was added
	const auto lines = [&] { return std::get<std::vector<std::string>>(info->GetFieldValue(life, "Lines").value()); };
	CHECK(lines() == std::vector<std::string>{ "create" });

	fixture.Tick(2);
	CHECK(lines() == std::vector<std::string>{ "create", "update 1", "update 2" });
	CHECK(std::get<double>(info->GetFieldValue(life, "Elapsed").value()) == fixture.Scene->GetTime());
	CHECK(std::get<bool>(info->GetFieldValue(life, "Same").value()));

	REQUIRE(fixture.Scene->GetScheduler().RemoveSystem("Life"));
	CHECK(lines() == std::vector<std::string>{ "create", "update 1", "update 2", "destroy" });
	CHECK(fixture.Engine->GetErrors().empty());
}

TEST_CASE("Scripting: a system that raises an error is disabled for the session")
{
	ScriptFixture fixture;
	const ScopedLogCapture capture;
	ScriptEngine& engine = fixture.Start(R"lua(
Component("Counter", { Good = Int(0), Bad = Int(0) })
System("Bad", {
	OnUpdate = function(world, dt)
		local _, counter = world:QueryFirst("Counter")
		counter.Bad = counter.Bad + 1
		if world.Tick == 2 then
			local missing = nil
			return missing.field
		end
	end,
})
System("Good", {
	Order = 1,
	OnUpdate = function(world, dt)
		local _, counter = world:QueryFirst("Counter")
		counter.Good = counter.Good + 1
	end,
})
)lua");
	fixture.Eval("(...):Spawn('Counter', { Counter = {} })");
	SystemScheduler& scheduler = fixture.Scene->GetScheduler();

	fixture.Tick();
	CHECK(engine.GetErrors().empty());
	fixture.Tick(3);

	// The failing system stopped at its error; the rest of the tick and the later ticks went on without it.
	CHECK(Counter(fixture, "Bad") == 2);
	CHECK(Counter(fixture, "Good") == 4);
	CHECK_FALSE(scheduler.IsSystemEnabled("Bad"));
	CHECK(scheduler.IsSystemEnabled("Good"));
	const auto* bad = dynamic_cast<const ScriptSystem*>(scheduler.FindSystem("Bad"));
	REQUIRE(bad != nullptr);
	CHECK(bad->HasFailed());

	REQUIRE(engine.GetErrors().size() == 1);
	const ScriptError& error = engine.GetErrors().front();
	CHECK(error.File == "Scripts/Main.lua");
	CHECK(error.Line == 9);
	CHECK(error.Context == "Bad.OnUpdate");
	CHECK(error.Tick == 2);
	CHECK_MESSAGE(Contains(error.Message, "Scripts/Main.lua:9: attempt to index a nil value"), error.Message);
	CHECK(Contains(error.Traceback, "Scripts/Main.lua:9"));
	CHECK(capture.Contains(LogLevel::Error, "Bad.OnUpdate: Scripts/Main.lua:9:"));
	CHECK(capture.Contains(LogLevel::Error, "system 'Bad' is disabled for this session"));

	// Enabling it again by hand does not bring it back: it failed.
	REQUIRE(scheduler.SetSystemEnabled("Bad", true));
	fixture.Tick();
	CHECK(Counter(fixture, "Bad") == 2);
	CHECK(engine.GetErrors().size() == 1);
}

TEST_CASE("Scripting: errors raised by bindings inside a system carry the script line")
{
	ScriptFixture fixture;
	const ScopedLogCapture capture;
	ScriptEngine& engine = fixture.Start(R"lua(
System("Typo", {
	OnUpdate = function(world, dt)
		for entity, health in world:Query("Health") do
			health.Curent = 5
		end
	end,
})
System("Stale", {
	OnUpdate = function(world, dt)
		local entity = world:Spawn("Gone")
		entity:Destroy()
		entity.Name = "Back"
	end,
})
)lua");
	fixture.Scene->CreateEntity("E").AddComponent<HealthComponent>();
	fixture.Tick(2);

	REQUIRE(engine.GetErrors().size() == 2);
	const ScriptError& stale = engine.GetErrors()[0]; // "Stale" sorts before "Typo"
	CHECK(stale.Context == "Stale.OnUpdate");
	CHECK(stale.File == "Scripts/Main.lua");
	CHECK(stale.Line == 13);
	CHECK_MESSAGE(Contains(stale.Message, "no longer exists"), stale.Message);
	const ScriptError& typo = engine.GetErrors()[1];
	CHECK(typo.Context == "Typo.OnUpdate");
	CHECK(typo.Line == 5);
	CHECK_MESSAGE(Contains(typo.Message, "Scripts/Main.lua:5: Health has no field 'Curent'"), typo.Message);
	CHECK(fixture.Scene->GetEntityCount() == 1); // the entity that Stale spawned is gone
}

TEST_CASE("Scripting: errors in OnCreate and OnDestroy are recorded and never escape")
{
	ScriptFixture fixture;
	const ScopedLogCapture capture;
	ScriptEngine& engine = fixture.Start(R"lua(
Component("Counter", { Updates = Int(0) })
System("BrokenCreate", {
	OnCreate = function(world) error("no start") end,
	OnUpdate = function(world, dt) error("never runs") end,
})
System("BrokenDestroy", {
	OnUpdate = function(world, dt)
		local _, counter = world:QueryFirst("Counter")
		counter.Updates = counter.Updates + 1
	end,
	OnDestroy = function(world) error("no end") end,
})
)lua");
	REQUIRE(engine.GetErrors().size() == 1);
	CHECK(engine.GetErrors()[0].Context == "BrokenCreate.OnCreate");
	CHECK(engine.GetErrors()[0].Line == 4);
	CHECK(Contains(engine.GetErrors()[0].Message, "Scripts/Main.lua:4: no start"));
	SystemScheduler& scheduler = fixture.Scene->GetScheduler();
	CHECK_FALSE(scheduler.IsSystemEnabled("BrokenCreate"));

	fixture.Eval("(...):Spawn('Counter', { Counter = {} })");
	fixture.Tick(2);
	CHECK(Counter(fixture, "Updates") == 2);
	CHECK(engine.GetErrors().size() == 1);

	REQUIRE(scheduler.RemoveSystem("BrokenDestroy"));
	REQUIRE(scheduler.RemoveSystem("BrokenCreate")); // its OnDestroy does not run: it never started
	REQUIRE(engine.GetErrors().size() == 2);
	CHECK(engine.GetErrors()[1].Context == "BrokenDestroy.OnDestroy");
	CHECK(Contains(engine.GetErrors()[1].Message, "no end"));
}

TEST_CASE("Scripting: the watchdog stops a system update that runs away")
{
	ScriptFixture fixture;
	const ScopedLogCapture capture;
	ScriptEngineSpecification specification = fixture.MakeSpecification();
	specification.Limits.InstructionsPerCall = 100'000;
	ScriptEngine& engine = fixture.Create(std::move(specification));
	REQUIRE(engine
	            .LoadScript("Scripts/Main.lua", R"lua(
Component("Counter", { Work = Int(0), After = Int(0) })
System("A_Busy", {
	OnUpdate = function(world, dt)
		local _, counter = world:QueryFirst("Counter")
		local sum = 0
		for i = 1, 10000 do sum = sum + i end -- well below the limit, every tick
		counter.Work = counter.Work + 1
	end,
})
System("B_Loop", {
	OnUpdate = function(world, dt)
		while true do end
	end,
})
System("C_After", {
	OnUpdate = function(world, dt)
		local _, counter = world:QueryFirst("Counter")
		counter.After = counter.After + 1
	end,
})
)lua")
	            .HasValue());
	engine.FinishLoading();
	REQUIRE(engine.AttachToWorld().HasValue());
	fixture.Eval("(...):Spawn('Counter', { Counter = {} })");

	fixture.Tick(5);
	// Every update has its own budget: the busy system never ran out, the endless one was stopped once.
	CHECK(Counter(fixture, "Work") == 5);
	CHECK(Counter(fixture, "After") == 5);
	REQUIRE(engine.GetErrors().size() == 1);
	const ScriptError& error = engine.GetErrors().front();
	CHECK(error.Context == "B_Loop.OnUpdate");
	CHECK(error.File == "Scripts/Main.lua");
	CHECK(error.Line == 13);
	CHECK(error.Tick == 1);
	CHECK_MESSAGE(Contains(error.Message, "instruction limit of 100000 exceeded"), error.Message);
	CHECK_FALSE(fixture.Scene->GetScheduler().IsSystemEnabled("B_Loop"));
}

TEST_CASE("Scripting: an engine attaches to its world once, after loading")
{
	ScriptFixture fixture;
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine.LoadScript("Scripts/Main.lua", "System('Mover', { OnUpdate = function() end })").HasValue());
	CHECK(ScriptEngine::Find(*fixture.Scene) == nullptr);

	const Result<void> early = engine.AttachToWorld();
	REQUIRE_FALSE(early.HasValue());
	CHECK(Contains(early.GetError().Message, "after its scripts are loaded"));

	engine.FinishLoading();
	REQUIRE(engine.AttachToWorld().HasValue());
	CHECK(ScriptEngine::Find(*fixture.Scene) == &engine);
	CHECK(fixture.Scene->GetScheduler().FindSystem("Mover") != nullptr);
	const Result<void> twice = engine.AttachToWorld();
	REQUIRE_FALSE(twice.HasValue());
	CHECK(Contains(twice.GetError().Message, "the world has a script engine already"));

	// The world's systems keep the engine alive: the host may drop its reference.
	const std::weak_ptr<ScriptEngine> weak = fixture.Engine;
	fixture.Engine.reset();
	CHECK_FALSE(weak.expired());
	fixture.Tick();
	CHECK(ScriptEngine::Find(*fixture.Scene) != nullptr);
	fixture.Scene.reset();
	CHECK(weak.expired());
}

TEST_CASE("Scripting: a script system cannot take the name of a system the world has")
{
	ScriptFixture fixture;
	REQUIRE(
		fixture.Scene->GetScheduler().AddSystem(CreateScope<RecordingSystem>("Mover", SystemPhase::Update)).HasValue());
	ScriptEngine& engine = fixture.Create();
	REQUIRE(engine
	            .LoadScript(
					"Scripts/Main.lua",
					"System('Other', { OnUpdate = function() end })\n"
					"System('Mover', { OnUpdate = function() end })")
	            .HasValue());
	engine.FinishLoading();

	const Result<void> attached = engine.AttachToWorld();
	REQUIRE_FALSE(attached.HasValue());
	CHECK_MESSAGE(
		Contains(
			attached.GetError().Message, "the script system 'Mover' (Scripts/Main.lua:2) has the name of a system"),
		attached.GetError().Message);
	// Nothing was added.
	CHECK(fixture.Scene->GetScheduler().GetSystemCount() == 1);
	CHECK(ScriptEngine::Find(*fixture.Scene) == nullptr);
}
