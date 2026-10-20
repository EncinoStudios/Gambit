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

	// Draws from the random generator and queues a spawn when it is created, as script and physics systems may:
	// both are part of the run only if the simulation was started before the systems were added (D-020).
	class StartupSystem final : public ISystem
	{
	public:
		std::string_view GetName() const override { return "StartupSystem"; }
		SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnCreate(World& world) override
		{
			static_cast<void>(world.GetRandom().NextU32());
			static_cast<void>(world.GetCommandBuffer().CreateEntity("Spawned"));
		}

		void OnUpdate(World&, TimeStep) override {}
	};

	Result<void> AddJitterAndStartup(World& world)
	{
		if (Result<void> added = AddJitter(world); !added)
		{
			return added;
		}
		return world.GetScheduler().AddSystem(CreateScope<StartupSystem>());
	}

	std::string Hex(uint64_t hash)
	{
		return std::format("0x{:016X}", hash);
	}

	std::vector<std::string> Tick(World& world, int ticks)
	{
		std::vector<std::string> hashes;
		for (int tick = 0; tick < ticks; ++tick)
		{
			world.Tick(TimeStep(1.0f / 60.0f));
			hashes.push_back(Hex(world.ComputeStateHash()));
		}
		return hashes;
	}

	// The hashes of the saved scene loaded from its file and run the way the runtime does: load (which starts the
	// world like BeginSimulation), add the systems, tick.
	std::vector<std::string> RunFromFile(AutomationFixture& fixture, const EditorContext::WorldSetup& setup, int ticks)
	{
		World loaded(fixture.Types);
		LoadReport report;
		REQUIRE(SceneSerializer::Load(loaded, fixture.Context.GetProject()->GetVFS(), "Scenes/Main.gscene", report)
		            .HasValue());
		loaded.BeginSimulation(loaded.GetSeed());
		REQUIRE(loaded.GetScheduler().AddSystem(CreateScope<TransformSystem>()).HasValue());
		REQUIRE(setup(loaded).HasValue());
		return Tick(loaded, ticks);
	}

	// What an editing session leaves in the edited world that a scene file does not store: a used random
	// generator and a pending command.
	void DisturbLikeAnEditor(World& world)
	{
		for (int draw = 0; draw < 5; ++draw)
		{
			static_cast<void>(world.GetRandom().NextU32());
		}
		static_cast<void>(world.GetCommandBuffer().CreateEntity("Pending"));
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
	// D-020: a file stores only the seed, not what editing did to the world's random generator or command buffer.
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;
	fixture.Succeed("scene.save");
	const std::vector<std::string> fromFile = RunFromFile(fixture, AddJitter, 60);
	CHECK(fromFile.front() != fromFile.back());

	DisturbLikeAnEditor(fixture.Scene());
	CHECK(arena.RunHashes(60) == fromFile);

	// The entry point of editor play is the same.
	Result<Scope<World>> played = fixture.Context.CreateSimulationWorld(fixture.Scene());
	REQUIRE(played.HasValue());
	CHECK(Tick(**played, 60) == fromFile);
	CHECK((*played)->FindEntityByName("Pending").IsValid() == false);
}

TEST_CASE("Automation: sim.run and editor play start the simulation before the systems are created")
{
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;
	fixture.Context.SetSimulationSetup(AddJitterAndStartup);
	fixture.Succeed("scene.save");
	const std::vector<std::string> fromFile = RunFromFile(fixture, AddJitterAndStartup, 30);

	const Json run = fixture.Succeed("sim.run", J(R"({"ticks": 30, "hashes": true})"));
	CHECK(run.at("hashes").get<std::vector<std::string>>() == fromFile);
	CHECK(run.at("entityCount") == 4); // what StartupSystem queued in OnCreate is part of the run

	Result<Scope<World>> played = fixture.Context.CreateSimulationWorld(fixture.Scene());
	REQUIRE(played.HasValue());
	CHECK(Tick(**played, 30) == fromFile);

	// A seed of its own for sim.run starts the same way, from that seed.
	Result<Scope<World>> reseeded = fixture.Context.CreateSimulationWorld(fixture.Scene(), 4242);
	REQUIRE(reseeded.HasValue());
	CHECK((*reseeded)->GetSeed() == 4242);
	const std::vector<std::string> reseededHashes = Tick(**reseeded, 30);
	const Json runReseeded = fixture.Succeed("sim.run", J(R"({"ticks": 30, "hashes": true, "seed": 4242})"));
	CHECK(runReseeded.at("hashes").get<std::vector<std::string>>() == reseededHashes);
	CHECK(reseededHashes != fromFile);
}

TEST_CASE("Automation: seeds use the whole 64-bit range")
{
	Arena arena;
	AutomationFixture& fixture = arena.Fixture;
	constexpr uint64_t Large = (uint64_t(1) << 63) + 1;
	const Json run = fixture.Succeed("sim.run", Json{ { "ticks", 3 }, { "seed", Large } });
	CHECK(run.at("seed").get<uint64_t>() == Large);
	// A seed a result reported can be given back.
	CHECK(
		fixture.Succeed("sim.run", Json{ { "ticks", 3 }, { "seed", run.at("seed") } }).at("stateHash")
		== run.at("stateHash"));
	const Json biggest = fixture.Succeed("sim.run", Json{ { "ticks", 1 }, { "seed", UINT64_MAX } });
	CHECK(biggest.at("seed").get<uint64_t>() == UINT64_MAX);

	const Json created = fixture.Succeed("scene.new", Json{ { "path", "Scenes/Large.gscene" }, { "seed", Large } });
	CHECK(created.at("seed").get<uint64_t>() == Large);
	CHECK(fixture.Scene().GetSeed() == Large);

	// Beyond the range the message says so.
	const CommandResult tooLarge = fixture.Reject(
		"sim.run", J(R"({"ticks": 1, "seed": 18446744073709551616})"), CommandErrorCode::InvalidArguments);
	CHECK(tooLarge.Message.find("out of range") != std::string::npos);
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
