#include <doctest/doctest.h>

#include "AutomationFixture.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/Serialization/SceneSerializer.h"

#include <fstream>
#include <map>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::string ReadFile(const std::filesystem::path& path)
	{
		return ReadTextFile(path);
	}

	std::string ProjectFile(AutomationFixture& fixture, std::string_view relative)
	{
		return ReadFile(fixture.GetProjectDirectory() / std::string(relative));
	}
}

TEST_CASE("Automation: project.new creates a project and opens its start scene")
{
	AutomationFixture fixture;
	const Json created = fixture.Succeed(
		"project.new",
		J(R"({"directory": ")" + fixture.GetProjectDirectory().generic_string() + R"(", "name": "Demo Game"})"));
	CHECK(created.at("name") == "Demo Game");
	CHECK(created.at("startScene") == "Scenes/Main.gscene");
	CHECK(std::filesystem::exists(fixture.GetProjectDirectory() / "Demo Game.gproject"));
	CHECK(std::filesystem::exists(fixture.GetProjectDirectory() / "Scenes" / "Main.gscene"));
	CHECK(std::filesystem::exists(fixture.GetProjectDirectory() / "InputActions.json"));
	for (const char* folder : { "Assets", "Scripts", "Data", "Scenes", "Comments" })
	{
		CHECK(std::filesystem::is_directory(fixture.GetProjectDirectory() / folder));
	}

	REQUIRE(fixture.Context.GetProject() != nullptr);
	REQUIRE(fixture.Context.GetActiveScene() != nullptr);
	CHECK(fixture.Context.GetActiveScene()->Path == "Scenes/Main.gscene");
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
	CHECK(fixture.Succeed("entity.list").at("total") == 0); // entity commands work right away
}

TEST_CASE("Automation: project.new refuses bad names, existing projects and unsaved work")
{
	AutomationFixture fixture;
	const std::string directory = fixture.GetProjectDirectory().generic_string();
	fixture.Reject(
		"project.new", J(R"({"directory": ")" + directory + R"(", "name": "bad/name"})"), CommandErrorCode::Failed);
	fixture.Reject("project.new", J(R"({"directory": ")" + directory + R"("})"), CommandErrorCode::InvalidArguments);
	fixture.CreateProject();

	// A second project in the same directory.
	const CommandResult exists = fixture.Reject(
		"project.new", J(R"({"directory": ")" + directory + R"(", "name": "Other"})"), CommandErrorCode::Failed);
	CHECK(exists.Message.find("already contains the project file") != std::string::npos);

	// Unsaved changes in the open project are not thrown away.
	fixture.Create("Unsaved");
	const CommandResult dirty = fixture.Reject(
		"project.new",
		J(R"({"directory": ")" + (fixture.Directory.GetPath() / "Second").generic_string() + R"(", "name": "Second"})"),
		CommandErrorCode::Conflict);
	CHECK(dirty.Message.find("unsaved changes (Scenes/Main.gscene)") != std::string::npos);
	fixture.Succeed("scene.save");
	fixture.Succeed(
		"project.new",
		J(R"({"directory": ")" + (fixture.Directory.GetPath() / "Second").generic_string()
	      + R"(", "name": "Second"})"));
	CHECK(fixture.Context.GetProject()->GetSettings().Name == "Second");
	CHECK(fixture.Succeed("entity.list").at("total") == 0);
}

TEST_CASE("Automation: project.info reports the project and its state")
{
	AutomationFixture fixture;
	fixture.Reject("project.info", Json::object(), CommandErrorCode::NoProject);
	fixture.CreateProject();
	fixture.Create("Crate");

	const Json info = fixture.Succeed("project.info");
	CHECK(info.at("name") == "Test");
	CHECK(info.at("startScene") == "Scenes/Main.gscene");
	CHECK(info.at("settings").at("SimulationHz") == 60);
	CHECK(info.at("settings").at("Directories").at("Scripts") == "Scripts");
	CHECK(info.at("openScenes") == J(R"(["Scenes/Main.gscene"])"));
	CHECK(info.at("activeScene") == "Scenes/Main.gscene");
	CHECK(info.at("dirty") == J(R"(["Scenes/Main.gscene"])"));
	CHECK(info.at("assetCount") == 1); // the start scene
	CHECK(info.at("commentCount") == 0);
	CHECK(info.at("directory").get<std::string>().ends_with("Project"));
}

TEST_CASE("Automation: scenes are created, saved, closed and opened again")
{
	AutomationFixture fixture;
	fixture.CreateProject();

	const Json created =
		fixture.Succeed("scene.new", J(R"({"path": "Scenes/Level1.gscene", "name": "First level", "seed": 77})"));
	CHECK(created.at("path") == "Scenes/Level1.gscene");
	CHECK(created.at("name") == "First level");
	CHECK(created.at("seed") == 77);
	CHECK(created.at("active") == true);
	CHECK(created.at("dirty") == true);
	CHECK_FALSE(std::filesystem::exists(fixture.GetProjectDirectory() / "Scenes/Level1.gscene")); // memory only
	CHECK(fixture.Context.GetActiveScene()->Path == "Scenes/Level1.gscene");

	fixture.Create("Hero", J(R"({"components": {"Health": {"Current": 3}}})"));
	fixture.Create("Sidekick", J(R"({"parent": "Hero"})"));
	const uint64_t hash = fixture.Hash();

	const Json saved = fixture.Succeed("scene.save");
	CHECK(saved.at("saved") == J(R"(["Scenes/Level1.gscene"])"));
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Level1.gscene"));
	const std::string text = ProjectFile(fixture, "Scenes/Level1.gscene");
	CHECK(text.find("\"Name\": \"First level\"") != std::string::npos);
	CHECK(text.find("\"Seed\": 77") != std::string::npos);
	REQUIRE(
		fixture.Context.GetProject()->GetAssets().FindByPath("Scenes/Level1.gscene")
		!= nullptr); // registered with a .meta file
	CHECK(std::filesystem::exists(fixture.GetProjectDirectory() / "Scenes/Level1.gscene.meta"));

	// Close (it is saved) and open again: the same scene, the same state hash.
	const Json closed = fixture.Succeed("scene.close");
	CHECK(closed.at("closed") == "Scenes/Level1.gscene");
	CHECK(closed.at("activeScene") == "Scenes/Main.gscene");
	const Json opened = fixture.Succeed("scene.open", J(R"({"path": "Scenes/Level1.gscene"})"));
	CHECK(opened.at("entityCount") == 2);
	CHECK(opened.at("seed") == 77);
	CHECK(opened.at("alreadyOpen") == false);
	CHECK(opened.at("dirty") == false);
	CHECK(fixture.Hash() == hash);
	CHECK(fixture.Succeed("scene.open", J(R"({"path": "Scenes/Level1.gscene"})")).at("alreadyOpen") == true);
	CHECK(fixture.Context.GetScenes().size() == 2);
}

TEST_CASE("Automation: scene.save, scene.close and scene.set-active work on named scenes")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Succeed("scene.new", J(R"({"path": "Scenes/B.gscene", "activate": false})"));
	CHECK(fixture.Context.GetActiveScene()->Path == "Scenes/Main.gscene");
	fixture.Create("InMain");
	fixture.Succeed("scene.set-active", J(R"({"path": "Scenes/B.gscene"})"));
	CHECK(fixture.Context.GetActiveScene()->Path == "Scenes/B.gscene");
	fixture.Create("InB");

	// Save one by name; the other stays dirty. "all" saves every dirty document.
	fixture.Succeed("scene.save", J(R"({"path": "Scenes/Main.gscene"})"));
	CHECK_FALSE(fixture.Context.GetUndoStack().IsDirty("Scenes/Main.gscene"));
	CHECK(fixture.Context.GetUndoStack().IsDirty("Scenes/B.gscene"));
	CHECK(fixture.Succeed("scene.save", J(R"({"all": true})")).at("saved") == J(R"(["Scenes/B.gscene"])"));
	CHECK(fixture.Succeed("scene.save", J(R"({"all": true})")).at("saved").empty());

	// A scene with unsaved changes is not closed by accident.
	fixture.Create("MoreInB");
	const CommandResult refused = fixture.Reject("scene.close", Json::object(), CommandErrorCode::Conflict);
	CHECK(refused.Message == "the scene 'Scenes/B.gscene' has unsaved changes: save it first or discard them");
	CHECK(fixture.Context.FindScene("Scenes/B.gscene") != nullptr);
	fixture.Succeed("scene.close", J(R"({"discard": true})"));
	CHECK(fixture.Context.FindScene("Scenes/B.gscene") == nullptr);
	CHECK(fixture.Context.GetActiveScene()->Path == "Scenes/Main.gscene");
	CHECK(
		fixture.Context.GetUndoStack().GetUndoHistory()
		== std::vector<std::string>{ "Create entity 'InMain'" }); // the history of the closed scene is gone
	fixture.Succeed("scene.open", J(R"({"path": "Scenes/B.gscene"})"));
	CHECK(fixture.Succeed("entity.list").at("total") == 1); // the discarded entity is not there

	fixture.Reject("scene.close", J(R"({"path": "Scenes/Nope.gscene"})"), CommandErrorCode::NotFound);
	fixture.Reject("scene.save", J(R"({"path": "Scenes/Nope.gscene"})"), CommandErrorCode::NotFound);
	fixture.Reject("scene.set-active", J(R"({"path": "Scenes/Nope.gscene"})"), CommandErrorCode::NotFound);
}

TEST_CASE("Automation: scene paths are checked")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const auto create = [&](std::string_view path) {
		Json arguments = Json::object();
		arguments["path"] = std::string(path);
		return fixture.Reject("scene.new", arguments, CommandErrorCode::InvalidArguments).Message;
	};
	CHECK(create("Scenes/Level.txt") == "'Scenes/Level.txt' must end in '.gscene'");
	CHECK(
		create("Elsewhere/Level.gscene")
			.find("outside the asset directories of the project (Assets, Scenes, Data, Scripts)")
		!= std::string::npos);
	CHECK(create("../Level.gscene").find("not a valid project path") != std::string::npos);
	CHECK(create("/tmp/Level.gscene").find("not a path inside the project") != std::string::npos);
	CHECK(create("Scenes/aux.gscene").find("not a valid project path") != std::string::npos);

	fixture.Reject("scene.new", J(R"({"path": "Scenes/Main.gscene"})"), CommandErrorCode::AlreadyExists); // open
	REQUIRE(fixture.Context.CloseScene("Scenes/Main.gscene", true).HasValue());
	const CommandResult exists =
		fixture.Reject("scene.new", J(R"({"path": "Scenes/Main.gscene"})"), CommandErrorCode::AlreadyExists); // on disk
	CHECK(exists.Message == "the scene 'Scenes/Main.gscene' exists already: open it with scene.open");
	fixture.Reject("scene.open", J(R"({"path": "Scenes/Missing.gscene"})"), CommandErrorCode::NotFound);
	fixture.Reject("scene.new", J(R"({"path": "Scenes/X.gscene", "seed": -1})"), CommandErrorCode::InvalidArguments);

	// Path spellings of an open scene all find it.
	fixture.Succeed("scene.open", J(R"({"path": "Scenes\\Main.gscene"})"));
	CHECK(fixture.Succeed("scene.open", J(R"({"path": "./Scenes//Main.gscene"})")).at("alreadyOpen") == true);
	CHECK(fixture.Context.GetScenes().size() == 1);
}

TEST_CASE("Automation: scene.open reports what the file contained that was ignored")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	std::ofstream(fixture.GetProjectDirectory() / "Scenes/Odd.gscene")
		<< R"({"Version": 1, "Name": "Odd", "Seed": 1, "Mood": "calm", "Entities": [
			{"UUID": "0x1", "Components": {"Tag": {"Name": "A", "Colour": 1}, "Ghost": {}}}]})";

	const CommandResult opened = fixture.Run("scene.open", J(R"({"path": "Scenes/Odd.gscene"})"));
	REQUIRE(opened.Ok);
	CHECK(opened.Data.at("entityCount") == 1);
	REQUIRE(opened.Warnings.size() == 3);
	std::string all;
	for (const std::string& warning : opened.Warnings)
	{
		all += warning + "\n";
	}
	CHECK(all.find("Scenes/Odd.gscene/Mood: unknown member, ignored") != std::string::npos);
	CHECK(all.find("unknown component, ignored") != std::string::npos);
	CHECK(all.find("Colour: unknown field, ignored") != std::string::npos);

	std::ofstream(fixture.GetProjectDirectory() / "Scenes/Broken.gscene")
		<< R"({"Version": 1, "Entities": [{"UUID": "0x1"}, {"UUID": "0x1"}]})";
	fixture.Reject("scene.open", J(R"({"path": "Scenes/Broken.gscene"})"), CommandErrorCode::Failed);
	CHECK(fixture.Context.FindScene("Scenes/Broken.gscene") == nullptr);
}

TEST_CASE("Automation: scene.list shows open scenes and the scene files of the project")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Succeed("scene.new", J(R"({"path": "Scenes/B.gscene"})"));
	fixture.Succeed("scene.save");
	fixture.Succeed("scene.close");
	fixture.Succeed("scene.new", J(R"({"path": "Scenes/C.gscene"})"));
	fixture.Create("Thing");

	const Json list = fixture.Succeed("scene.list");
	CHECK(list.at("count") == 3);
	std::map<std::string, Json> byPath;
	for (const Json& scene : list.at("scenes"))
	{
		byPath.emplace(scene.at("path").get<std::string>(), scene);
	}
	CHECK(byPath.at("Scenes/Main.gscene").at("open") == true);
	CHECK(byPath.at("Scenes/Main.gscene").at("active") == false);
	CHECK(byPath.at("Scenes/C.gscene").at("active") == true);
	CHECK(byPath.at("Scenes/C.gscene").at("dirty") == true);
	CHECK(byPath.at("Scenes/C.gscene").at("entityCount") == 1);
	CHECK(byPath.at("Scenes/B.gscene").at("open") == false); // on disk only
	CHECK(byPath.at("Scenes/B.gscene").contains("handle"));
}

TEST_CASE("Automation: a saved scene is byte for byte what a fresh load saves again")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create(
		"Root", J(R"({"components": {"Transform": {"Translation": [0.1, 2, 3]}, "Health": {"Current": 0.3}}})"));
	fixture.Create("A", J(R"({"parent": "Root"})"));
	fixture.Create("B", J(R"({"parent": "Root", "index": 0})"));
	fixture.Create("Other");
	fixture.Succeed("scene.save");
	const std::string text = ProjectFile(fixture, "Scenes/Main.gscene");

	fixture.Succeed("scene.close");
	fixture.Succeed("scene.open", J(R"({"path": "Scenes/Main.gscene"})"));
	fixture.Succeed(
		"component.set", J(R"({"entity": "Root", "component": "Health", "values": {"Current": 0.3}})")); // no change
	fixture.Succeed("scene.save", J(R"({"all": true})"));
	CHECK(ProjectFile(fixture, "Scenes/Main.gscene") == text);
}
