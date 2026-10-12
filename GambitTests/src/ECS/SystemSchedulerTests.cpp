#include <doctest/doctest.h>

#include "ECS/TestWorld.h"

#include "Gambit/ECS/SystemScheduler.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// Appends "<name>" to a shared journal on update, "+<name>" / "-<name>" on create / destroy.
	class RecordingSystem final : public ISystem
	{
	public:
		RecordingSystem(
			std::string name, SystemPhase phase, int32_t order, std::vector<std::string>& journal,
			bool runsInEditMode = false)
			: m_Name(std::move(name)),
			  m_Phase(phase),
			  m_Order(order),
			  m_Journal(journal),
			  m_RunsInEditMode(runsInEditMode)
		{}

		[[nodiscard]] std::string_view GetName() const override { return m_Name; }
		[[nodiscard]] SystemPhase GetPhase() const override { return m_Phase; }
		[[nodiscard]] int32_t GetOrder() const override { return m_Order; }
		[[nodiscard]] bool RunsInEditMode() const override { return m_RunsInEditMode; }

		void OnCreate(World&) override { m_Journal.push_back("+" + m_Name); }
		void OnUpdate(World& world, TimeStep timeStep) override
		{
			m_Journal.push_back(m_Name);
			LastTick = timeStep.GetTick();
			LastWorld = &world;
		}
		void OnDestroy(World&) override { m_Journal.push_back("-" + m_Name); }

		uint64_t LastTick = 0;
		World* LastWorld = nullptr;

	private:
		std::string m_Name;
		SystemPhase m_Phase;
		int32_t m_Order;
		std::vector<std::string>& m_Journal;
		bool m_RunsInEditMode;
	};

	// Records a deferred spawn and checks that it only shows up after its phase.
	class SpawningSystem final : public ISystem
	{
	public:
		[[nodiscard]] std::string_view GetName() const override { return "Spawner"; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::Update; }

		void OnUpdate(World& world, TimeStep) override
		{
			Spawned = world.GetCommandBuffer().CreateEntity("Spawned");
			ExistedDuringUpdate = world.ContainsEntity(Spawned);
		}

		UUID Spawned;
		bool ExistedDuringUpdate = true;
	};

	class ObserverSystem final : public ISystem
	{
	public:
		explicit ObserverSystem(const SpawningSystem& spawner)
			: m_Spawner(spawner)
		{}

		[[nodiscard]] std::string_view GetName() const override { return "Observer"; }
		[[nodiscard]] SystemPhase GetPhase() const override { return SystemPhase::PrePhysics; }

		void OnUpdate(World& world, TimeStep) override { SawSpawn = world.ContainsEntity(m_Spawner.Spawned); }

		bool SawSpawn = false;

	private:
		const SpawningSystem& m_Spawner;
	};
}

TEST_CASE("ECS: systems run by phase, then order, then name")
{
	std::vector<std::string> journal; // outlives the world, whose systems write to it when destroyed
	TestWorld fixture;
	World& world = *fixture.Scene;
	SystemScheduler& scheduler = world.GetScheduler();
	const auto add = [&](std::string name, SystemPhase phase, int32_t order) {
		REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>(std::move(name), phase, order, journal)).HasValue());
	};
	add("Render", SystemPhase::PreRender, -100);
	add("Late", SystemPhase::LateUpdate, 0);
	add("UpdateB", SystemPhase::Update, 0);
	add("UpdateA", SystemPhase::Update, 0);
	add("UpdateFirst", SystemPhase::Update, -1);
	add("Input", SystemPhase::Input, 50);
	add("Physics", SystemPhase::Physics, 0);
	CHECK(
		journal
		== std::vector<std::string>{ "+Render", "+Late", "+UpdateB", "+UpdateA", "+UpdateFirst", "+Input",
	                                 "+Physics" });
	CHECK(scheduler.GetSystemCount() == 7);

	journal.clear();
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(
		journal
		== std::vector<std::string>{ "Input", "UpdateFirst", "UpdateA", "UpdateB", "Physics", "Late", "Render" });

	std::vector<std::string> order;
	for (const ISystem* system : scheduler.GetSystems())
	{
		order.emplace_back(system->GetName());
	}
	CHECK(
		order == std::vector<std::string>{ "Input", "UpdateFirst", "UpdateA", "UpdateB", "Physics", "Late", "Render" });

	// Systems see the world and the tick being run.
	auto* input = static_cast<RecordingSystem*>(scheduler.FindSystem("Input"));
	REQUIRE(input != nullptr);
	CHECK(input->LastWorld == &world);
	CHECK(input->LastTick == 1);
	CHECK(scheduler.FindSystem("Missing") == nullptr);

	// Removing calls OnDestroy; the world removes the rest, in reverse order, when it goes away.
	journal.clear();
	CHECK(scheduler.RemoveSystem("Physics"));
	CHECK_FALSE(scheduler.RemoveSystem("Physics"));
	CHECK(journal == std::vector<std::string>{ "-Physics" });
	journal.clear();
	fixture.Scene.reset();
	CHECK(journal == std::vector<std::string>{ "-Render", "-Late", "-UpdateB", "-UpdateA", "-UpdateFirst", "-Input" });
}

TEST_CASE("ECS: systems can be disabled and edit mode runs only editor-safe systems")
{
	std::vector<std::string> journal; // outlives the world, whose systems write to it when destroyed
	TestWorld fixture;
	World& world = *fixture.Scene;
	SystemScheduler& scheduler = world.GetScheduler();
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("Gameplay", SystemPhase::Update, 0, journal)).HasValue());
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("Preview", SystemPhase::Animation, 0, journal, true))
	            .HasValue());
	journal.clear();

	world.Tick(TimeStep(1.0f / 60.0f), SimulationMode::Edit);
	CHECK(journal == std::vector<std::string>{ "Preview" });

	journal.clear();
	CHECK(scheduler.SetSystemEnabled("Preview", false));
	CHECK_FALSE(scheduler.IsSystemEnabled("Preview"));
	CHECK(scheduler.IsSystemEnabled("Gameplay"));
	CHECK_FALSE(scheduler.SetSystemEnabled("Missing", false));
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(journal == std::vector<std::string>{ "Gameplay" });

	journal.clear();
	CHECK(scheduler.SetSystemEnabled("Preview", true));
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(journal == std::vector<std::string>{ "Gameplay", "Preview" });

	// Statistics count the runs of each system (wall-clock times are informational).
	const SystemStats* stats = scheduler.GetStats("Preview");
	REQUIRE(stats != nullptr);
	CHECK(stats->UpdateCount == 2);
	CHECK(stats->TotalMilliseconds >= stats->LastMilliseconds);
	CHECK(stats->MaxMilliseconds >= stats->LastMilliseconds);
	CHECK(scheduler.GetStats("Gameplay")->UpdateCount == 2);
	CHECK(scheduler.GetStats("Missing") == nullptr);
	scheduler.ResetStats();
	CHECK(scheduler.GetStats("Gameplay")->UpdateCount == 0);
}

TEST_CASE("ECS: adding systems is validated")
{
	std::vector<std::string> journal; // outlives the world, whose systems write to it when destroyed
	TestWorld fixture;
	SystemScheduler& scheduler = fixture.Scene->GetScheduler();
	CHECK_FALSE(scheduler.AddSystem(nullptr).HasValue());
	CHECK_FALSE(scheduler.AddSystem(CreateScope<RecordingSystem>("", SystemPhase::Update, 0, journal)).HasValue());
	REQUIRE(scheduler.AddSystem(CreateScope<RecordingSystem>("Same", SystemPhase::Update, 0, journal)).HasValue());
	const Result<void> duplicate =
		scheduler.AddSystem(CreateScope<RecordingSystem>("Same", SystemPhase::Physics, 0, journal));
	REQUIRE_FALSE(duplicate.HasValue());
	CHECK(duplicate.GetError().Message.find("Same") != std::string::npos);
	CHECK(scheduler.GetSystemCount() == 1);
	CHECK(journal == std::vector<std::string>{ "+Same" }); // rejected systems are never created
}

TEST_CASE("ECS: the command buffer is flushed after every phase")
{
	TestWorld fixture;
	World& world = *fixture.Scene;
	auto spawner = CreateScope<SpawningSystem>();
	auto observer = CreateScope<ObserverSystem>(*spawner);
	const SpawningSystem& spawnerRef = *spawner;
	const ObserverSystem& observerRef = *observer;
	REQUIRE(world.GetScheduler().AddSystem(std::move(observer)).HasValue());
	REQUIRE(world.GetScheduler().AddSystem(std::move(spawner)).HasValue());

	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK_FALSE(spawnerRef.ExistedDuringUpdate); // deferred...
	CHECK(observerRef.SawSpawn);                 // ...and applied before the next phase
	CHECK(world.ContainsEntity(spawnerRef.Spawned));
	CHECK(world.GetCommandBuffer().IsEmpty());

	// Commands recorded between ticks are applied at the start of the next one.
	const UUID between = world.GetCommandBuffer().CreateEntity("Between");
	CHECK_FALSE(world.ContainsEntity(between));
	world.Tick(TimeStep(1.0f / 60.0f));
	CHECK(world.ContainsEntity(between));
}

TEST_CASE("ECS: system phases have names")
{
	CHECK(ToString(SystemPhase::Input) == "Input");
	CHECK(ToString(SystemPhase::PreRender) == "PreRender");
	CHECK(ToString(SystemPhase::Count) == "Unknown");
	for (size_t index = 0; index < SystemPhaseCount; ++index)
	{
		const auto phase = static_cast<SystemPhase>(index);
		CHECK(ParseSystemPhase(ToString(phase)) == phase);
	}
	CHECK_FALSE(ParseSystemPhase("update").has_value());
}
