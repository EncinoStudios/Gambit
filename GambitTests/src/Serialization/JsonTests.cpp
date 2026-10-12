#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Serialization: JSON parses strict documents and reports where malformed ones break")
{
	const Result<Json> parsed = ParseJson(R"({"b": 1, "a": [true, null, "x"]})");
	REQUIRE(parsed.HasValue());
	CHECK((*parsed)["a"].size() == 3);

	// Keys keep the order of the text: ordered documents are what makes output stable.
	std::vector<std::string> keys;
	for (const auto& [key, member] : parsed->items())
	{
		keys.push_back(key);
	}
	CHECK(keys == std::vector<std::string>{ "b", "a" });

	CHECK(ParseJson("\xEF\xBB\xBF{}").HasValue()); // a byte order mark is dropped

	const Result<Json> broken = ParseJson("{\n  \"a\": 1,\n}", "Scenes/Main.gscene");
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.starts_with("Scenes/Main.gscene: "));
	CHECK(broken.GetError().Message.find("line 3") != std::string::npos);
	CHECK(broken.GetError().Message.find("json.exception") == std::string::npos); // no library prefix

	CHECK_FALSE(ParseJson("").HasValue());
	CHECK_FALSE(ParseJson("{} trailing").HasValue());
	CHECK_FALSE(ParseJson("// comment\n{}").HasValue()); // no comments in strict JSON
	CHECK_FALSE(ParseJson("{\"a\": 1,}").HasValue());
	CHECK_FALSE(ParseJson("{\"a\": \"\xC3\x28\"}").HasValue()); // invalid UTF-8
}

TEST_CASE("Serialization: JSON nested too deeply is refused instead of overflowing the stack")
{
	const std::string fine = std::string(MaxJsonDepth, '[') + std::string(MaxJsonDepth, ']');
	CHECK(ParseJson(fine).HasValue());
	const std::string tooDeep = std::string(MaxJsonDepth + 1, '[') + std::string(MaxJsonDepth + 1, ']');
	const Result<Json> refused = ParseJson(tooDeep, "deep.json");
	REQUIRE_FALSE(refused.HasValue());
	CHECK(refused.GetError().Message.find("deeper") != std::string::npos);

	// Brackets inside strings do not count.
	std::string inString = "[\"";
	inString += std::string(1000, '[');
	inString += "\"]";
	CHECK(ParseJson(inString).HasValue());
	CHECK(ParseJson("[\"\\\"[[[[\"]").HasValue()); // an escaped quote does not end the string
	const std::string hostile = std::string(100000, '[');
	CHECK_FALSE(ParseJson(hostile).HasValue());
}

TEST_CASE("Serialization: JSON is written canonically")
{
	Json document = Json::object();
	document["Zeta"] = 1;
	document["Alpha"] = Json::array({ 1.5, "text", Json::object() });
	document["Empty"] = Json::array();
	document["Text"] = "caf\xC3\xA9"; // non-ASCII stays UTF-8
	const Result<std::string> text = DumpJson(document);
	REQUIRE(text.HasValue());
	CHECK(
		*text
		== "{\n  \"Zeta\": 1,\n  \"Alpha\": [\n    1.5,\n    \"text\",\n    {}\n  ],\n  \"Empty\": [],\n  \"Text\": "
		   "\"caf\xC3\xA9\"\n}\n");

	// Reading it back gives the same document.
	const Result<Json> again = ParseJson(*text);
	REQUIRE(again.HasValue());
	CHECK(*again == document);
	CHECK(*DumpJson(*again) == *text);
}

TEST_CASE("Serialization: writing text that is not UTF-8 is an error, not an exception")
{
	Json document = Json::object();
	document["Name"] = std::string("bad \xFF byte");
	const Result<std::string> text = DumpJson(document);
	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Message.find("UTF-8") != std::string::npos);
}

TEST_CASE("Serialization: JSON pointers escape keys and indices")
{
	CHECK(JsonPathAppend("", "Entities") == "/Entities");
	CHECK(JsonPathAppend("/Entities", 3) == "/Entities/3");
	CHECK(JsonPathAppend("/Entities/3", "Components") == "/Entities/3/Components");
	CHECK(JsonPathAppend("", "a/b") == "/a~1b");
	CHECK(JsonPathAppend("", "a~b") == "/a~0b");
	CHECK(MakePathError("/a/b", "bad").Message == "/a/b: bad");
	CHECK(MakePathError("", "bad").Message == "bad");
	CHECK(AddSource("file.json", Error("bad", 7)).Message == "file.json: bad");
	CHECK(AddSource("file.json", Error("bad", 7)).Code == 7);
	CHECK(AddSource("", Error("bad")).Message == "bad");
}

TEST_CASE("Serialization: a load report keeps its first warnings and counts the rest")
{
	LoadReport report;
	CHECK(report.IsEmpty());
	for (size_t index = 0; index < LoadReport::MaxStored + 5; ++index)
	{
		report.Warn(JsonPathAppend("", index), "unknown field");
	}
	CHECK_FALSE(report.IsEmpty());
	CHECK(report.GetWarnings().size() == LoadReport::MaxStored);
	CHECK(report.GetSuppressedCount() == 5);
	CHECK(report.GetWarnings()[0].Path == "/0");
	CHECK(report.GetWarnings()[0].Message == "unknown field");

	ScopedLogCapture capture;
	report.Log("Scenes/Main.gscene");
	CHECK(capture.Contains(LogLevel::Warn, "Scenes/Main.gscene: /0: unknown field"));
	CHECK(capture.Contains(LogLevel::Warn, "5 more warnings"));
}

TEST_CASE("Serialization: migrations upgrade old documents step by step")
{
	MigrationTable table;
	table.Add(1, [](Json& document) -> Result<void> {
		document["Title"] = document["Name"]; // version 1 called it Name
		document.erase("Name");
		return {};
	});
	table.Add(2, [](Json& document) -> Result<void> {
		document["Scale"] = 2.0 * document["Scale"].get<double>();
		return {};
	});

	Json old = Json::parse(R"({"Version": 1, "Name": "Crate", "Scale": 1.5})");
	REQUIRE(table.Upgrade(old, 3, "thing").HasValue());
	CHECK(old["Version"] == 3);
	CHECK(old["Title"] == "Crate");
	CHECK_FALSE(old.contains("Name"));
	CHECK(old["Scale"] == 3.0);

	Json middle = Json::parse(R"({"Version": 2, "Title": "Crate", "Scale": 1.0})");
	REQUIRE(table.Upgrade(middle, 3, "thing").HasValue());
	CHECK(middle["Scale"] == 2.0);

	Json current = Json::parse(R"({"Version": 3, "Title": "Crate", "Scale": 1.0})");
	REQUIRE(table.Upgrade(current, 3, "thing").HasValue());
	CHECK(current["Scale"] == 1.0); // nothing to do
}

TEST_CASE("Serialization: migrations reject documents they cannot upgrade")
{
	MigrationTable table;
	table.Add(1, [](Json&) -> Result<void> { return Error("no luck"); });
	const auto upgradeError = [&](const char* text, uint32_t current) {
		Json document = Json::parse(text);
		const Result<void> result = table.Upgrade(document, current, "thing");
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};

	CHECK(upgradeError("[]", 1).find("must be a JSON object") != std::string::npos);
	CHECK(upgradeError("{}", 1).find("no \"Version\"") != std::string::npos);
	CHECK(upgradeError(R"({"Version": "1"})", 1).find("positive integer") != std::string::npos);
	CHECK(upgradeError(R"({"Version": 0})", 1).find("positive integer") != std::string::npos);
	CHECK(upgradeError(R"({"Version": 1.5})", 1).find("positive integer") != std::string::npos);
	CHECK(upgradeError(R"({"Version": 5})", 2).find("newer Gambit") != std::string::npos);
	CHECK(upgradeError(R"({"Version": 2})", 3).find("no migration to version 3") != std::string::npos);
	const std::string failed = upgradeError(R"({"Version": 1})", 2);
	CHECK(failed.find("version 1 to 2 failed") != std::string::npos);
	CHECK(failed.find("no luck") != std::string::npos);

	// A step that meets a document it did not expect fails the upgrade instead of throwing out of it.
	MigrationTable throwing;
	throwing.Add(1, [](Json& document) -> Result<void> {
		document["Scale"] = 2.0 * document.at("Scale").get<double>();
		return {};
	});
	for (const char* text : { R"({"Version": 1})", R"({"Version": 1, "Scale": "big"})" })
	{
		Json document = Json::parse(text);
		const Result<void> result = throwing.Upgrade(document, 2, "thing");
		REQUIRE_FALSE(result.HasValue());
		CHECK(result.GetError().Message.find("from version 1 to 2 failed") != std::string::npos);
	}

	Json untouched = Json::parse(R"({"Version": 1})");
	CHECK(GetNoMigrations().Upgrade(untouched, 1, "thing").HasValue());
	CHECK(
		GetNoMigrations().Upgrade(untouched, 2, "thing").GetError().Message.find("no migration") != std::string::npos);
}
