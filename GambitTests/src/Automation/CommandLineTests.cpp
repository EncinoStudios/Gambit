#include <doctest/doctest.h>

#include "Gambit/Automation/BuiltinCommands.h"
#include "Gambit/Automation/CommandLine.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace Gambit;

namespace
{
	using Words = std::vector<std::string>;

	struct Registry
	{
		CommandRegistry Commands;

		Registry() { RegisterBuiltinCommands(Commands); }

		const CommandInfo& Get(std::string_view name) const
		{
			const CommandInfo* info = Commands.Find(name);
			REQUIRE(info != nullptr);
			return *info;
		}

		Result<Json>
		Parse(std::string_view command, const Words& tokens, std::optional<std::string> base = std::nullopt) const
		{
			return ParseCommandOptions(Get(command), tokens, base);
		}

		// The arguments of a command line that must parse.
		Json Arguments(std::string_view command, const Words& tokens) const
		{
			const Result<Json> parsed = Parse(command, tokens);
			INFO((parsed.HasValue() ? std::string() : parsed.GetError().Message));
			REQUIRE(parsed.HasValue());
			return *parsed;
		}

		std::string ErrorOf(std::string_view command, const Words& tokens) const
		{
			const Result<Json> parsed = Parse(command, tokens);
			REQUIRE_FALSE(parsed.HasValue());
			return parsed.GetError().Message;
		}
	};

	Json J(std::string_view text)
	{
		return Json::parse(text);
	}
}

TEST_CASE("Automation: command line: names convert between camelCase and kebab-case")
{
	CHECK(ToCamelCase("keep-world-transform") == "keepWorldTransform");
	CHECK(ToCamelCase("name") == "name");
	CHECK(ToCamelCase("keepWorldTransform") == "keepWorldTransform");
	CHECK(ToCamelCase("--odd--") == "odd");
	CHECK(ToKebabCase("keepWorldTransform") == "keep-world-transform");
	CHECK(ToKebabCase("name") == "name");
	for (const char* name : { "entity", "scenePath", "includeInternal", "nameContains" })
	{
		CHECK(ToCamelCase(ToKebabCase(name)) == name);
	}
}

TEST_CASE("Automation: command line: command names are read from dotted or spaced words")
{
	const Registry registry;
	const auto resolve = [&](const Words& tokens) { return ResolveCommand(registry.Commands, tokens); };

	const auto dotted = resolve({ "entity.create", "--name", "Crate" });
	REQUIRE(dotted.has_value());
	CHECK(dotted->Info->Name == "entity.create");
	CHECK(dotted->WordCount == 1);

	const auto spaced = resolve({ "entity", "create", "--name", "Crate" });
	REQUIRE(spaced.has_value());
	CHECK(spaced->Info->Name == "entity.create");
	CHECK(spaced->WordCount == 2);

	const auto hyphen = resolve({ "data", "add-row", "--table", "Data/A.csv" });
	REQUIRE(hyphen.has_value());
	CHECK(hyphen->Info->Name == "data.add-row");

	const auto single = resolve({ "undo", "--steps", "2" });
	REQUIRE(single.has_value());
	CHECK(single->Info->Name == "undo");
	CHECK(single->WordCount == 1);

	// Words after the name are not part of it.
	const auto extra = resolve({ "scene", "list", "everything" });
	REQUIRE(extra.has_value());
	CHECK(extra->Info->Name == "scene.list");
	CHECK(extra->WordCount == 2);

	CHECK_FALSE(resolve({}).has_value());
	CHECK_FALSE(resolve({ "entity" }).has_value()); // a group is not a command
	CHECK_FALSE(resolve({ "entity", "--name", "x" }).has_value());
	CHECK_FALSE(resolve({ "entitiy", "create" }).has_value());
	CHECK_FALSE(resolve({ "--json", "entity", "list" }).has_value()); // global options are removed before
}

TEST_CASE("Automation: command line: options become the arguments of a command, typed by its schema")
{
	const Registry registry;

	const Json created = registry.Arguments(
		"entity.create",
		{ "--name", "Crate", "--index", "2", "--parent=Root", "--components",
	      R"({"Transform": {"Translation": [1, 2, 3]}})" });
	CHECK(
		created
		== J(
			R"({"name": "Crate", "index": 2, "parent": "Root", "components": {"Transform": {"Translation": [1, 2, 3]}}})"));

	// Text stays text, whatever it looks like.
	CHECK(registry.Arguments("entity.create", { "--name", "42" }) == J(R"({"name": "42"})"));
	CHECK(registry.Arguments("entity.create", { "--name", "true" }) == J(R"({"name": "true"})"));
	CHECK(registry.Arguments("entity.create", { "--name=a=b" }) == J(R"({"name": "a=b"})"));
	CHECK(registry.Arguments("entity.create", { "--name", "-1" }) == J(R"({"name": "-1"})"));

	// Camel case and kebab case name the same option.
	CHECK(
		registry.Arguments("entity.reparent", { "--entity", "A", "--keep-world-transform" })
		== J(R"({"entity": "A", "keepWorldTransform": true})"));
	CHECK(
		registry.Arguments("entity.reparent", { "--entity", "A", "--keepWorldTransform" })
		== J(R"({"entity": "A", "keepWorldTransform": true})"));
	CHECK(
		registry.Arguments("comment.list", { "--scene-path", "Scenes/A.gscene" })
		== J(R"({"scenePath": "Scenes/A.gscene"})"));

	// Integers above INT64_MAX (64-bit seeds) are read; the schema decides whether they fit.
	const Json seeded = registry.Arguments("sim.run", { "--ticks", "1", "--seed", "18446744073709551615" });
	CHECK(seeded.at("seed").get<uint64_t>() == UINT64_MAX);
	CHECK(registry.Get("sim.run").ParseArguments(seeded).HasValue());
}

TEST_CASE("Automation: command line: boolean options stand alone, take a value or are negated")
{
	const Registry registry;
	const auto reparent = [&](const Words& tokens) {
		Words line = { "--entity", "A" };
		line.insert(line.end(), tokens.begin(), tokens.end());
		return registry.Arguments("entity.reparent", line).at("keepWorldTransform");
	};
	CHECK(reparent({ "--keep-world-transform" }) == true);
	CHECK(reparent({ "--keep-world-transform", "true" }) == true);
	CHECK(reparent({ "--keep-world-transform", "false" }) == false);
	CHECK(reparent({ "--keep-world-transform", "off" }) == false);
	CHECK(reparent({ "--keep-world-transform", "YES" }) == true);
	CHECK(reparent({ "--keep-world-transform=0" }) == false);
	CHECK(reparent({ "--no-keep-world-transform" }) == false);

	// A flag followed by another option, or by a word that is no boolean, does not swallow it.
	const Json run = registry.Arguments("sim.run", { "--hashes", "--ticks", "5" });
	CHECK(run == J(R"({"hashes": true, "ticks": 5})"));
	CHECK(
		registry.ErrorOf("entity.reparent", { "--entity", "A", "--keep-world-transform", "maybe" })
			.find("unexpected argument 'maybe'")
		!= std::string::npos);
	CHECK(
		registry.ErrorOf("entity.reparent", { "--entity", "A", "--keep-world-transform=maybe" })
			.find("expects true or false, got 'maybe'")
		!= std::string::npos);
	CHECK(
		registry.ErrorOf("entity.reparent", { "--entity", "A", "--no-keep-world-transform=true" })
			.find("has no option --no-keep-world-transform")
		!= std::string::npos);
}

TEST_CASE("Automation: command line: array, object and any-typed options")
{
	const Registry registry;

	CHECK(registry.Arguments("selection.set", { "--entities", "A" }) == J(R"({"entities": ["A"]})"));
	CHECK(
		registry.Arguments("selection.set", { "--entities", "A", "--entities", "B" })
		== J(R"({"entities": ["A", "B"]})"));
	CHECK(registry.Arguments("selection.set", { "--entities", R"(["A", "B"])" }) == J(R"({"entities": ["A", "B"]})"));
	CHECK(
		registry.Arguments("sim.run", { "--ticks", "3", "--components", "Health", "--components", "Tag" })
		== J(R"({"ticks": 3, "components": ["Health", "Tag"]})"));
	CHECK(
		registry
			.Arguments(
				"comment.add", { "--kind", "Position", "--position", "1", "--position", "2.5", "--position", "-3" })
			.at("position")
		== J("[1, 2.5, -3]"));
	CHECK(
		registry.Arguments("comment.add", { "--kind", "Position", "--position", "[1, 2.5, -3]" }).at("position")
		== J("[1, 2.5, -3]"));

	// Any: JSON when it parses, else text.
	const auto value = [&](const char* text) {
		return registry.Arguments("data.set", { "--table", "T", "--row", "r", "--column", "c", "--value", text })
			.at("value");
	};
	CHECK(value("12.5") == 12.5);
	CHECK(value("7") == 7);
	CHECK(value("true") == true);
	CHECK(value("Pistol") == "Pistol");
	CHECK(value("\"7\"") == "7");
	CHECK(value("[1, 2]") == J("[1, 2]"));

	CHECK(
		registry.ErrorOf("selection.set", { "--entities", "[1, 2" }).find("expects a JSON array") != std::string::npos);
	CHECK(
		registry.ErrorOf("entity.create", { "--components", "{oops}" }).find("expects a JSON object")
		!= std::string::npos);
	CHECK(
		registry.ErrorOf("entity.create", { "--components", "[1]" }).find("expects a JSON object")
		!= std::string::npos);
	CHECK(
		registry.Arguments("sim.run", { "--components", "5", "--ticks", "1" })
		== J(R"({"components": ["5"], "ticks": 1})")); // text, as the schema says
}

TEST_CASE("Automation: command line: the --args object is the base that options override")
{
	const Registry registry;
	const Result<Json> merged =
		registry.Parse("entity.create", { "--name", "Override" }, std::string(R"({"name": "Base", "index": 3})"));
	REQUIRE(merged.HasValue());
	CHECK(*merged == J(R"({"name": "Override", "index": 3})"));

	const Result<Json> only = registry.Parse("entity.create", {}, std::string(R"({"components": {"Transform": {}}})"));
	REQUIRE(only.HasValue());
	CHECK(*only == J(R"({"components": {"Transform": {}}})"));

	const Result<Json> broken = registry.Parse("entity.create", {}, std::string("{nope"));
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.find("--args") != std::string::npos);
	const Result<Json> array = registry.Parse("entity.create", {}, std::string("[1]"));
	REQUIRE_FALSE(array.HasValue());
	CHECK(array.GetError().Message == "--args expects a JSON object");
}

TEST_CASE("Automation: command line: option mistakes are explained")
{
	const Registry registry;
	CHECK(
		registry.ErrorOf("entity.create", { "--nme", "Crate" })
		== "'entity.create' has no option --nme (did you mean --name?); its options are: --name, --parent, "
		   "--components, --index");
	CHECK(registry.ErrorOf("entity.create", { "--zzz", "1" }).find("(did you mean") == std::string::npos);
	CHECK(registry.ErrorOf("entity.create", { "--name" }) == "option --name needs a value");
	CHECK(registry.ErrorOf("entity.create", { "--name", "--index", "1" }) == "option --name needs a value");
	CHECK(
		registry.ErrorOf("entity.create", { "Crate" })
		== "unexpected argument 'Crate': options look like --name value");
	CHECK(registry.ErrorOf("entity.create", { "-x" }) == "unexpected argument '-x': options look like --name value");
	CHECK(registry.ErrorOf("entity.create", { "--index", "abc" }) == "option --index expects an integer, got 'abc'");
	CHECK(registry.ErrorOf("entity.create", { "--index", "1.5" }) == "option --index expects an integer, got '1.5'");
	CHECK(
		registry.ErrorOf("sim.run", { "--ticks", "10", "--seed", "x" }).find("expects an integer")
		!= std::string::npos);
	CHECK(registry.ErrorOf("undo", { "--steps" }) == "option --steps needs a value");
	CHECK(
		registry.ErrorOf("project.info", { "--anything" })
		== "'project.info' has no option --anything; it takes no options");

	// Required arguments and enum values are the registry's job, not the parser's.
	const Result<Json> incomplete = registry.Parse("entity.rename", { "--entity", "A" });
	REQUIRE(incomplete.HasValue());
	CHECK(*incomplete == J(R"({"entity": "A"})"));
	const Result<Json> enumerated = registry.Parse("entity.list", { "--layout", "sideways" });
	REQUIRE(enumerated.HasValue());
	CHECK(*enumerated == J(R"({"layout": "sideways"})"));
}
