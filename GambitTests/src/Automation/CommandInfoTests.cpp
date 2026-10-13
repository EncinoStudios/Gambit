#include <doctest/doctest.h>

#include "Gambit/Automation/CommandInfo.h"

using namespace Gambit;

namespace
{
	Json J(std::string_view text)
	{
		return Json::parse(text);
	}

	CommandInfo MakeInfo()
	{
		return CommandInfo{
			.Name = "thing.make",
			.Summary = "Make a thing",
			.Arguments = { StringArg("name", "The name").Require(),
			               IntegerArg("count", "How many").WithRange(1, 10).WithDefault(1),
			               NumberArg("scale", "The scale"), BooleanArg("shiny", "Whether it shines").WithDefault(false),
			               EnumArg("kind", { "small", "large" }, "The kind").WithDefault("small"),
			               ArrayArg("tags", ArgumentType::String, "Tags"), ObjectArg("extra", "More"),
			               AnyArg("anything", "Whatever") },
			.ResultDescription = "{ id }",
			.Flags = CommandFlags::Mutating,
		};
	}
}

TEST_CASE("Automation: arguments are completed with defaults and normalized")
{
	const CommandInfo info = MakeInfo();
	const Result<Json> parsed = info.ParseArguments(J(R"({"name": "x", "count": 3.0})"));
	REQUIRE(parsed.HasValue());
	CHECK(*parsed == J(R"({"name": "x", "count": 3, "shiny": false, "kind": "small"})"));
	CHECK(parsed->at("count").is_number_integer()); // 3.0 became 3

	// null stands for an absent argument.
	const Result<Json> withNull = info.ParseArguments(J(R"({"name": "x", "count": null, "scale": null})"));
	REQUIRE(withNull.HasValue());
	CHECK(withNull->at("count") == 1);
	CHECK_FALSE(withNull->contains("scale"));

	// No arguments at all is null or an empty object.
	CHECK(StringArg("a", "").Name == "a");
	CommandInfo none{ .Name = "a.b", .Summary = "s" };
	CHECK(none.ParseArguments(Json(nullptr)).HasValue());
	CHECK(none.ParseArguments(Json::object())->empty());
}

TEST_CASE("Automation: argument errors name the argument and say what is valid")
{
	const CommandInfo info = MakeInfo();
	const auto message = [&](std::string_view json) {
		const Result<Json> parsed = info.ParseArguments(J(json));
		REQUIRE_FALSE(parsed.HasValue());
		return parsed.GetError().Message;
	};

	CHECK(message(R"({})") == "missing required argument 'name' (The name)");
	CHECK(message(R"({"name": 5})") == "argument 'name': expected a string, found an integer");
	CHECK(message(R"({"name": "x", "count": "many"})") == "argument 'count': expected an integer, found a string");
	CHECK(message(R"({"name": "x", "count": 1.5})") == "argument 'count': expected an integer, found a number");
	CHECK(message(R"({"name": "x", "count": 0})") == "argument 'count': 0 is below the minimum 1");
	CHECK(message(R"({"name": "x", "count": 11})") == "argument 'count': 11 is above the maximum 10");
	CHECK(message(R"({"name": "x", "scale": "big"})") == "argument 'scale': expected a number, found a string");
	CHECK(message(R"({"name": "x", "shiny": "yes"})") == "argument 'shiny': expected true or false, found a string");
	CHECK(message(R"({"name": "x", "kind": "huge"})") == "argument 'kind': 'huge' is not one of: small, large");
	CHECK(message(R"({"name": "x", "tags": "a"})") == "argument 'tags': expected an array, found a string");
	CHECK(message(R"({"name": "x", "tags": ["a", 2]})") == "argument 'tags[1]': expected a string, found an integer");
	CHECK(message(R"({"name": "x", "extra": []})") == "argument 'extra': expected an object, found an array");
	CHECK(
		message(R"({"name": "x", "colour": 1})")
		== "'thing.make' has no argument 'colour'; its arguments are: name, count, scale, shiny, kind, tags, extra, "
		   "anything");
	CHECK(message(R"({"name": "x", "shin": true})").find("(did you mean 'shiny'?)") != std::string::npos);
	CHECK_FALSE(info.ParseArguments(J("[1]")).HasValue());
	CHECK(
		info.ParseArguments(J(R"([1])")).GetError().Message
		== "the arguments of 'thing.make' must be a JSON object, found an array");
}

TEST_CASE("Automation: arrays of enum strings check every element")
{
	CommandInfo info{ .Name = "a.b",
		              .Summary = "s",
		              .Arguments = { ArrayArg("modes", ArgumentType::String, "Modes").WithEnum({ "x", "y" }) } };
	CHECK(info.Validate().HasValue());
	CHECK(info.ParseArguments(J(R"({"modes": ["x", "y"]})")).HasValue());
	const Result<Json> bad = info.ParseArguments(J(R"({"modes": ["x", "z"]})"));
	REQUIRE_FALSE(bad.HasValue());
	CHECK(bad.GetError().Message == "argument 'modes[1]': 'z' is not one of: x, y");
}

TEST_CASE("Automation: the input schema is a JSON Schema object")
{
	const Json schema = MakeInfo().GetInputSchema();
	CHECK(schema.at("type") == "object");
	CHECK(schema.at("additionalProperties") == false);
	CHECK(schema.at("required") == J(R"(["name"])"));

	const Json& properties = schema.at("properties");
	CHECK(properties.at("name") == J(R"({"type": "string", "description": "The name"})"));
	CHECK(
		properties.at("count")
		== J(R"({"type": "integer", "description": "How many", "default": 1, "minimum": 1.0, "maximum": 10.0})"));
	CHECK(properties.at("kind").at("enum") == J(R"(["small", "large"])"));
	CHECK(properties.at("tags") == J(R"({"type": "array", "description": "Tags", "items": {"type": "string"}})"));
	CHECK(properties.at("shiny").at("default") == false);
	CHECK_FALSE(properties.at("anything").contains("type")); // any JSON value

	const Json json = MakeInfo().ToJson();
	CHECK(json.at("name") == "thing.make");
	CHECK(json.at("summary") == "Make a thing");
	CHECK(json.at("result") == "{ id }");
	CHECK(json.at("flags") == J(R"(["mutating"])"));
	CHECK(json.at("arguments") == schema);
}

TEST_CASE("Automation: command descriptions are validated")
{
	const auto invalid = [](CommandInfo info) {
		return info.Validate().HasValue() ? std::string() : info.Validate().GetError().Message;
	};

	CHECK(MakeInfo().Validate().HasValue());
	CHECK(invalid({ .Name = "", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "Entity.create", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "entity..create", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "entity.", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "entity.create-", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "1a", .Summary = "s" }).find("invalid command name") == 0);
	CHECK(invalid({ .Name = "undo", .Summary = "s" }).empty());
	CHECK(invalid({ .Name = "data.add-row2", .Summary = "s" }).empty());
	CHECK(invalid({ .Name = "a.b", .Summary = "" }) == "command 'a.b' has no summary");
	CHECK(
		invalid({ .Name = "a.b", .Summary = "s", .Flags = CommandFlags::Undoable })
		== "command 'a.b' is undoable but not mutating");
	CHECK(
		invalid({ .Name = "a.b", .Summary = "s", .Flags = CommandFlags::Undoable | CommandFlags::Mutating })
		== "command 'a.b' is undoable, so it needs a project or a scene");

	const auto withArgument = [](ArgumentInfo argument) {
		return CommandInfo{ .Name = "a.b", .Summary = "s", .Arguments = { std::move(argument) } };
	};
	CHECK(invalid(withArgument(StringArg("Bad", ""))) == "command 'a.b': invalid argument name 'Bad' (use camelCase)");
	CHECK(invalid(withArgument(StringArg("bad-name", ""))).find("invalid argument name") != std::string::npos);
	CHECK(invalid(withArgument(StringArg("scene", ""))) == "command 'a.b': 'scene' is reserved by the command line");
	CHECK(invalid(withArgument(StringArg("dryRun", ""))).find("reserved") != std::string::npos);
	CHECK(
		invalid(withArgument(StringArg("x", "").Require().WithDefault("a")))
		== "command 'a.b': required argument 'x' cannot have a default");
	CHECK(
		invalid(withArgument(IntegerArg("x", "").WithEnum({ "a" })))
			.find("only strings and arrays of strings have an enum")
		!= std::string::npos);
	CHECK(
		invalid(withArgument(IntegerArg("x", "").WithDefault("text"))).find("the default of 'x' is invalid")
		!= std::string::npos);
	CHECK(
		invalid(withArgument(IntegerArg("x", "").WithRange(1, 5).WithDefault(9))).find("above the maximum")
		!= std::string::npos);

	CommandInfo twice{ .Name = "a.b", .Summary = "s", .Arguments = { StringArg("x", ""), StringArg("x", "") } };
	CHECK(invalid(twice) == "command 'a.b': argument 'x' is declared twice");
}

TEST_CASE("Automation: flags have names and similar names are suggested")
{
	CHECK(GetFlagNames(CommandFlags::None).empty());
	CHECK(
		GetFlagNames(CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene)
		== std::vector<std::string>{ "mutating", "undoable", "requires-scene" });
	CHECK(HasFlag(CommandFlags::Mutating | CommandFlags::EditorOnly, CommandFlags::EditorOnly));
	CHECK_FALSE(HasFlag(CommandFlags::Mutating, CommandFlags::EditorOnly));
	CHECK_FALSE(HasFlag(CommandFlags::Mutating, CommandFlags::None));

	const std::vector<std::string> names{ "entity.create", "entity.destroy", "component.set", "undo" };
	CHECK(FindClosestNames("entity.creat", names, 3).front() == "entity.create");
	CHECK(FindClosestNames("componnet.set", names, 1) == std::vector<std::string>{ "component.set" });
	CHECK(FindClosestNames("zzzzzzzz", names).empty());
	CHECK(FindClosestNames("", names).empty());
	CHECK(ToString(ArgumentType::Integer) == "integer");
	CHECK(ToString(ArgumentType::Any) == "any");
}

TEST_CASE("Automation: numbers in messages are written plainly")
{
	CHECK(FormatNumber(5) == "5");
	CHECK(FormatNumber(-3) == "-3");
	CHECK(FormatNumber(0) == "0");
	CHECK(FormatNumber(100000) == "100000"); // not 1e+05
	CHECK(FormatNumber(1000000) == "1000000");
	CHECK(FormatNumber(2.5) == "2.5");
	CHECK(FormatNumber(-0.125) == "-0.125");
	CHECK(FormatNumber(1.0e20) == "1e+20");

	CommandInfo info{ .Name = "a.b", .Summary = "s", .Arguments = { IntegerArg("count", "").WithRange(1, 100000) } };
	const Result<Json> tooMany = info.ParseArguments(J(R"({"count": 200000})"));
	REQUIRE_FALSE(tooMany.HasValue());
	CHECK(tooMany.GetError().Message == "argument 'count': 200000 is above the maximum 100000");
}
