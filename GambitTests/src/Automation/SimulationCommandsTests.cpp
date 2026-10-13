#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include "Gambit/ECS/Systems/TransformSystem.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// Randomizes health every tick: the history of a run depends on the state of the world's random generator, so
	// it shows whether a simulation started the way D-020 says.
	class JitterSystem final : public ISystem
	{
	public:
		std::string_view GetName() const override { return "JitterSystem"; }
		SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnUpdate(World& world, TimeStep) override
		{
			for (auto [entity, health] : world.View<HealthComponent>().each())
			{
				health.Current = world.GetRandom().NextFloat01() * 100.0f;
			}
		}
	};

	Result<void> AddJitter(World& world)
	{
		return world.GetScheduler().AddSystem(CreateScope<JitterSystem>());
	}

	// A project with three units that jitter once a simulation runs.
	struct Arena
	{
		AutomationFixture Fixture;

		Arena()
		{
			Fixture.Context.SetSimulationSetup(AddJitter);
			Fixture.CreateProject();
			for (const char* name : { "A", "B", "C" })
			{
				Fixture.Create(name, J(R"({"components": {"Health": {"Current": 10}, "Transform": {}}})"));
			}
		}

		std::vector<std::string> RunHashes(uint32_t ticks)
		{
			const Json result = Fixture.Succeed("sim.run", Json{ { "ticks", ticks }, { "hashes", true } });
			return result.at("hashes").get<std::vector<std::string>>();
		}
	};
}

TEST_CASE("Automation: sim.run simulates a copy of the scene and reports the state hashes")
{
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;
	const uint64_t before = fixture.Hash();

	const Json result = fixture.Succeed("sim.run", J(R"({"ticks": 30, "hashes": true})"));
	CHECK(result.at("ticks") == 30);
	CHECK(result.at("tick") == 30);
	CHECK(result.at("simulationHz") == 60);
	CHECK(result.at("time").get<double>() == doctest::Approx(0.5));
	CHECK(result.at("entityCount") == 3);
	CHECK(result.at("seed") == fixture.Context.GetActiveScene()->Scene->GetSeed());
	REQUIRE(result.at("hashes").size() == 30);
	CHECK(result.at("hashes")[29] == result.at("stateHash"));
	CHECK(result.at("hashes")[0] != result.at("initialHash"));
	CHECK(fixture.Hash() == before); // the scene itself does not change

	// The same scene and seed give the same history, a different seed another one.
	const Json again = fixture.Succeed("sim.run", J(R"({"ticks": 30, "hashes": true})"));
	CHECK(again.at("hashes") == result.at("hashes"));
	CHECK(again.at("initialHash") == result.at("initialHash"));
	const Json reseeded = fixture.Succeed("sim.run", J(R"({"ticks": 30, "seed": 4242})"));
	CHECK(reseeded.at("seed") == 4242);
	CHECK(reseeded.at("stateHash") != result.at("stateHash"));
	CHECK_FALSE(reseeded.contains("hashes"));

	// No ticks: the initial state.
	const Json none = fixture.Succeed("sim.run", J(R"({"ticks": 0})"));
	CHECK(none.at("stateHash") == none.at("initialHash"));
	CHECK(none.at("tick") == 0);
}

TEST_CASE("Automation: sim.run of an edited scene gives the hashes of the scene loaded from its file")
{
	// D-020: entities created while editing consumed random numbers from the world, a file stores only the seed.
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;
	const std::vector<std::string> played = arena.RunHashes(60);
	fixture.Succeed("scene.save");

	World loaded(fixture.Types);
	LoadReport report;
	REQUIRE(
		SceneSerializer::Load(loaded, fixture.Context.GetProject()->GetVFS(), "Scenes/Main.gscene", report).HasValue());
	REQUIRE(loaded.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
	REQUIRE(AddJitter(loaded).HasValue());
	loaded.BeginSimulation(loaded.GetSeed());
	std::vector<std::string> fromFile;
	for (int tick = 0; tick < 60; ++tick)
	{
		loaded.Tick(TimeStep(1.0f / 60.0f));
		fromFile.push_back(std::format("0x{:016X}", loaded.ComputeStateHash()));
	}
	CHECK(played == fromFile);
	CHECK(played.front() != played.back());
}

TEST_CASE("Automation: sim.run dumps components and honours the simulation rate and setup")
{
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;

	const Json dumped = fixture.Succeed("sim.run", J(R"({"ticks": 5, "components": ["Health", "PlayerTag"]})"));
	REQUIRE(dumped.at("dump").size() == 3);
	CHECK(dumped.at("dump")[0].at("name") == "A");
	CHECK(dumped.at("dump")[0].at("components").at("Health").at("Current") != 10.0); // jittered
	CHECK_FALSE(dumped.at("dump")[0].at("components").contains("PlayerTag"));
	CHECK_FALSE(fixture.Succeed("sim.run", J(R"({"ticks": 1})")).contains("dump"));

	ProjectSettings settings = fixture.Context.GetProject()->GetSettings();
	settings.SimulationHz = 30;
	REQUIRE(fixture.Context.GetProject()->SetSettings(settings).HasValue());
	const Json slow = fixture.Succeed("sim.run", J(R"({"ticks": 30})"));
	CHECK(slow.at("simulationHz") == 30);
	CHECK(slow.at("time").get<double>() == doctest::Approx(1.0));

	// A host whose simulation cannot be set up reports why.
	fixture.Context.SetSimulationSetup([](World&) -> Result<void> { return Error("no physics here"); });
	const CommandResult failed = fixture.Reject("sim.run", J(R"({"ticks": 1})"), CommandErrorCode::Failed);
	CHECK(failed.Message.find("no physics here") != std::string::npos);
}

TEST_CASE("Automation: sim.run checks its arguments and needs a scene")
{
	AutomationFixture fixture;
	fixture.Reject("sim.run", J(R"({"ticks": 1})"), CommandErrorCode::NoProject);
	fixture.CreateProject();
	fixture.Create("A");
	fixture.Reject("sim.run", J(R"({})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("sim.run", J(R"({"ticks": -1})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("sim.run", J(R"({"ticks": 1000001})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("sim.run", J(R"({"ticks": 1, "seed": -3})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("sim.run", J(R"({"ticks": 1, "components": ["Nope"]})"), CommandErrorCode::NotFound);

	fixture.Succeed("scene.close", J(R"({"discard": true})"));
	fixture.Reject("sim.run", J(R"({"ticks": 1})"), CommandErrorCode::NoScene);
}

TEST_CASE("Automation: log.read returns the recent messages of the host")
{
	const ScopedLogCapture capture;
	AutomationFixture fixture;
	GB_CORE_INFO("automation test: first message");
	GB_CORE_WARN("automation test: a Warning");
	GB_CORE_ERROR("automation test: broken thing");

	const Json all = fixture.Succeed("log.read");
	REQUIRE(all.at("count").get<size_t>() >= 3);
	const Json& last = all.at("entries").back();
	CHECK(last.at("level") == "error");
	CHECK(last.at("message") == "automation test: broken thing");
	CHECK(last.at("time").get<std::string>().size() == 20); // 2026-03-14T09:26:53Z
	CHECK(all.at("lastSequence") == last.at("sequence"));
	CHECK(all.at("totalWritten").get<uint64_t>() >= 3);

	const Json errors = fixture.Succeed("log.read", J(R"({"level": "error"})"));
	REQUIRE(errors.at("count") == 1);
	CHECK(errors.at("entries")[0].at("message") == "automation test: broken thing");
	const Json warnings = fixture.Succeed("log.read", J(R"({"level": "warn", "contains": "WARNING"})"));
	REQUIRE(warnings.at("count") == 1);
	CHECK(warnings.at("entries")[0].at("level") == "warn");
	CHECK(fixture.Succeed("log.read", J(R"({"contains": "no such text"})")).at("count") == 0);

	const Json newest = fixture.Succeed("log.read", J(R"({"count": 1})"));
	REQUIRE(newest.at("count") == 1);
	CHECK(newest.at("entries")[0].at("message") == "automation test: broken thing");

	// Reading on from where the last read ended.
	const uint64_t cursor = all.at("lastSequence");
	GB_CORE_WARN("automation test: later");
	const Json later = fixture.Succeed("log.read", Json{ { "after", cursor } });
	REQUIRE(later.at("count") == 1);
	CHECK(later.at("entries")[0].at("message") == "automation test: later");
	const Json nothing = fixture.Succeed("log.read", Json{ { "after", later.at("lastSequence") } });
	CHECK(nothing.at("count") == 0);
	CHECK(nothing.at("lastSequence") == later.at("lastSequence"));

	fixture.Reject("log.read", J(R"({"level": "loud"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("log.read", J(R"({"count": 0})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: log.read says when the host keeps no log history")
{
	LogConfig config = MakeTestLogConfig();
	config.RingBufferCapacity = 0;
	const ScopedLogInit init(config);
	AutomationFixture fixture;
	fixture.Reject("log.read", Json::object(), CommandErrorCode::Unsupported);

	// A host can hand the context a buffer of its own.
	const Ref<RingBufferSink> buffer = CreateRef<RingBufferSink>(8);
	fixture.Context.SetLogBuffer(buffer);
	const LogRecord record{ LogLevel::Warn, "HOST", "from the host", std::chrono::system_clock::now() };
	buffer->Write(record);
	const Json read = fixture.Succeed("log.read");
	REQUIRE(read.at("count") == 1);
	CHECK(read.at("entries")[0].at("logger") == "HOST");
}
