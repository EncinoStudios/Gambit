#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include <chrono>
#include <fstream>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// A project with a scene (an entity "Crate" with Health), a texture and a script.
	struct Workshop
	{
		AutomationFixture Fixture;
		std::string Crate;
		std::string Texture;

		Workshop()
		{
			Fixture.CreateProject();
			Crate = Fixture.Create("Crate", J(R"({"components": {"Health": {"Current": 5}}})"));
			Fixture.Create("Barrel");
			const std::filesystem::path source = Fixture.Directory.GetPath() / "Logo.png";
			std::ofstream(source, std::ios::binary) << "not really a png";
			Texture = Fixture.Succeed("asset.import", Json{ { "source", source.generic_string() } }).at("handle");
			Fixture.Context.SetAuthorName("tester");
			Fixture.Context.GetComments().SetClock(
				[] { return std::chrono::system_clock::time_point(std::chrono::seconds(1'773'480'413)); });
		}

		std::string Add(const Json& arguments) { return Fixture.Succeed("comment.add", arguments).at("id"); }
	};
}

TEST_CASE("Automation: comment.add starts a thread about every kind of target")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;

	const Json entity = fixture.Succeed(
		"comment.add", J(R"({"kind": "Entity", "entity": "Crate", "text": "Should this be breakable?"})"));
	CHECK(entity.at("id").get<std::string>().size() == 18);
	CHECK(entity.at("status") == "Open");
	CHECK(entity.at("author") == "tester"); // the author of the session
	CHECK(entity.at("created") == "2026-03-14T09:26:53Z");
	CHECK(
		entity.at("target")
		== Json{ { "kind", "Entity" }, { "scene", "Scenes/Main.gscene" }, { "entity", shop.Crate } });
	REQUIRE(entity.at("messages").size() == 1);
	CHECK(
		entity.at("messages")[0]
		== Json{
			{ "author", "tester" }, { "text", "Should this be breakable?" }, { "timestamp", "2026-03-14T09:26:53Z" } });
	const std::filesystem::path file =
		fixture.GetProjectDirectory() / "Comments" / (entity.at("id").get<std::string>() + ".json");
	CHECK(Json::parse(ReadTextFile(file)).at("Target").at("Kind") == "Entity");

	const Json component = fixture.Succeed(
		"comment.add",
		J(R"({"kind": "Component", "entity": "Crate", "component": "Health", "text": "Max is too low", "author": "claude"})"));
	CHECK(component.at("author") == "claude");
	CHECK(
		component.at("target")
		== Json{ { "kind", "Component" },
	             { "scene", "Scenes/Main.gscene" },
	             { "entity", shop.Crate },
	             { "component", "Health" } });

	const Json asset = fixture.Succeed(
		"comment.add", J(R"({"kind": "Asset", "asset": "Assets/Logo.png", "text": "Needs a higher resolution"})"));
	CHECK(
		asset.at("target") == Json{ { "kind", "Asset" }, { "asset", "Assets/Logo.png" }, { "handle", shop.Texture } });
	const Json byHandle = fixture.Succeed(
		"comment.add", Json{ { "kind", "Asset" }, { "asset", shop.Texture }, { "text", "Same asset, by handle" } });
	CHECK(byHandle.at("target") == asset.at("target"));

	const Json script = fixture.Succeed(
		"comment.add", J(R"({"kind": "File", "file": "./Scripts//Player.lua", "line": 12, "text": "Off by one?"})"));
	CHECK(script.at("target") == Json{ { "kind", "File" }, { "file", "Scripts/Player.lua" }, { "line", 12 } });
	const Json wholeFile =
		fixture.Succeed("comment.add", J(R"({"kind": "File", "file": "Scripts/Player.lua", "text": "Split this up"})"));
	CHECK_FALSE(wholeFile.at("target").contains("line"));

	const Json feature = fixture.Succeed(
		"comment.add", J(R"({"kind": "Feature", "feature": "inventory", "text": "Open question: stack sizes"})"));
	CHECK(feature.at("target") == Json{ { "kind", "Feature" }, { "feature", "inventory" } });

	const Json position = fixture.Succeed(
		"comment.add", J(R"({"kind": "Position", "position": [1, 2.5, -3], "text": "Put the door here"})"));
	CHECK(
		position.at("target")
		== Json{ { "kind", "Position" }, { "scene", "Scenes/Main.gscene" }, { "position", J("[1.0, 2.5, -3.0]") } });

	// Every thread is its own file in the Comments folder.
	size_t files = 0;
	for (const auto& item : std::filesystem::directory_iterator(fixture.GetProjectDirectory() / "Comments"))
	{
		files += item.path().extension() == ".json" ? 1 : 0;
	}
	CHECK(files == 8);
	CHECK(fixture.Context.GetComments().GetCount() == 8);
}

TEST_CASE("Automation: comment.add checks the target and the text")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;
	const auto reject = [&](std::string_view json, CommandErrorCode code = CommandErrorCode::InvalidArguments) {
		return fixture.Reject("comment.add", J(json), code).Message;
	};

	CHECK(reject(R"({"kind": "Entity", "text": "x"})").find("Entity comments need 'entity'") != std::string::npos);
	CHECK(
		reject(R"({"kind": "Entity", "entity": "Nobody", "text": "x"})", CommandErrorCode::NotFound)
			.find("no entity named 'Nobody'")
		!= std::string::npos);
	CHECK(
		reject(
			R"({"kind": "Entity", "entity": "Crate", "scenePath": "Scenes/Other.gscene", "text": "x"})",
			CommandErrorCode::NoScene)
			.find("'Scenes/Other.gscene' is not open")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Component", "entity": "Crate", "text": "x"})").find("Component comments need 'component'")
		!= std::string::npos);
	CHECK(
		reject(
			R"({"kind": "Component", "entity": "Crate", "component": "Healt", "text": "x"})",
			CommandErrorCode::NotFound)
			.find("did you mean 'Health'?")
		!= std::string::npos);
	CHECK(reject(R"({"kind": "Asset", "text": "x"})").find("Asset comments need 'asset'") != std::string::npos);
	CHECK(
		reject(R"({"kind": "Asset", "asset": "Assets/Missing.png", "text": "x"})", CommandErrorCode::NotFound)
			.find("there is no asset")
		!= std::string::npos);
	CHECK(reject(R"({"kind": "File", "text": "x"})").find("File comments need 'file'") != std::string::npos);
	CHECK(
		reject(R"({"kind": "File", "file": "../Secrets.txt", "text": "x"})").find("not a valid project path")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "File", "file": "/etc/passwd", "text": "x"})").find("not a path inside the project")
		!= std::string::npos);
	CHECK(reject(R"({"kind": "Feature", "text": "x"})").find("Feature comments need 'feature'") != std::string::npos);
	CHECK(
		reject(R"({"kind": "Feature", "feature": "", "text": "x"})").find("a feature tag is 1 to 64 characters")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Position", "text": "x"})").find("Position comments need 'position'") != std::string::npos);
	CHECK(
		reject(R"({"kind": "Position", "position": [1, 2], "text": "x"})").find("three numbers") != std::string::npos);
	CHECK(
		reject(R"({"kind": "Position", "position": ["a", 2, 3], "text": "x"})").find("expected a number")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Feature", "feature": "ui", "text": ""})").find("a comment must be 1 to 16384 bytes")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Feature", "feature": "ui", "text": "x", "author": ""})").find("the author must be")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Wish", "feature": "ui", "text": "x"})")
			.find("not one of: Entity, Component, Asset, File, Feature, Position")
		!= std::string::npos);
	CHECK(
		reject(R"({"kind": "Feature", "feature": "ui"})").find("missing required argument 'text'")
		!= std::string::npos);
	CHECK(fixture.Context.GetComments().GetCount() == 0);

	AutomationFixture bare;
	bare.Reject("comment.add", J(R"({"kind": "Feature", "feature": "ui", "text": "x"})"), CommandErrorCode::NoProject);
}

TEST_CASE("Automation: threads are answered, resolved, reopened and deleted")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;
	const std::string id = shop.Add(J(R"({"kind": "Feature", "feature": "combat", "text": "How much damage?"})"));
	const std::filesystem::path file = fixture.GetProjectDirectory() / "Comments" / (id + ".json");

	const Json reply =
		fixture.Succeed("comment.reply", Json{ { "id", id }, { "text", "Try 12." }, { "author", "claude" } });
	REQUIRE(reply.at("messages").size() == 2);
	CHECK(reply.at("messages")[1].at("author") == "claude");
	CHECK(reply.at("status") == "Open");

	const Json resolved = fixture.Succeed("comment.resolve", Json{ { "id", id }, { "text", "Done." } });
	CHECK(resolved.at("status") == "Resolved");
	CHECK(resolved.at("messages").size() == 3);
	CHECK(Json::parse(ReadTextFile(file)).at("Status") == "Resolved");
	const Json silent = fixture.Succeed("comment.reopen", Json{ { "id", id } }); // without a message
	CHECK(silent.at("status") == "Open");
	CHECK(silent.at("messages").size() == 3);
	const Json again = fixture.Succeed("comment.resolve", Json{ { "id", id } });
	CHECK(again.at("messages").size() == 3);
	const Json why = fixture.Succeed("comment.reopen", Json{ { "id", id }, { "text", "Not done after all." } });
	CHECK(why.at("messages").size() == 4);

	const Json got = fixture.Succeed("comment.get", Json{ { "id", id } });
	CHECK(got.at("messages").size() == 4);
	CHECK(got.at("messageCount") == 4);

	const Json deleted = fixture.Succeed("comment.delete", Json{ { "id", id } });
	CHECK(deleted.at("deleted") == id);
	CHECK_FALSE(std::filesystem::exists(file));
	for (const char* command :
	     { "comment.get", "comment.reply", "comment.resolve", "comment.reopen", "comment.delete" })
	{
		Json arguments{ { "id", id } };
		if (std::string_view(command) == "comment.reply")
		{
			arguments["text"] = "x";
		}
		const CommandResult missing = fixture.Run(command, arguments);
		INFO(command);
		CHECK_FALSE(missing.Ok);
		CHECK(missing.Code == CommandErrorCode::NotFound);
		CHECK(missing.Message.find("there is no comment thread") != std::string::npos);
	}
	fixture.Reject("comment.reply", Json{ { "id", "0x1" }, { "text", "" } }, CommandErrorCode::NotFound);
	const std::string other = shop.Add(J(R"({"kind": "Feature", "feature": "ui", "text": "x"})"));
	fixture.Reject("comment.reply", Json{ { "id", other }, { "text", "" } }, CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: comment.list filters the threads, oldest first")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;
	int seconds = 0;
	fixture.Context.GetComments().SetClock(
		[&] { return std::chrono::system_clock::time_point(std::chrono::seconds(1'773'480'413 + seconds++)); });

	const std::string onCrate = shop.Add(J(R"({"kind": "Entity", "entity": "Crate", "text": "one"})"));
	const std::string onHealth = shop.Add(
		J(R"({"kind": "Component", "entity": "Crate", "component": "Health", "text": "two", "author": "claude"})"));
	const std::string onBarrel = shop.Add(J(R"({"kind": "Entity", "entity": "Barrel", "text": "three"})"));
	const std::string onAsset = shop.Add(J(R"({"kind": "Asset", "asset": "Assets/Logo.png", "text": "four"})"));
	const std::string onFile = shop.Add(J(R"({"kind": "File", "file": "Scripts/Player.lua", "text": "five"})"));
	const std::string onFeature = shop.Add(J(R"({"kind": "Feature", "feature": "inventory", "text": "six"})"));
	fixture.Succeed("comment.resolve", Json{ { "id", onBarrel } });

	const auto ids = [&](const std::string& json) {
		const Json listed = fixture.Succeed("comment.list", J(json));
		std::vector<std::string> found;
		for (const Json& thread : listed.at("threads"))
		{
			found.push_back(thread.at("id"));
		}
		return found;
	};
	using Ids = std::vector<std::string>;
	CHECK(ids("{}") == Ids{ onCrate, onHealth, onBarrel, onAsset, onFile, onFeature });
	CHECK(ids(R"({"status": "Resolved"})") == Ids{ onBarrel });
	CHECK(ids(R"({"status": "Open", "kind": "Entity"})") == Ids{ onCrate });
	CHECK(ids(R"({"author": "claude"})") == Ids{ onHealth });
	CHECK(ids(R"({"entity": "Crate"})") == Ids{ onCrate, onHealth });
	CHECK(ids(R"({"entity": ")" + shop.Crate + R"("})") == Ids{ onCrate, onHealth });
	CHECK(ids(R"({"entity": "Crate", "component": "Health"})") == Ids{ onHealth });
	CHECK(ids(R"({"scenePath": "./Scenes/Main.gscene"})") == Ids{ onCrate, onHealth, onBarrel });
	CHECK(ids(R"({"asset": "Assets//Logo.png"})") == Ids{ onAsset });
	CHECK(ids(R"({"file": "Scripts/Player.lua"})") == Ids{ onFile });
	CHECK(ids(R"({"feature": "inventory"})") == Ids{ onFeature });
	CHECK(ids(R"({"feature": "nothing"})").empty());

	const Json limited = fixture.Succeed("comment.list", J(R"({"limit": 2})"));
	CHECK(limited.at("count") == 2);
	CHECK(limited.at("total") == 6);
	CHECK(limited.at("threads")[0].at("text") == "one"); // the opening message
	CHECK(limited.at("threads")[0].at("messageCount") == 1);

	fixture.Reject("comment.list", J(R"({"entity": "Nobody"})"), CommandErrorCode::NotFound);
	fixture.Reject("comment.list", J(R"({"file": "../x"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject("comment.list", J(R"({"status": "Closed"})"), CommandErrorCode::InvalidArguments);
	fixture.Succeed("scene.close", J(R"({"discard": true})"));
	fixture.Reject("comment.list", J(R"({"entity": "Crate"})"), CommandErrorCode::NoScene);
	CHECK(ids(R"({"entity": ")" + shop.Crate + R"("})") == Ids{ onCrate, onHealth }); // a UUID needs no scene
}

TEST_CASE("Automation: comments survive reopening the project and stay out of the asset registry")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;
	const std::string id = shop.Add(J(R"({"kind": "Entity", "entity": "Crate", "text": "Remember me"})"));
	fixture.Succeed("comment.reply", Json{ { "id", id }, { "text", "Still here" } });
	fixture.Succeed("scene.save");

	EditorContext second(fixture.Types);
	REQUIRE(second.OpenProject(fixture.GetProjectDirectory()).HasValue());
	const CommentThread* thread = second.GetComments().Find(id);
	REQUIRE(thread != nullptr);
	CHECK(thread->Messages.size() == 2);
	CHECK(thread->Target.Entity == *UUID::Parse(shop.Crate));
	const Json info = fixture.Succeed("project.info");
	CHECK(info.at("commentCount") == 1);

	// Comments are project data, not content: no asset, no .meta file.
	CHECK(fixture.Succeed("asset.list", J(R"({"path": "Comments"})")).at("total") == 0);
	for (const auto& item : std::filesystem::directory_iterator(fixture.GetProjectDirectory() / "Comments"))
	{
		CHECK(item.path().extension() == ".json");
	}
}

TEST_CASE("Automation: a dry run keeps comments in memory")
{
	Workshop shop;
	AutomationFixture& fixture = shop.Fixture;
	fixture.Context.SetDryRun(true);
	const std::string id = shop.Add(J(R"({"kind": "Feature", "feature": "ui", "text": "What if"})"));
	CHECK_FALSE(std::filesystem::exists(fixture.GetProjectDirectory() / "Comments" / (id + ".json")));
	CHECK(fixture.Succeed("comment.list").at("total") == 1);
	fixture.Succeed("comment.resolve", Json{ { "id", id } });
	fixture.Succeed("comment.delete", Json{ { "id", id } });
	CHECK(fixture.Succeed("comment.list").at("total") == 0);

	fixture.Context.SetDryRun(false);
	const std::string written = shop.Add(J(R"({"kind": "Feature", "feature": "ui", "text": "For real"})"));
	CHECK(std::filesystem::exists(fixture.GetProjectDirectory() / "Comments" / (written + ".json")));
}
