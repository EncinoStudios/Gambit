#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include <set>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::vector<std::string> SelectedNames(AutomationFixture& fixture)
	{
		std::vector<std::string> names;
		const Json selection = fixture.Succeed("selection.get");
		for (const Json& entity : selection.at("entities"))
		{
			names.push_back(entity.at("name").get<std::string>());
		}
		return names;
	}
}

TEST_CASE("Automation: selection.set selects entities in order")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("A");
	fixture.Create("B");
	const std::string c = fixture.Create("C");
	CHECK(SelectedNames(fixture).empty());

	const Json set = fixture.Succeed("selection.set", J(R"({"entities": ["C", "A"]})"));
	CHECK(set.at("count") == 2);
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "C", "A" });
	CHECK(fixture.Succeed("selection.get").at("entities")[0].at("uuid") == c);

	fixture.Succeed("selection.set", J(R"({"entities": ["B", "C"], "mode": "add"})"));
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "C", "A", "B" }); // C is not selected twice
	fixture.Succeed("selection.set", J(R"({"entities": ["A"], "mode": "remove"})"));
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "C", "B" });
	fixture.Succeed("selection.set", J(R"({"entities": []})"));
	CHECK(SelectedNames(fixture).empty());
	fixture.Succeed("selection.set", J(R"({"entities": ["A"]})"));
	fixture.Succeed("selection.set");
	CHECK(SelectedNames(fixture).empty());

	fixture.Succeed("selection.set", J(R"({"entities": ["A"]})"));
	fixture.Reject("selection.set", J(R"({"entities": ["Nobody"]})"), CommandErrorCode::NotFound);
	fixture.Reject("selection.set", J(R"({"entities": [3]})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("selection.set", J(R"({"mode": "toggle"})"), CommandErrorCode::InvalidArguments);
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "A" }); // a failed command changes nothing

	// The selection belongs to the active scene.
	fixture.Succeed("scene.new", J(R"({"path": "Scenes/Other.gscene"})"));
	CHECK(SelectedNames(fixture).empty());
}

TEST_CASE("Automation: undo and redo step through the changes")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Reject("undo", Json::object(), CommandErrorCode::Conflict);
	fixture.Reject("redo", Json::object(), CommandErrorCode::Conflict);

	const uint64_t empty = fixture.Hash();
	fixture.Create("One");
	fixture.Create("Two");
	fixture.Create("Three");
	fixture.Succeed("entity.rename", J(R"({"entity": "Three", "name": "Tres"})"));

	const Json undone = fixture.Succeed("undo");
	CHECK(undone.at("steps") == 1);
	CHECK(undone.at("undone") == J(R"(["Rename 'Three' to 'Tres'"])"));
	CHECK(undone.at("canUndo") == true);
	CHECK(undone.at("canRedo") == true);
	CHECK(undone.at("nextUndo") == "Create entity 'Three'");
	CHECK(undone.at("nextRedo") == "Rename 'Three' to 'Tres'");
	CHECK(undone.at("dirty") == J(R"(["Scenes/Main.gscene"])"));
	CHECK(fixture.Scene().FindEntityByName("Three").IsValid());

	const Json more = fixture.Succeed("undo", J(R"({"steps": 10})"));
	CHECK(more.at("steps") == 3); // only as many as there were
	CHECK(more.at("canUndo") == false);
	CHECK(fixture.Hash() == empty);
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene")); // back at the saved state

	const Json redone = fixture.Succeed("redo", J(R"({"steps": 2})"));
	CHECK(redone.at("redone") == J(R"(["Create entity 'One'", "Create entity 'Two'"])"));
	CHECK(fixture.Scene().GetEntityCount() == 2);
	CHECK(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
	fixture.Succeed("redo", J(R"({"steps": 5})"));
	CHECK(fixture.Scene().FindEntityByName("Tres").IsValid());
	fixture.Reject("redo", Json::object(), CommandErrorCode::Conflict);

	fixture.Reject("undo", J(R"({"steps": 0})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("undo", J(R"({"count": 1})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: doing something new after undo discards what could be redone")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("A");
	fixture.Create("B");
	fixture.Succeed("undo");
	fixture.Create("C");
	fixture.Reject("redo", Json::object(), CommandErrorCode::Conflict);
	CHECK(
		fixture.Context.GetUndoStack().GetUndoHistory()
		== std::vector<std::string>{ "Create entity 'C'", "Create entity 'A'" });
}

TEST_CASE("Automation: the selection follows undo and redo")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Keep");
	fixture.Create("Fresh");
	fixture.Succeed("selection.set", J(R"({"entities": ["Keep", "Fresh"]})"));
	fixture.Succeed("undo"); // "Fresh" does not exist any more
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "Keep" });
	fixture.Succeed("redo");
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "Keep" }); // a redone entity is not selected again
}

TEST_CASE("Automation: a failed undo step still takes removed entities out of the selection")
{
	const ScopedLogCapture log;
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Old");
	fixture.Create("Fresh");
	fixture.Succeed("selection.set", J(R"({"entities": ["Fresh"]})"));
	// The scene changes behind the history's back, so undoing the creation of "Old" fails after "Fresh" is gone.
	fixture.Scene().DestroyEntity(fixture.Scene().FindEntityByName("Old"));

	const CommandResult failed = fixture.Reject("undo", J(R"({"steps": 2})"), CommandErrorCode::Conflict);
	CHECK(failed.Message.find("the undo history was cleared") != std::string::npos);
	CHECK(log.Contains(LogLevel::Error, "undo 'Create entity 'Old'' failed"));
	CHECK_FALSE(fixture.Scene().FindEntityByName("Fresh").IsValid());
	CHECK(fixture.Context.GetSelection().empty());
	CHECK(fixture.Succeed("selection.get").at("count") == 0);
	fixture.Create("Next");
	fixture.Succeed("selection.set", J(R"({"entities": ["Next"], "mode": "add"})"));
	CHECK(SelectedNames(fixture) == std::vector<std::string>{ "Next" });
}

TEST_CASE("Automation: selection.get names no entity for a selected UUID that is gone")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string gone = fixture.Create("Gone");
	fixture.Succeed("selection.set", J(R"({"entities": ["Gone"]})"));
	// Only a host that removes entities without the commands can leave such an entry.
	fixture.Scene().DestroyEntity(fixture.Scene().FindEntityByName("Gone"));
	const Json selection = fixture.Succeed("selection.get");
	REQUIRE(selection.at("count") == 1);
	CHECK(selection.at("entities")[0].at("uuid") == gone);
	CHECK(selection.at("entities")[0].at("name").is_null());
}

TEST_CASE("Automation: every undoable command has an undo test")
{
	AutomationFixture fixture;
	// The commands that are tested for undo in this suite; a new undoable command must be added here and get a test.
	const std::set<std::string> tested = { "component.add",    "component.remove",  "component.set",   "data.add-row",
		                                   "data.remove-row",  "data.set",          "entity.create",   "entity.destroy",
		                                   "entity.duplicate", "entity.rename",     "entity.reparent", "bulk.set",
		                                   "bulk.import-csv",  "prefab.instantiate" };
	std::set<std::string> undoable;
	std::set<std::string> mutating;
	for (const CommandInfo* info : fixture.Commands.List())
	{
		if (info->HasFlag(CommandFlags::Undoable))
		{
			undoable.insert(info->Name);
			CHECK(info->HasFlag(CommandFlags::Mutating));
		}
		else if (info->HasFlag(CommandFlags::Mutating))
		{
			mutating.insert(info->Name);
		}
	}
	CHECK(undoable == tested);
	// Everything else that changes something writes a file, changes project data outside the documents (comments)
	// or the session itself: those changes have no undo (D-054).
	CHECK(
		mutating
		== std::set<std::string>{ "asset.import", "bulk.write-csv", "comment.add", "comment.delete", "comment.reopen",
	                              "comment.reply", "comment.resolve", "prefab.create", "project.new", "redo",
	                              "scene.close", "scene.new", "scene.save", "undo" });
}
