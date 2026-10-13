#include <doctest/doctest.h>

#include "Reflection/ReflectionTestTypes.h"
#include "TestSupport.h"

#include "Cli/AutomationCommands.h"

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Core/PlatformDetection.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Cli;
using namespace Gambit::Tests;

namespace
{
	struct Captured
	{
		int ExitCode = -1;
		std::string Out;
		std::string Err;

		[[nodiscard]] Json AsJson() const
		{
			INFO("output: ", Out);
			const Result<Json> parsed = ParseJson(Out);
			REQUIRE(parsed.HasValue());
			return *parsed;
		}
	};

	Json J(std::string_view text)
	{
		return Json::parse(text);
	}

	// Sets an environment variable of this process until the end of the scope.
	class ScopedEnvironmentVariable
	{
	public:
		ScopedEnvironmentVariable(const char* name, const char* value)
			: m_Name(name)
		{
#if defined(GB_PLATFORM_WINDOWS)
			_putenv_s(name, value);
#else
			setenv(name, value, 1);
#endif
		}

		~ScopedEnvironmentVariable()
		{
#if defined(GB_PLATFORM_WINDOWS)
			_putenv_s(m_Name, "");
#else
			unsetenv(m_Name);
#endif
		}

		ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
		ScopedEnvironmentVariable& operator=(const ScopedEnvironmentVariable&) = delete;

	private:
		const char* m_Name;
	};

	// The gambit tool with the test components, running in this process on a temporary directory.
	class Tool
	{
	public:
		ScopedTempDirectory Directory;
		TypeRegistry Types;
		CommandDispatcher Dispatcher{ "gambit" };

		Tool()
		{
			RegisterTestTypes(Types);
			RegisterBuiltinCommands(Dispatcher);
			RegisterAutomationCommands(Dispatcher, Types);
		}

		[[nodiscard]] std::string Project() const { return (Directory.GetPath() / "Game").generic_string(); }

		[[nodiscard]] Captured Run(const std::vector<std::string>& args, const std::string& input = {}) const
		{
			std::ostringstream out;
			std::ostringstream err;
			std::istringstream in(input);
			Captured captured;
			captured.ExitCode = Dispatcher.Run(args, CommandContext{ out, err, in });
			captured.Out = out.str();
			captured.Err = err.str();
			return captured;
		}

		// A run that must succeed; the arguments get the project.
		Captured Succeed(std::vector<std::string> args) const
		{
			args.insert(args.end(), { "--project", Project() });
			const Captured result = Run(args);
			INFO("command: ", args.front(), " ", args.size() > 1 ? args[1] : std::string(), "\nerrors: ", result.Err);
			REQUIRE(result.ExitCode == ExitSuccess);
			return result;
		}

		void CreateProject() const
		{
			const Captured created = Run({ "project", "new", "--directory", Project(), "--name", "Game" });
			INFO(created.Err);
			REQUIRE(created.ExitCode == ExitSuccess);
		}

		[[nodiscard]] std::string ReadProjectFile(std::string_view relative) const
		{
			return ReadTextFile(Directory.GetPath() / "Game" / std::string(relative));
		}

		void WriteNative(const std::filesystem::path& path, std::string_view text) const
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream(path, std::ios::binary) << text;
		}

		[[nodiscard]] std::filesystem::path BatchFile(std::string_view text) const
		{
			const std::filesystem::path path = Directory.GetPath() / "commands.jsonl";
			WriteNative(path, text);
			return path;
		}
	};
}

TEST_CASE("CLI: an automation command runs against the project and saves what it changed")
{
	const Tool tool;
	const Captured created = tool.Run({ "project", "new", "--directory", tool.Project(), "--name", "Game" });
	CHECK(created.ExitCode == ExitSuccess);
	CHECK(created.Out.starts_with("Created project 'Game' in "));
	CHECK(std::filesystem::exists(tool.Directory.GetPath() / "Game" / "Game.gproject"));
	CHECK(created.Err.empty());

	// Spaced and dotted names are the same command; options become arguments.
	const Captured crate =
		tool.Succeed({ "entity", "create", "--name", "Crate", "--components", R"({"Health": {"Current": 5}})" });
	CHECK(crate.Out.find("Created entity 'Crate' (0x") == 0);
	CHECK(crate.Out.find("saved Scenes/Main.gscene\n") != std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene").find("\"Current\": 5.0") != std::string::npos);
	const Captured barrel = tool.Succeed({ "entity.create", "--name", "Barrel", "--parent", "Crate" });
	CHECK(barrel.Out.find("saved Scenes/Main.gscene") != std::string::npos);

	// A later run starts from the files.
	const Captured list = tool.Succeed({ "entity", "list" });
	CHECK(list.Out.find("Crate") != std::string::npos);
	CHECK(list.Out.find("Barrel") != std::string::npos);
	CHECK(list.Out.find("saved") == std::string::npos); // reading saves nothing
	CHECK(list.Out.find("total: 2") != std::string::npos);

	const Captured get = tool.Succeed({ "component", "get", "--entity", "Crate", "--component", "Health", "--json" });
	const Json document = get.AsJson();
	CHECK(document.at("ok") == true);
	CHECK(document.at("data").at("values") == J(R"({"Current": 5.0, "Maximum": 100.0})"));
	CHECK_FALSE(document.contains("saved"));

	const Captured set = tool.Succeed({ "component", "set", "--entity", "Crate", "--component", "Health", "--values",
	                                    R"({"Maximum": 50})", "--json" });
	CHECK(set.AsJson().at("saved") == J(R"(["Scenes/Main.gscene"])"));
	CHECK(set.AsJson().at("data").at("changed") == J(R"(["Maximum"])"));
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene").find("\"Maximum\": 50.0") != std::string::npos);
}

TEST_CASE("CLI: exit codes tell usage errors from failed commands")
{
	const Tool tool;
	tool.CreateProject();
	const auto run = [&](std::vector<std::string> args) {
		args.insert(args.end(), { "--project", tool.Project() });
		return tool.Run(args);
	};

	const Captured destroyed = run({ "entity", "destroy", "--entity", "Nobody" });
	CHECK(destroyed.ExitCode == ExitFailure);
	CHECK(destroyed.Err.starts_with("gambit: there is no entity named 'Nobody'"));
	CHECK(destroyed.Out.empty());

	// The command line itself is wrong: unknown command, unknown option, bad or missing arguments.
	const Captured unknown = run({ "entity", "explode" });
	CHECK(unknown.ExitCode == ExitUsage);
	CHECK(unknown.Err.find("unknown command 'entity'") != std::string::npos);
	const Captured typo = run({ "entiti", "list" });
	CHECK(typo.ExitCode == ExitUsage);
	const Captured badOption = run({ "entity", "create", "--nme", "Crate" });
	CHECK(badOption.ExitCode == ExitUsage);
	CHECK(badOption.Err.find("did you mean --name?") != std::string::npos);
	CHECK(badOption.Err.find("Run 'gambit help entity.create' for its options.") != std::string::npos);
	const Captured missing = run({ "entity", "rename", "--entity", "Crate" });
	CHECK(missing.ExitCode == ExitUsage);
	CHECK(missing.Err.find("missing required argument 'name'") != std::string::npos);
	const Captured badEnum = run({ "entity", "list", "--layout", "sideways" });
	CHECK(badEnum.ExitCode == ExitUsage);
	CHECK(badEnum.Err.find("'sideways' is not one of: flat, tree") != std::string::npos);
	const Captured badValue = run({ "entity", "create", "--index", "abc" });
	CHECK(badValue.ExitCode == ExitUsage);
	CHECK(badValue.Err.find("option --index expects an integer") != std::string::npos);

	// With --json the same errors are results.
	const Captured jsonFailure = run({ "entity", "destroy", "--entity", "Nobody", "--json" });
	CHECK(jsonFailure.ExitCode == ExitFailure);
	CHECK(jsonFailure.Err.empty());
	const Json failure = jsonFailure.AsJson();
	CHECK(failure.at("ok") == false);
	CHECK(failure.at("error").at("code") == "not-found");
	const Captured jsonUsage = run({ "entity", "create", "--nme", "x", "--json" });
	CHECK(jsonUsage.ExitCode == ExitUsage);
	CHECK(jsonUsage.AsJson().at("error").at("code") == "invalid-arguments");

	// A project that cannot be opened is a failure of the command, not of the command line.
	const Captured noProject =
		tool.Run({ "entity", "list", "--project", (tool.Directory.GetPath() / "Missing").generic_string() });
	CHECK(noProject.ExitCode == ExitFailure);
	CHECK(noProject.Err.find("cannot open the project") != std::string::npos);
	const Captured noScene =
		tool.Run({ "entity", "list", "--project", tool.Project(), "--scene", "Scenes/Nope.gscene" });
	CHECK(noScene.ExitCode == ExitFailure);
	CHECK(noScene.Err.find("cannot open the scene 'Scenes/Nope.gscene'") != std::string::npos);
	const Captured sceneWithoutProject = tool.Run({ "scene", "list", "--scene", "Scenes/A.gscene" });
	CHECK(sceneWithoutProject.ExitCode == ExitFailure);
	CHECK(sceneWithoutProject.Err.find("--scene needs a project") != std::string::npos);

	// Nothing was saved by the failures.
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene").find("Crate") == std::string::npos);
}

TEST_CASE("CLI: --dry-run saves nothing and --scene picks the scene")
{
	const Tool tool;
	tool.CreateProject();
	const std::string before = tool.ReadProjectFile("Scenes/Main.gscene");

	const Captured dry = tool.Succeed({ "entity", "create", "--name", "Ghost", "--dry-run" });
	CHECK(dry.Out.find("Created entity 'Ghost'") != std::string::npos);
	CHECK(dry.Out.find("saved") == std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene") == before);
	const Captured dryJson = tool.Succeed({ "entity", "create", "--name", "Ghost", "--dry-run", "--json" });
	CHECK_FALSE(dryJson.AsJson().contains("saved"));

	// Commands that write files at once refuse.
	const Captured refused =
		tool.Run({ "project", "new", "--directory", (tool.Directory.GetPath() / "Other").generic_string(), "--name",
	               "Other", "--dry-run" });
	CHECK(refused.ExitCode == ExitFailure);
	CHECK(refused.Err.find("writes project files right away, which a dry run does not do") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(tool.Directory.GetPath() / "Other"));

	// A second scene is made, saved, and then edited by naming it.
	const Captured level = tool.Succeed({ "scene", "new", "--path", "Scenes/Level.gscene" });
	CHECK(level.Out.find("saved Scenes/Level.gscene") != std::string::npos);
	const Captured onLevel = tool.Succeed({ "entity", "create", "--name", "Tree", "--scene", "Scenes/Level.gscene" });
	CHECK(onLevel.Out.find("saved Scenes/Level.gscene") != std::string::npos);
	CHECK(onLevel.Out.find("Main.gscene") == std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Level.gscene").find("Tree") != std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene") == before);

	// --args carries the arguments as JSON; options override it.
	const Captured fromArgs = tool.Succeed(
		{ "entity", "create", "--args", R"({"name": "FromArgs", "index": 0})", "--name", "Overridden", "--json" });
	CHECK(fromArgs.AsJson().at("data").at("name") == "Overridden");
}

TEST_CASE("CLI: help is generated from the command schemas")
{
	const Tool tool;
	const Captured create = tool.Run({ "help", "entity", "create" });
	CHECK(create.ExitCode == ExitSuccess);
	CHECK(create.Out.starts_with("entity.create - Create an entity in the active scene\n"));
	CHECK(
		create.Out.find("Usage: gambit entity create [--name <string>] [--parent <string>] [--components <object>] "
	                    "[--index <integer>]")
		!= std::string::npos);
	CHECK(create.Out.find("gambit entity.create --args '<json object>'") != std::string::npos);
	CHECK(create.Out.find("--name <string>") != std::string::npos);
	CHECK(create.Out.find("Name of the entity (default: \"Entity\")") != std::string::npos);
	CHECK(create.Out.find("(min 0)") != std::string::npos);
	CHECK(create.Out.find("Result: { uuid, name, parent, enabled, index, components: [names] }") != std::string::npos);
	CHECK(create.Out.find("Flags: mutating, undoable, requires-scene") != std::string::npos);
	CHECK(create.Out.find("Global options: --project") != std::string::npos);

	const Captured rename = tool.Run({ "help", "entity.rename" });
	CHECK(rename.Out.find("Usage: gambit entity rename --entity <string> --name <string>") != std::string::npos);
	CHECK(rename.Out.find("(required)") != std::string::npos);

	const Captured list = tool.Run({ "help", "entity", "list" });
	CHECK(list.Out.find("--layout <flat|tree>") != std::string::npos);
	CHECK(list.Out.find("(default: \"flat\")") != std::string::npos);
	CHECK(list.Out.find("(min 1, max 100000)") != std::string::npos);

	// --help after the command, and a group.
	CHECK(tool.Run({ "entity", "create", "--help" }).Out == create.Out);
	CHECK(tool.Run({ "entity.create", "-h" }).Out == create.Out);
	const Captured group = tool.Run({ "help", "entity" });
	CHECK(group.ExitCode == ExitSuccess);
	CHECK(group.Out.starts_with("Commands of 'entity':\n"));
	CHECK(group.Out.find("entity.create") != std::string::npos);
	CHECK(group.Out.find("entity.find") != std::string::npos);
	CHECK(group.Out.find("scene.new") == std::string::npos);
	CHECK(tool.Run({ "entity", "--help" }).Out == group.Out);
	CHECK(tool.Run({ "help", "nope" }).ExitCode == ExitUsage);
	CHECK(tool.Run({ "help", "entity", "explode" }).ExitCode == ExitUsage);

	// The overview lists everything.
	const Captured overview = tool.Run({ "--help" });
	CHECK(overview.Out.find("Automation commands") != std::string::npos);
	for (const char* name : { "entity.create", "component.set", "scene.save", "data.add-row", "bulk.set", "comment.add",
	                          "sim.run", "undo" })
	{
		CHECK(overview.Out.find(name) != std::string::npos);
	}
	CHECK(overview.Out.find("batch") != std::string::npos);
	CHECK(overview.Out.find("Exit codes: 0 ok, 1 the command failed, 2 usage error.") != std::string::npos);
}

TEST_CASE("CLI: commands lists the registry and dumps its schemas")
{
	const Tool tool;
	const Captured plain = tool.Run({ "commands", "entity." });
	CHECK(plain.ExitCode == ExitSuccess);
	CHECK(plain.Out.starts_with(
		"entity.create     Create an entity in the active scene\n")); // names are padded to the longest
	CHECK(plain.Out.find("scene.new") == std::string::npos);

	const Captured all = tool.Run({ "commands", "--json" });
	CHECK(all.ExitCode == ExitSuccess);
	const Json document = all.AsJson();
	const Json& commands = document.at("commands");
	REQUIRE(commands.is_array());
	CommandRegistry registry;
	RegisterBuiltinCommands(registry);
	CHECK(commands.size() == registry.GetCount());
	CHECK(commands.size() > 40);
	std::vector<std::string> names;
	for (const Json& command : commands)
	{
		names.push_back(command.at("name"));
		INFO(names.back());
		CHECK_FALSE(command.at("summary").get<std::string>().empty());
		CHECK(command.at("arguments").at("type") == "object");
		CHECK(command.at("arguments").at("additionalProperties") == false);
		CHECK(command.contains("flags"));
	}
	CHECK(std::ranges::is_sorted(names));
	const auto create =
		std::ranges::find_if(commands, [](const Json& command) { return command.at("name") == "entity.create"; });
	REQUIRE(create != commands.end());
	CHECK(create->at("arguments").at("properties").at("index").at("minimum") == 0);
	CHECK(create->at("flags") == J(R"(["mutating", "undoable", "requires-scene"])"));

	const Json scenes = tool.Run({ "commands", "scene.", "--json" }).AsJson();
	CHECK(scenes.at("commands").size() == 6);
	CHECK(tool.Run({ "commands", "a", "b" }).ExitCode == ExitUsage);
}

TEST_CASE("CLI: batch runs many commands in one process and saves at the end")
{
	const Tool tool;
	tool.CreateProject();
	const std::filesystem::path clean = tool.BatchFile(
		R"({"command": "entity.create", "args": {"name": "Crate", "components": {"Health": {}}}})"
		"\n"
		R"({"command": "entity.create", "args": {"name": "Barrel", "parent": "Crate"}})"
		"\n"
		R"({"command": "component.set", "args": {"entity": "Crate", "component": "Health", "values": {"Current": 7}}})"
		"\n"
		R"({"command": "undo"})"
		"\n"
		R"({"command": "entity.list"})"
		"\n");
	const Captured ran = tool.Run({ "batch", clean.generic_string(), "--project", tool.Project() });
	INFO(ran.Err);
	CHECK(ran.ExitCode == ExitSuccess);
	CHECK(ran.Out.find("[1] entity.create: Created entity 'Crate'") == 0);
	CHECK(ran.Out.find("[2] entity.create: Created entity 'Barrel'") != std::string::npos);
	CHECK(ran.Out.find("[4] undo: Undid Set Health.Current of 'Crate'") != std::string::npos);
	CHECK(ran.Out.find("5 commands, 0 failed; saved Scenes/Main.gscene\n") != std::string::npos);
	const std::string scene = tool.ReadProjectFile("Scenes/Main.gscene");
	CHECK(scene.find("Barrel") != std::string::npos);
	CHECK(scene.find("\"Current\": 100.0") != std::string::npos); // undone

	// JSON mode, on a fresh project (the names must be unique): one result per line and a summary.
	const Tool other;
	other.CreateProject();
	const Captured json =
		other.Run({ "batch", clean.generic_string(), "--project", other.Project(), "--json", "--dry-run" });
	CHECK(json.ExitCode == ExitSuccess);
	std::vector<Json> lines;
	std::istringstream stream(json.Out);
	for (std::string line; std::getline(stream, line);)
	{
		lines.push_back(J(line));
	}
	REQUIRE(lines.size() == 6);
	CHECK(lines[0].at("line") == 1);
	CHECK(lines[0].at("command") == "entity.create");
	CHECK(lines[0].at("ok") == true);
	CHECK(
		lines[5].at("summary")
		== J(R"({"executed": 5, "failed": 0, "saved": [], "note": "dry run: nothing was saved"})"));
}

TEST_CASE("CLI: batch stops at the first failure and saves nothing, unless told to keep going")
{
	const Tool tool;
	tool.CreateProject();
	const std::string before = tool.ReadProjectFile("Scenes/Main.gscene");
	const std::filesystem::path file = tool.BatchFile(R"({"command": "entity.create", "args": {"name": "First"}})"
	                                                  "\n"
	                                                  R"({"command": "entity.destroy", "args": {"entity": "Nobody"}})"
	                                                  "\n"
	                                                  R"({"command": "entity.create", "args": {"name": "Third"}})"
	                                                  "\n");

	const Captured stopped = tool.Run({ "batch", file.generic_string(), "--project", tool.Project() });
	CHECK(stopped.ExitCode == ExitFailure);
	CHECK(stopped.Out.find("[2] entity.destroy: error: there is no entity named 'Nobody'") != std::string::npos);
	CHECK(stopped.Out.find("Third") == std::string::npos);
	CHECK(stopped.Out.find("2 commands, 1 failed\n") != std::string::npos);
	CHECK(stopped.Err.find("nothing was saved because a command failed") != std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene") == before);

	const Captured kept = tool.Run({ "batch", file.generic_string(), "--project", tool.Project(), "--keep-going" });
	CHECK(kept.ExitCode == ExitFailure);
	CHECK(kept.Out.find("Third") != std::string::npos);
	CHECK(kept.Out.find("3 commands, 1 failed; saved Scenes/Main.gscene\n") != std::string::npos);
	const std::string saved = tool.ReadProjectFile("Scenes/Main.gscene");
	CHECK(saved.find("First") != std::string::npos);
	CHECK(saved.find("Third") != std::string::npos);

	// A bad argument is a usage error; the exit code is the worst one seen.
	const std::filesystem::path usage = tool.BatchFile(R"({"command": "entity.create", "args": {"nme": "x"}})"
	                                                   "\n"
	                                                   R"({"command": "entity.destroy", "args": {"entity": "Nobody"}})"
	                                                   "\n"
	                                                   R"({"command": "no.such"})"
	                                                   "\n"
	                                                   "{broken json\n"
	                                                   R"({"args": {}})"
	                                                   "\n");
	const Captured mixed =
		tool.Run({ "batch", usage.generic_string(), "--project", tool.Project(), "--keep-going", "--json" });
	CHECK(mixed.ExitCode == ExitUsage);
	std::istringstream lines(mixed.Out);
	std::string line;
	REQUIRE(std::getline(lines, line));
	CHECK(J(line).at("error").at("code") == "invalid-arguments");
	REQUIRE(std::getline(lines, line));
	CHECK(J(line).at("error").at("code") == "not-found");
	REQUIRE(std::getline(lines, line));
	CHECK(J(line).at("error").at("code") == "unknown-command");
	REQUIRE(std::getline(lines, line));
	CHECK(J(line).at("error").at("message").get<std::string>().find("line 4") != std::string::npos);
	REQUIRE(std::getline(lines, line));
	CHECK(J(line).at("error").at("message").get<std::string>().find("expected {\"command\"") != std::string::npos);
}

TEST_CASE("CLI: batch reads standard input and checks its usage")
{
	const Tool tool;
	tool.CreateProject();
	const Captured fromInput = tool.Run(
		{ "batch", "-", "--project", tool.Project() },
		R"({"command": "entity.create", "args": {"name": "Piped"}})"
		"\n"
		R"({"command": "selection.set", "args": {"entities": ["Piped"]}})"
		"\n"
		R"({"command": "selection.get"})");
	INFO(fromInput.Err);
	CHECK(fromInput.ExitCode == ExitSuccess);
	CHECK(fromInput.Out.find("[3] selection.get: 1 selected") != std::string::npos); // a batch is one session
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene").find("Piped") != std::string::npos);

	CHECK(tool.Run({ "batch" }).ExitCode == ExitUsage);
	CHECK(tool.Run({ "batch", "a", "b" }).ExitCode == ExitUsage);
	const Captured missing =
		tool.Run({ "batch", (tool.Directory.GetPath() / "none.jsonl").generic_string(), "--project", tool.Project() });
	CHECK(missing.ExitCode == ExitFailure);
	CHECK(missing.Err.find("cannot read") != std::string::npos);
	const std::filesystem::path empty = tool.BatchFile("\n\n");
	const Captured nothing = tool.Run({ "batch", empty.generic_string(), "--project", tool.Project() });
	CHECK(nothing.ExitCode == ExitSuccess);
	CHECK(nothing.Out == "0 commands, 0 failed\n");
}

TEST_CASE("CLI: a batch can start a project of its own")
{
	const Tool tool;
	const std::filesystem::path file = tool.BatchFile(
		R"({"command": "project.new", "args": {"directory": ")" + tool.Project()
		+ R"(", "name": "Game"}})"
		  "\n"
		  R"({"command": "entity.create", "args": {"name": "Hero", "components": {"Transform": {"Translation": [1, 2, 3]}}}})"
		  "\n"
		  R"({"command": "scene.new", "args": {"path": "Scenes/Second.gscene"}})"
		  "\n"
		  R"({"command": "entity.create", "args": {"name": "Villain"}})"
		  "\n"
		  R"({"command": "scene.save", "args": {"all": true}})"
		  "\n");
	const Captured result = tool.Run({ "batch", file.generic_string() });
	INFO(result.Err);
	CHECK(result.ExitCode == ExitSuccess);
	CHECK(tool.ReadProjectFile("Scenes/Main.gscene").find("Hero") != std::string::npos);
	CHECK(tool.ReadProjectFile("Scenes/Second.gscene").find("Villain") != std::string::npos);
}

TEST_CASE("CLI: results are printed for people")
{
	CHECK(
		FormatHuman(J(R"({"count": 2, "name": "Crate", "ok": true, "none": null})"))
		== "count: 2\nname: Crate\nok: true\nnone: null\n");
	CHECK(
		FormatHuman(J(R"({"items": [{"id": "a", "n": 1}, {"id": "bb", "n": 22}]})"))
		== "items:\n  id  n\n  a   1\n  bb  22\n");
	CHECK(
		FormatHuman(J(R"({"list": [1, 2, 3], "empty": [], "map": {"a": 1}})"))
		== "list: [1,2,3]\nempty: []\nmap: {\"a\":1}\n");
	CHECK(FormatHuman(J(R"({"cells": [{"v": [1, 2], "o": {"x": 1}}]})")) == "cells:\n  v\n  1,2\n");
	CHECK(FormatHuman(J("[1, 2]")) == "[\n  1,\n  2\n]\n");

	// A tree (entity.list --layout tree): the children follow their parent, indented in the name column.
	CHECK(
		FormatHuman(
			J(R"({"roots": [{"uuid": "0x1", "name": "A", "children": [{"uuid": "0x2", "name": "B", "children": []}]}, )"
	          R"({"uuid": "0x3", "name": "C", "children": []}]})"))
		== "roots:\n  uuid  name\n  0x1   A\n  0x2     B\n  0x3   C\n");

	std::string longText(100, 'x');
	const std::string table = FormatHuman(J(R"({"rows": [{"t": ")" + longText + R"("}]})"));
	CHECK(table.find(std::string(45, 'x') + "...") != std::string::npos);
	CHECK(table.find(longText) == std::string::npos);

	// A nested object too long for one line is indented.
	Json nested = Json::object();
	nested["deep"] = Json::object();
	for (int index = 0; index < 8; ++index)
	{
		nested["deep"]["key" + std::to_string(index)] = "some value " + std::to_string(index);
	}
	const std::string text = FormatHuman(nested);
	CHECK(text.starts_with("deep:\n  {\n    \"key0\": \"some value 0\",\n"));

	CHECK(GetExitCode(CommandResult::Success()) == ExitSuccess);
	CHECK(GetExitCode(CommandResult::Failure(CommandErrorCode::NotFound, "x")) == ExitFailure);
	CHECK(GetExitCode(CommandResult::Failure(CommandErrorCode::Internal, "x")) == ExitFailure);
	CHECK(GetExitCode(CommandResult::Failure(CommandErrorCode::NoProject, "x")) == ExitFailure);
	CHECK(GetExitCode(CommandResult::Failure(CommandErrorCode::InvalidArguments, "x")) == ExitUsage);
	CHECK(GetExitCode(CommandResult::Failure(CommandErrorCode::UnknownCommand, "x")) == ExitUsage);
}

TEST_CASE("CLI: comment commands are written by GAMBIT_AUTHOR or by the user")
{
	const Tool tool;
	tool.CreateProject();
	const Captured added = tool.Succeed(
		{ "comment", "add", "--kind", "Feature", "--feature", "ui", "--text", "Menu is missing", "--json" });
	const Json thread = added.AsJson().at("data");
	CHECK(thread.at("status") == "Open");
	CHECK(thread.at("author") == "user");
	CHECK(std::filesystem::exists(
		tool.Directory.GetPath() / "Game" / "Comments" / (thread.at("id").get<std::string>() + ".json")));

	const Captured replied = tool.Succeed(
		{ "comment", "reply", "--id", thread.at("id"), "--text", "Added.", "--author", "claude", "--json" });
	CHECK(replied.AsJson().at("data").at("messages").size() == 2);
	const Captured listed = tool.Succeed({ "comment", "list", "--status", "Open" });
	CHECK(listed.Out.find("Menu is missing") != std::string::npos);
	CHECK(tool.Succeed({ "comment", "resolve", "--id", thread.at("id") }).Out.find("Resolved ") == 0);
	CHECK(tool.Succeed({ "comment", "list", "--status", "Open", "--json" }).AsJson().at("data").at("total") == 0);

	const ScopedEnvironmentVariable author("GAMBIT_AUTHOR", "build-bot");
	const Captured named =
		tool.Succeed({ "comment", "add", "--kind", "Feature", "--feature", "ci", "--text", "Flaky", "--json" });
	CHECK(named.AsJson().at("data").at("author") == "build-bot");
}
