#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/Process.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Core/DirectoryMount.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Core/Version.h"
#include "Gambit/ECS/Entity.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <filesystem>
#include <string>
#include <vector>

// These tests run the real `gambit` binary (GB_TEST_GAMBIT_EXE, built by the same CMake run) the way a person or an
// agent would: one process per command, the project on disk the only thing that connects them.

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	struct Run
	{
		ProcessResult Process;

		[[nodiscard]] int ExitCode() const { return Process.ExitCode; }

		[[nodiscard]] Json AsJson() const
		{
			INFO("stdout: ", Process.Output, "\nstderr: ", Process.Errors);
			const Result<Json> parsed = ParseJson(Process.Output);
			REQUIRE(parsed.HasValue());
			return *parsed;
		}

		// The "data" of a --json result that must have succeeded.
		[[nodiscard]] Json Data() const
		{
			const Json document = AsJson();
			REQUIRE(document.at("ok") == true);
			return document.at("data");
		}
	};

	class Tool
	{
	public:
		ScopedTempDirectory Directory;

		[[nodiscard]] std::filesystem::path ProjectDirectory() const { return Directory.GetPath() / "Tetris"; }

		// Runs the binary; the project is passed unless the arguments say otherwise.
		[[nodiscard]] Run Execute(
			std::vector<std::string> arguments, const std::filesystem::path& workingDirectory = {},
			std::string input = {}) const
		{
			ProcessOptions options;
			options.WorkingDirectory = workingDirectory;
			options.Input = std::move(input);
			Run run{ RunProcess(GB_TEST_GAMBIT_EXE, arguments, options) };
			INFO("command: gambit ", arguments.empty() ? std::string() : arguments.front(), " ...");
			INFO("start error: ", run.Process.StartError);
			REQUIRE(run.Process.Started);
			CHECK_FALSE(run.Process.TimedOut);
			INFO("wait error: ", run.Process.WaitError);
			REQUIRE(run.Process.WaitError.empty()); // else ExitCode means nothing
			return run;
		}

		// Runs a command on the project that must succeed, with --json; returns its data.
		Json Call(std::vector<std::string> arguments) const
		{
			arguments.insert(arguments.end(), { "--project", ProjectDirectory().generic_string(), "--json" });
			const Run run = Execute(std::move(arguments));
			INFO("stderr: ", run.Process.Errors);
			REQUIRE(run.ExitCode() == 0);
			return run.Data();
		}
	};

	struct Loaded
	{
		TypeRegistry Types;
		Scope<World> Scene;
		SceneMetadata Metadata;
	};

	// Loads a scene file of the project with the engine, to check what the binary wrote.
	void LoadScene(Loaded& loaded, const std::filesystem::path& projectDirectory, const std::string& scenePath)
	{
		RegisterEngineTypes(loaded.Types);
		loaded.Scene = CreateScope<World>(loaded.Types);
		VFS vfs;
		REQUIRE(vfs.Mount("", CreateRef<DirectoryMount>(projectDirectory)).HasValue());
		LoadReport report;
		Result<SceneMetadata> metadata = SceneSerializer::Load(*loaded.Scene, vfs, scenePath, report);
		INFO((metadata.HasValue() ? std::string() : metadata.GetError().Message));
		REQUIRE(metadata.HasValue());
		CHECK(report.IsEmpty());
		loaded.Metadata = std::move(*metadata);
	}
}

TEST_CASE(
	"CLI: the gambit binary builds a project over several runs and the files reload" * doctest::test_suite("slow"))
{
	const Tool gambit;
	const std::string project = gambit.ProjectDirectory().generic_string();

	const Run version = gambit.Execute({ "--version" });
	CHECK(version.ExitCode() == 0);
	CHECK(version.Process.Output == std::string("gambit ") + std::string(VersionString) + "\n");

	// Create the project; its start scene is the first scene.
	const Run created = gambit.Execute({ "project", "new", "--directory", project, "--name", "Tetris", "--json" });
	REQUIRE(created.ExitCode() == 0);
	CHECK(created.Data().at("startScene") == "Scenes/Main.gscene");
	CHECK(std::filesystem::exists(gambit.ProjectDirectory() / "Tetris.gproject"));

	// A second scene with a name and a seed; it is saved when the command ends.
	const Json level =
		gambit.Call({ "scene", "new", "--path", "Scenes/Level1.gscene", "--name", "Level 1", "--seed", "7" });
	CHECK(level.at("name") == "Level 1");
	CHECK(std::filesystem::exists(gambit.ProjectDirectory() / "Scenes" / "Level1.gscene"));

	// Entities, one run each.
	const Json player = gambit.Call({ "entity", "create", "--scene", "Scenes/Level1.gscene", "--name", "Player",
	                                  "--components", R"({"Transform": {"Translation": [0, 1, 0]}})" });
	const Json hat =
		gambit.Call({ "entity", "create", "--scene", "Scenes/Level1.gscene", "--name", "Hat", "--parent", "Player" });
	CHECK(hat.at("parent") == player.at("uuid"));
	gambit.Call({ "entity", "create", "--scene", "Scenes/Level1.gscene", "--name", "Floor" });
	gambit.Call({ "component", "add", "--scene", "Scenes/Level1.gscene", "--entity", "Hat", "--component", "Transform",
	              "--values", R"({"Translation": [0, 0.5, 0]})" });

	// A later process sees what the earlier ones saved.
	const Json tree = gambit.Call({ "entity", "list", "--scene", "Scenes/Level1.gscene", "--layout", "tree" });
	REQUIRE(tree.at("roots").size() == 2);
	CHECK(tree.at("roots")[0].at("name") == "Player");
	CHECK(tree.at("roots")[0].at("children")[0].at("name") == "Hat");
	CHECK(tree.at("roots")[1].at("name") == "Floor");
	const Json hatData = gambit.Call({ "entity", "get", "--scene", "Scenes/Level1.gscene", "--entity", "Hat" });
	CHECK(hatData.at("components").at("Transform").at("Translation") == Json::parse("[0.0, 0.5, 0.0]"));

	// The engine reads the same files: the hierarchy, the seed and the values.
	Loaded level1;
	LoadScene(level1, gambit.ProjectDirectory(), "Scenes/Level1.gscene");
	CHECK(level1.Metadata.Name == "Level 1");
	CHECK(level1.Scene->GetSeed() == 7);
	CHECK(level1.Scene->GetEntityCount() == 3);
	const Entity loadedHat = level1.Scene->TryGetEntity(*UUID::Parse(hat.at("uuid").get<std::string>()));
	REQUIRE(loadedHat.IsValid());
	CHECK(loadedHat.GetName() == "Hat");
	CHECK(level1.Scene->GetParent(loadedHat).GetName() == "Player");
	CHECK(loadedHat.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 0.5f, 0.0f));
	Loaded main;
	LoadScene(main, gambit.ProjectDirectory(), "Scenes/Main.gscene");
	CHECK(main.Scene->GetEntityCount() == 0); // the other scene was left alone

	// Edit, undo is per process (nothing to undo in a new one), destroy.
	gambit.Call({ "component", "set", "--scene", "Scenes/Level1.gscene", "--entity", "Hat", "--component", "Transform",
	              "--values", R"({"Translation": [0, 2, 0]})" });
	const Run undo = gambit.Execute({ "undo", "--project", project, "--scene", "Scenes/Level1.gscene", "--json" });
	CHECK(undo.ExitCode() == 1);
	CHECK(undo.AsJson().at("error").at("code") == "conflict");
	const Json destroyed =
		gambit.Call({ "entity", "destroy", "--scene", "Scenes/Level1.gscene", "--entity", "Player" });
	CHECK(destroyed.at("entityCount") == 2); // Player and Hat
	Loaded after;
	LoadScene(after, gambit.ProjectDirectory(), "Scenes/Level1.gscene");
	CHECK(after.Scene->GetEntityCount() == 1);
}

TEST_CASE(
	"CLI: the gambit binary reports usage errors, failures and unknown projects with exit codes"
	* doctest::test_suite("slow"))
{
	const Tool gambit;
	const std::string project = gambit.ProjectDirectory().generic_string();
	REQUIRE(gambit.Execute({ "project", "new", "--directory", project, "--name", "Tetris" }).ExitCode() == 0);

	const Run unknownCommand = gambit.Execute({ "frobnicate" });
	CHECK(unknownCommand.ExitCode() == 2);
	CHECK(unknownCommand.Process.Output.empty());
	CHECK(unknownCommand.Process.Errors.find("unknown command 'frobnicate'") != std::string::npos);

	const Run noArguments = gambit.Execute({});
	CHECK(noArguments.ExitCode() == 2);
	CHECK(noArguments.Process.Errors.find("Usage: gambit") != std::string::npos);

	const Run badOption = gambit.Execute({ "entity", "create", "--nme", "x", "--project", project });
	CHECK(badOption.ExitCode() == 2);
	CHECK(badOption.Process.Errors.find("did you mean --name?") != std::string::npos);

	const Run failed = gambit.Execute({ "entity", "destroy", "--entity", "Nobody", "--project", project });
	CHECK(failed.ExitCode() == 1);
	CHECK(failed.Process.Errors.find("there is no entity named 'Nobody'") != std::string::npos);

	const Run noProject =
		gambit.Execute({ "entity", "list", "--project", (gambit.Directory.GetPath() / "Missing").generic_string() });
	CHECK(noProject.ExitCode() == 1);
	CHECK(noProject.Process.Errors.find("cannot open the project") != std::string::npos);

	// Without --project the project in the current directory is used; in a directory without one, commands say so.
	const Run inProject = gambit.Execute({ "entity", "create", "--name", "Here", "--json" }, gambit.ProjectDirectory());
	CHECK(inProject.ExitCode() == 0);
	CHECK(inProject.Data().at("name") == "Here");
	const Run listed = gambit.Execute({ "entity", "list", "--json" }, gambit.ProjectDirectory());
	CHECK(listed.Data().at("total") == 1);
	const std::filesystem::path empty = gambit.Directory.GetPath() / "Empty";
	std::filesystem::create_directories(empty);
	const Run nowhere = gambit.Execute({ "entity", "list", "--json" }, empty);
	CHECK(nowhere.ExitCode() == 1);
	CHECK(nowhere.AsJson().at("error").at("code") == "no-project");
	CHECK(
		nowhere.AsJson().at("error").at("message").get<std::string>().find("needs an open project")
		!= std::string::npos);

	// Commands that need no project work anywhere.
	const Run types = gambit.Execute({ "component", "types", "--name", "Transform", "--json" }, empty);
	CHECK(types.ExitCode() == 0);
	CHECK(types.Data().at("components")[0].at("name") == "Transform");
}

TEST_CASE("CLI: gambit batch, sim run and the command list work as processes" * doctest::test_suite("slow"))
{
	const Tool gambit;
	const std::string project = gambit.ProjectDirectory().generic_string();
	REQUIRE(gambit.Execute({ "project", "new", "--directory", project, "--name", "Tetris" }).ExitCode() == 0);

	// A whole scene from standard input, in one process.
	const std::string script =
		R"({"command": "entity.create", "args": {"name": "Rig"}})"
		"\n"
		R"({"command": "entity.create", "args": {"name": "Arm", "parent": "Rig", "components": {"Transform": {"Translation": [1, 0, 0]}}}})"
		"\n"
		R"({"command": "entity.create", "args": {"name": "Hand", "parent": "Arm", "components": {"Transform": {"Translation": [1, 0, 0]}}}})"
		"\n"
		R"({"command": "selection.set", "args": {"entities": ["Hand"]}})"
		"\n"
		R"({"command": "comment.add", "args": {"kind": "Entity", "entity": "Hand", "text": "Needs fingers"}})"
		"\n";
	const Run batch = gambit.Execute({ "batch", "-", "--project", project }, {}, script);
	INFO(batch.Process.Errors);
	CHECK(batch.ExitCode() == 0);
	CHECK(batch.Process.Output.find("5 commands, 0 failed; saved Scenes/Main.gscene\n") != std::string::npos);

	// Simulating is deterministic across processes, and does not change the files.
	const std::string before = ReadTextFile(gambit.ProjectDirectory() / "Scenes" / "Main.gscene");
	const Json first = gambit.Call({ "sim", "run", "--ticks", "120", "--components", "Transform" });
	const Json second = gambit.Call({ "sim", "run", "--ticks", "120" });
	CHECK(first.at("stateHash") == second.at("stateHash"));
	CHECK(first.at("initialHash") == second.at("initialHash"));
	CHECK(first.at("tick") == 120);
	CHECK(first.at("entityCount") == 3);
	CHECK(ReadTextFile(gambit.ProjectDirectory() / "Scenes" / "Main.gscene") == before);

	// Comments are files of the project.
	const Json comments = gambit.Call({ "comment", "list", "--entity", "Hand" });
	CHECK(comments.at("total") == 1);
	CHECK(comments.at("threads")[0].at("text") == "Needs fingers");
	size_t threadFiles = 0;
	for (const auto& item : std::filesystem::directory_iterator(gambit.ProjectDirectory() / "Comments"))
	{
		threadFiles += item.path().extension() == ".json" ? 1 : 0;
	}
	CHECK(threadFiles == 1);

	// The schemas of the commands, for tools that build on them.
	const Run commands = gambit.Execute({ "commands", "--json" });
	CHECK(commands.ExitCode() == 0);
	CommandRegistry registry;
	RegisterBuiltinCommands(registry);
	CHECK(commands.AsJson().at("commands").size() == registry.GetCount());
	const Run help = gambit.Execute({ "help", "entity", "create" });
	CHECK(help.ExitCode() == 0);
	CHECK(help.Process.Output.find("Usage: gambit entity create") != std::string::npos);

	// A failing batch exits with 1 and saves nothing.
	const Run failing = gambit.Execute(
		{ "batch", "-", "--project", project }, {},
		R"({"command": "entity.create", "args": {"name": "Lost"}})"
		"\n"
		R"({"command": "entity.destroy", "args": {"entity": "Nobody"}})"
		"\n");
	CHECK(failing.ExitCode() == 1);
	CHECK(ReadTextFile(gambit.ProjectDirectory() / "Scenes" / "Main.gscene") == before);
}
