#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include "Gambit/Automation/CommandBatch.h"

#include <fstream>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::string MainScene(AutomationFixture& fixture)
	{
		return ReadTextFile(fixture.GetProjectDirectory() / "Scenes/Main.gscene");
	}
}

TEST_CASE("Automation: a batch runs its lines in one session and saves at the end")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string before = MainScene(fixture);

	std::vector<BatchStep> steps;
	const BatchSummary summary = RunCommandBatch(
		fixture.Commands, fixture.Context,
		"{\"command\": \"entity.create\", \"args\": {\"name\": \"Rig\"}}\n"
		"\n"
		"  {\"command\": \"selection.set\", \"args\": {\"entities\": [\"Rig\"]}}  \r\n"
		"{\"command\": \"selection.get\"}",
		BatchOptions{}, [&](const BatchStep& step) { steps.push_back(step); });
	CHECK(summary.Executed == 3);
	CHECK(summary.Failed == 0);
	CHECK(summary.Saving == BatchSaving::Saved);
	CHECK(summary.Saved == std::vector<std::string>{ "Scenes/Main.gscene" });
	REQUIRE(steps.size() == 3);
	CHECK(steps[0].Line == 1);
	CHECK(steps[0].Command == "entity.create");
	CHECK(steps[1].Line == 3);                    // blank lines count, but do not run
	CHECK(steps[2].Result.Data.at("count") == 1); // the selection of line 3 is still there
	CHECK(MainScene(fixture) != before);
	CHECK(MainScene(fixture).find("Rig") != std::string::npos);
}

TEST_CASE("Automation: a batch stops at the first failure and then saves nothing")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string before = MainScene(fixture);
	const std::string text = "{\"command\": \"entity.create\", \"args\": {\"name\": \"Lost\"}}\n"
							 "not json\n"
							 "{\"command\": \"entity.create\", \"args\": {\"name\": \"Never\"}}\n";

	std::vector<BatchStep> steps;
	const BatchSummary stopped = RunCommandBatch(
		fixture.Commands, fixture.Context, text, BatchOptions{}, [&](const BatchStep& step) { steps.push_back(step); });
	CHECK(stopped.Executed == 2);
	CHECK(stopped.Failed == 1);
	CHECK(stopped.Saving == BatchSaving::Stopped);
	CHECK(stopped.Saved.empty());
	REQUIRE(steps.size() == 2);
	CHECK(steps[1].Command.empty());
	CHECK(steps[1].Result.Code == CommandErrorCode::InvalidArguments);
	CHECK(steps[1].Result.Message.starts_with("line 2: "));
	CHECK(MainScene(fixture) == before);
	CHECK_FALSE(fixture.Scene().FindEntityByName("Never").IsValid());

	// Requests of the wrong shape are refused line by line; with KeepGoing the rest runs and is saved.
	steps.clear();
	const BatchSummary kept = RunCommandBatch(
		fixture.Commands, fixture.Context, text + "[1, 2]\n{\"args\": {}}\n{\"command\": \"entity.nope\"}\n",
		BatchOptions{ .KeepGoing = true }, [&](const BatchStep& step) { steps.push_back(step); });
	CHECK(kept.Executed == 6);
	CHECK(kept.Failed == 4);
	CHECK(kept.Saving == BatchSaving::Saved);
	CHECK(steps[3].Result.Message == "line 4: expected {\"command\": \"<name>\", \"args\": {...}}");
	CHECK(steps[5].Command == "entity.nope");
	CHECK(steps[5].Result.Code == CommandErrorCode::UnknownCommand);
	CHECK(MainScene(fixture).find("Never") != std::string::npos);
}

TEST_CASE("Automation: a batch saves nothing in a dry run and reports a failed save")
{
	const ScopedLogCapture log; // the load of the scene with an unknown component warns
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Context.SetDryRun(true);
	const BatchSummary dry = RunCommandBatch(
		fixture.Commands, fixture.Context, R"({"command": "entity.create", "args": {"name": "Dry"}})", BatchOptions{});
	CHECK(dry.Saving == BatchSaving::DryRun);
	CHECK(MainScene(fixture).find("Dry") == std::string::npos);
	fixture.Context.SetDryRun(false);

	// A scene whose load dropped data refuses the save at the end.
	std::ofstream(fixture.GetProjectDirectory() / "Scenes/Odd.gscene")
		<< R"({"Version": 1, "Entities": [{"UUID": "0x1", "Components": {"Ghost": {}}}]})";
	const BatchSummary refused = RunCommandBatch(
		fixture.Commands, fixture.Context,
		"{\"command\": \"scene.open\", \"args\": {\"path\": \"Scenes/Odd.gscene\"}}\n"
		"{\"command\": \"entity.create\", \"args\": {\"name\": \"New\"}}\n",
		BatchOptions{});
	CHECK(refused.Failed == 0);
	CHECK(refused.Saving == BatchSaving::Failed);
	REQUIRE(refused.SaveError.has_value());
	CHECK(GetCommandErrorCode(*refused.SaveError) == CommandErrorCode::Conflict);
}
