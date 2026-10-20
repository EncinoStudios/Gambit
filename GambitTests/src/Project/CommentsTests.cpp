#include <doctest/doctest.h>

#include "TestUtils/TempDirectory.h"

#include "Gambit/Project/Comments.h"

#include <chrono>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	using Clock = std::chrono::system_clock;

	// A clock that moves one minute on every reading, starting at 2026-03-14T09:26:53Z.
	CommentStore::Clock SteppingClock()
	{
		auto now = CreateRef<Clock::time_point>(Clock::time_point(std::chrono::seconds(1773480413)));
		return [now] {
			const Clock::time_point current = *now;
			*now += std::chrono::minutes(1);
			return current;
		};
	}

	CommentTarget EntityTarget(uint64_t entity = 0x10)
	{
		CommentTarget target;
		target.Kind = CommentTargetKind::Entity;
		target.Scene = "Scenes/Main.gscene";
		target.Entity = UUID(entity);
		return target;
	}

	CommentTarget FeatureTarget(std::string feature)
	{
		CommentTarget target;
		target.Kind = CommentTargetKind::Feature;
		target.Feature = std::move(feature);
		return target;
	}

	struct Fixture
	{
		TempDirectory Directory;
		CommentStore Store{ Directory.GetVFS(), "Comments" };

		Fixture() { Store.SetClock(SteppingClock()); }

		CommentThread Add(CommentTarget target, std::string_view text = "Looks odd", std::string_view author = "claude")
		{
			Result<CommentThread> thread = Store.Add(std::move(target), author, text);
			REQUIRE_MESSAGE(thread.HasValue(), (thread.HasValue() ? std::string() : thread.GetError().Message));
			return *thread;
		}

		std::vector<std::string> Ids(const CommentQuery& query = {}) const
		{
			std::vector<std::string> ids;
			for (const CommentThread* thread : Store.Query(query))
			{
				ids.push_back(thread->Id);
			}
			return ids;
		}
	};
}

TEST_CASE("Project: comments: a thread is stored as one JSON file per thread")
{
	Fixture fixture;
	const CommentThread thread = fixture.Add(EntityTarget(0xABC), "The crate is too big");

	CHECK(thread.Id.size() == 18);
	CHECK(thread.Id.starts_with("0x"));
	CHECK(thread.Author == "claude");
	CHECK(thread.Status == CommentStatus::Open);
	CHECK(thread.Created == "2026-03-14T09:26:53Z");
	CHECK(thread.Updated == thread.Created);
	REQUIRE(thread.Messages.size() == 1);
	CHECK(thread.Messages[0] == CommentMessage{ "claude", "The crate is too big", "2026-03-14T09:26:53Z" });

	const std::string path = "Comments/" + thread.Id + ".json";
	REQUIRE(fixture.Directory.Exists(path));
	const Json file = Json::parse(fixture.Directory.ReadFile(path));
	CHECK(file == Json::parse(R"({
		"Version": 1,
		"Id": ")" + thread.Id + R"(",
		"Target": {"Kind": "Entity", "Scene": "Scenes/Main.gscene", "Entity": "0x0000000000000ABC"},
		"Author": "claude",
		"Status": "Open",
		"Created": "2026-03-14T09:26:53Z",
		"Updated": "2026-03-14T09:26:53Z",
		"Messages": [{"Author": "claude", "Text": "The crate is too big", "Timestamp": "2026-03-14T09:26:53Z"}]
	})"));
	CHECK(fixture.Directory.ReadFile(path).ends_with("}\n")); // canonical text: indented, trailing newline
	CHECK(fixture.Store.GetCount() == 1);
	CHECK(fixture.Store.GetThreadPath(thread.Id) == path);
	REQUIRE(fixture.Store.Find(thread.Id) != nullptr);
	CHECK(fixture.Store.Find("0xnope") == nullptr);
}

TEST_CASE("Project: comments: replies, resolving and reopening add messages and move the update time")
{
	Fixture fixture;
	const CommentThread thread = fixture.Add(FeatureTarget("inventory"), "Needs a design");

	Result<CommentThread> reply = fixture.Store.Reply(thread.Id, "alex", "Agreed, see the doc");
	REQUIRE(reply.HasValue());
	CHECK(reply->Messages.size() == 2);
	CHECK(reply->Messages[1].Author == "alex");
	CHECK(reply->Updated == "2026-03-14T09:27:53Z");
	CHECK(reply->Created == thread.Created);
	CHECK(reply->Status == CommentStatus::Open);

	Result<CommentThread> resolved = fixture.Store.Resolve(thread.Id, "claude", "Done in commit abc");
	REQUIRE(resolved.HasValue());
	CHECK(resolved->Status == CommentStatus::Resolved);
	CHECK(resolved->Messages.size() == 3);
	CHECK(resolved->Updated == "2026-03-14T09:28:53Z");

	Result<CommentThread> silent = fixture.Store.Reopen(thread.Id, "alex"); // no message
	REQUIRE(silent.HasValue());
	CHECK(silent->Status == CommentStatus::Open);
	CHECK(silent->Messages.size() == 3);
	CHECK(silent->Updated == "2026-03-14T09:29:53Z");

	CHECK(fixture.Store.Resolve(thread.Id, "claude").HasValue()); // closing needs no text either
	CHECK(fixture.Store.Find(thread.Id)->Status == CommentStatus::Resolved);

	CHECK(
		fixture.Store.Reply("0x1234567812345678", "a", "text").GetError().Message
		== "there is no comment thread '0x1234567812345678'");
	CHECK_FALSE(fixture.Store.Reply(thread.Id, "alex", "").HasValue()); // a reply needs text
	CHECK_FALSE(fixture.Store.Reply(thread.Id, "", "text").HasValue());
	CHECK(fixture.Store.Find(thread.Id)->Messages.size() == 3);
}

TEST_CASE("Project: comments: threads survive a reload")
{
	Fixture fixture;
	CommentTarget position;
	position.Kind = CommentTargetKind::Position;
	position.Scene = "Scenes/Main.gscene";
	position.Position = glm::vec3(0.1f, -2.5f, 1000.25f);
	CommentTarget asset;
	asset.Kind = CommentTargetKind::Asset;
	asset.Asset = "Assets/Crate.png";
	asset.AssetHandle = UUID(0xFEED);
	CommentTarget file;
	file.Kind = CommentTargetKind::File;
	file.File = "Scripts/Player.lua";
	file.Line = 42;
	CommentTarget component = EntityTarget();
	component.Kind = CommentTargetKind::Component;
	component.Component = "Health";

	std::vector<CommentThread> written;
	for (CommentTarget target : { EntityTarget(), component, asset, file, FeatureTarget("audio"), position })
	{
		written.push_back(fixture.Add(std::move(target), "A note with \"quotes\" and caf\xC3\xA9"));
	}
	REQUIRE(fixture.Store.Reply(written[0].Id, "alex", "second").HasValue());
	REQUIRE(fixture.Store.Resolve(written[3].Id, "alex", "fixed").HasValue());

	CommentStore reloaded(fixture.Directory.GetVFS(), "Comments");
	LoadReport report;
	REQUIRE(reloaded.Load(report).HasValue());
	CHECK(report.IsEmpty());
	CHECK(reloaded.GetCount() == 6);
	for (const CommentThread& thread : written)
	{
		const CommentThread* found = reloaded.Find(thread.Id);
		REQUIRE(found != nullptr);
		const CommentThread* original = fixture.Store.Find(thread.Id);
		CHECK(found->Target == original->Target);
		CHECK(found->Author == original->Author);
		CHECK(found->Status == original->Status);
		CHECK(found->Created == original->Created);
		CHECK(found->Updated == original->Updated);
		CHECK(found->Messages == original->Messages);
	}
	CHECK(reloaded.Find(written[5].Id)->Target.Position == glm::vec3(0.1f, -2.5f, 1000.25f)); // exact floats
	CHECK(reloaded.Find(written[3].Id)->Target.Line == 42);
	// Loading again replaces the threads.
	REQUIRE(reloaded.Load(report).HasValue());
	CHECK(reloaded.GetCount() == 6);

	// A project without comments has no folder yet.
	TempDirectory empty;
	CommentStore none(empty.GetVFS(), "Comments");
	REQUIRE(none.Load(report).HasValue());
	CHECK(none.GetCount() == 0);
}

TEST_CASE("Project: comments: threads are queried by target, status and author, oldest first")
{
	Fixture fixture;
	const CommentThread first = fixture.Add(EntityTarget(0x10), "one", "claude");
	const CommentThread second = fixture.Add(EntityTarget(0x20), "two", "alex");
	const CommentThread third = fixture.Add(FeatureTarget("audio"), "three", "claude");
	CommentTarget component = EntityTarget(0x10);
	component.Kind = CommentTargetKind::Component;
	component.Component = "Health";
	const CommentThread fourth = fixture.Add(component, "four", "claude");
	CommentTarget file;
	file.Kind = CommentTargetKind::File;
	file.File = "Scripts/A.lua";
	file.Line = 3;
	const CommentThread fifth = fixture.Add(file, "five", "alex");
	CommentTarget asset;
	asset.Kind = CommentTargetKind::Asset;
	asset.Asset = "Assets/A.png";
	asset.AssetHandle = UUID(0x99);
	const CommentThread sixth = fixture.Add(asset, "six", "claude");
	REQUIRE(fixture.Store.Resolve(second.Id, "alex").HasValue());

	CHECK(fixture.Ids() == std::vector<std::string>{ first.Id, second.Id, third.Id, fourth.Id, fifth.Id, sixth.Id });

	CommentQuery query;
	query.Status = CommentStatus::Open;
	CHECK(fixture.Ids(query) == std::vector<std::string>{ first.Id, third.Id, fourth.Id, fifth.Id, sixth.Id });
	query.Status = CommentStatus::Resolved;
	CHECK(fixture.Ids(query) == std::vector<std::string>{ second.Id });

	query = {};
	query.Author = "alex";
	CHECK(fixture.Ids(query) == std::vector<std::string>{ second.Id, fifth.Id });
	query = {};
	query.Kind = CommentTargetKind::Entity;
	CHECK(fixture.Ids(query) == std::vector<std::string>{ first.Id, second.Id });
	query = {};
	query.Entity = UUID(0x10);
	CHECK(fixture.Ids(query) == std::vector<std::string>{ first.Id, fourth.Id }); // the entity and its component
	query.Component = "Health";
	CHECK(fixture.Ids(query) == std::vector<std::string>{ fourth.Id });
	query = {};
	query.Scene = "Scenes/Main.gscene";
	CHECK(fixture.Ids(query).size() == 3);
	query = {};
	query.Feature = "audio";
	CHECK(fixture.Ids(query) == std::vector<std::string>{ third.Id });
	query = {};
	query.File = "Scripts/A.lua";
	CHECK(fixture.Ids(query) == std::vector<std::string>{ fifth.Id });
	query = {};
	query.Asset = "Assets/A.png";
	CHECK(fixture.Ids(query) == std::vector<std::string>{ sixth.Id });
	query = {};
	query.AssetHandle = UUID(0x99);
	CHECK(fixture.Ids(query) == std::vector<std::string>{ sixth.Id });
	query = {};
	query.Author = "nobody";
	CHECK(fixture.Ids(query).empty());
}

TEST_CASE("Project: comments: deleting a thread removes its file")
{
	Fixture fixture;
	const CommentThread keep = fixture.Add(FeatureTarget("keep"));
	const CommentThread doomed = fixture.Add(FeatureTarget("doomed"));
	REQUIRE(fixture.Directory.Exists("Comments/" + doomed.Id + ".json"));

	REQUIRE(fixture.Store.Delete(doomed.Id).HasValue());
	CHECK_FALSE(fixture.Directory.Exists("Comments/" + doomed.Id + ".json"));
	CHECK(fixture.Store.Find(doomed.Id) == nullptr);
	CHECK(fixture.Store.GetCount() == 1);
	CHECK(fixture.Directory.Exists("Comments/" + keep.Id + ".json"));
	CHECK_FALSE(fixture.Store.Delete(doomed.Id).HasValue());
}

TEST_CASE("Project: comments: invalid threads are refused")
{
	Fixture fixture;
	const auto refuses = [&](CommentTarget target, std::string_view author, std::string_view text) {
		const Result<CommentThread> result = fixture.Store.Add(std::move(target), author, text);
		CHECK_FALSE(result.HasValue());
		return result.HasValue() ? std::string() : result.GetError().Message;
	};

	CHECK(refuses(FeatureTarget("x"), "", "text") == "the author must be 1 to 128 characters of printable UTF-8 text");
	CHECK(refuses(FeatureTarget("x"), "a\nb", "text").find("author") != std::string::npos);
	CHECK(refuses(FeatureTarget("x"), "claude", "") == "a comment must be 1 to 16384 bytes of valid UTF-8 text");
	CHECK(refuses(FeatureTarget("x"), "claude", "caf\xE9").find("valid UTF-8") != std::string::npos);
	CHECK(
		refuses(FeatureTarget("x"), "claude", std::string(CommentStore::MaxTextLength + 1, 'x')).find("16384")
		!= std::string::npos);
	CHECK(refuses(FeatureTarget(""), "claude", "text") == "a feature tag is 1 to 64 characters of printable text");
	CHECK(refuses(FeatureTarget(std::string(65, 'x')), "claude", "text").find("feature tag") != std::string::npos);

	CommentTarget target = EntityTarget();
	target.Scene = "";
	CHECK(refuses(target, "claude", "text") == "Entity comments need the scene");
	target.Scene = "../Outside.gscene";
	CHECK(refuses(target, "claude", "text").find("not a normalized project-relative path") != std::string::npos);
	target.Scene = "/abs/Main.gscene";
	CHECK(refuses(target, "claude", "text").find("not a normalized project-relative path") != std::string::npos);
	target = EntityTarget(0);
	CHECK(refuses(target, "claude", "text") == "Entity comments need the entity");
	target = EntityTarget();
	target.Kind = CommentTargetKind::Component;
	CHECK(refuses(target, "claude", "text") == "Component comments need the name of a component");
	target.Component = "not a name";
	CHECK_FALSE(fixture.Store.Add(target, "claude", "text").HasValue());

	CommentTarget asset;
	asset.Kind = CommentTargetKind::Asset;
	CHECK(refuses(asset, "claude", "text") == "Asset comments need the asset path or handle");
	CommentTarget file;
	file.Kind = CommentTargetKind::File;
	CHECK(refuses(file, "claude", "text") == "File comments need the file");
	CommentTarget position;
	position.Kind = CommentTargetKind::Position;
	position.Scene = "Scenes/Main.gscene";
	position.Position.x = std::numeric_limits<float>::infinity();
	CHECK(refuses(position, "claude", "text") == "the position of a Position comment must be finite");

	CHECK(fixture.Store.GetCount() == 0);
	CHECK_FALSE(fixture.Directory.Exists("Comments"));
}

TEST_CASE("Project: comments: files that are not valid threads are reported and left alone")
{
	Fixture fixture;
	const CommentThread good = fixture.Add(FeatureTarget("good"));
	const std::string goodText = fixture.Directory.ReadFile("Comments/" + good.Id + ".json");

	fixture.Directory.WriteFile("Comments/notes.txt", "not a thread");
	fixture.Directory.WriteFile("Comments/0x0000000000000001.json", "{ broken");
	fixture.Directory.WriteFile("Comments/0x0000000000000002.json", R"({"Version": 1, "Id": "0x0000000000000002"})");
	// The id inside must be the file name.
	std::string misnamed = goodText;
	misnamed.replace(misnamed.find(good.Id), good.Id.size(), "0x00000000000000AA");
	fixture.Directory.WriteFile("Comments/" + good.Id.substr(0, 16) + "FF.json", misnamed);
	// An unknown member is a warning, the thread still loads.
	std::string extra = goodText;
	extra.replace(extra.find(good.Id), good.Id.size(), "0x00000000000000BB");
	extra.insert(extra.find("\"Author\""), "\"Mood\": \"calm\",\n  ");
	fixture.Directory.WriteFile("Comments/0x00000000000000BB.json", extra);
	// A file from a newer engine.
	std::string newer = goodText;
	newer.replace(newer.find(good.Id), good.Id.size(), "0x00000000000000CC");
	newer.replace(newer.find("\"Version\": 1"), 12, "\"Version\": 99");
	fixture.Directory.WriteFile("Comments/0x00000000000000CC.json", newer);

	CommentStore reloaded(fixture.Directory.GetVFS(), "Comments");
	LoadReport report;
	REQUIRE(reloaded.Load(report).HasValue());
	CHECK(reloaded.GetCount() == 2); // the good one and the one with the unknown member
	CHECK(reloaded.Find(good.Id) != nullptr);
	CHECK(reloaded.Find("0x00000000000000BB") != nullptr);
	CHECK(report.GetWarnings().size() == 5);
	std::string all;
	for (const Diagnostic& warning : report.GetWarnings())
	{
		all += warning.Path + ": " + warning.Message + "\n";
	}
	CHECK(all.find("Comments/0x0000000000000001.json") != std::string::npos);
	CHECK(all.find("needs a \"Target\" and at least one message") != std::string::npos);
	CHECK(all.find("does not match the thread id") != std::string::npos);
	CHECK(all.find("/Mood: unknown member, ignored") != std::string::npos);
	CHECK(all.find("version 99") != std::string::npos);
	CHECK(fixture.Directory.ReadFile("Comments/0x0000000000000001.json") == "{ broken"); // untouched
}

TEST_CASE("Project: comments: a store without writes changes only memory")
{
	Fixture fixture;
	const CommentThread existing = fixture.Add(FeatureTarget("on disk"));
	fixture.Store.SetWritesEnabled(false);

	const CommentThread preview = fixture.Add(FeatureTarget("preview"));
	REQUIRE(fixture.Store.Reply(existing.Id, "alex", "a reply").HasValue());
	CHECK(fixture.Store.GetCount() == 2);
	CHECK(fixture.Store.Find(existing.Id)->Messages.size() == 2);
	CHECK_FALSE(fixture.Directory.Exists("Comments/" + preview.Id + ".json"));
	CHECK(Json::parse(fixture.Directory.ReadFile("Comments/" + existing.Id + ".json")).at("Messages").size() == 1);

	REQUIRE(fixture.Store.Delete(existing.Id).HasValue());
	CHECK(fixture.Directory.Exists("Comments/" + existing.Id + ".json")); // still there

	fixture.Store.SetWritesEnabled(true);
	REQUIRE(fixture.Store.Reply(preview.Id, "alex", "now it is written").HasValue());
	CHECK(fixture.Directory.Exists("Comments/" + preview.Id + ".json"));
}

TEST_CASE("Project: comments: names and timestamps")
{
	CHECK(ToString(CommentTargetKind::Position) == "Position");
	CHECK(ParseCommentTargetKind("Component") == CommentTargetKind::Component);
	CHECK_FALSE(ParseCommentTargetKind("component").has_value());
	CHECK(ParseCommentStatus("Resolved") == CommentStatus::Resolved);
	CHECK_FALSE(ParseCommentStatus("Closed").has_value());
	CHECK(ToString(CommentStatus::Open) == "Open");

	CHECK(FormatTimestampUtc(Clock::time_point(std::chrono::seconds(0))) == "1970-01-01T00:00:00Z");
	CHECK(FormatTimestampUtc(Clock::time_point(std::chrono::seconds(1773480413))) == "2026-03-14T09:26:53Z");
	CHECK(FormatTimestampUtc(Clock::time_point(std::chrono::milliseconds(1773480413999))) == "2026-03-14T09:26:53Z");
}
