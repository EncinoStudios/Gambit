#include <doctest/doctest.h>

#include "AutomationFixture.h"
#include "TestUtils/TempDirectory.h"

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	CommandInfo Simple(std::string name, CommandFlags flags = CommandFlags::None)
	{
		return CommandInfo{ .Name = std::move(name), .Summary = "A test command", .Flags = flags };
	}

	const CommandHandler Succeeding = [](EditorContext&, const Json&) { return CommandResult::Success(); };
}

TEST_CASE("Automation: commands are registered, found and listed by prefix")
{
	CommandRegistry registry;
	REQUIRE(registry.Register(Simple("entity.create"), Succeeding).HasValue());
	REQUIRE(registry.Register(Simple("entity.destroy"), Succeeding).HasValue());
	REQUIRE(registry.Register(Simple("component.set"), Succeeding).HasValue());
	REQUIRE(registry.Register(Simple("undo"), Succeeding).HasValue());

	CHECK(registry.GetCount() == 4);
	REQUIRE(registry.Find("entity.create") != nullptr);
	CHECK(registry.Find("entity.create")->Summary == "A test command");
	CHECK(registry.Find("entity") == nullptr);

	const auto names = [](const std::vector<const CommandInfo*>& commands) {
		std::vector<std::string> result;
		for (const CommandInfo* info : commands)
		{
			result.push_back(info->Name);
		}
		return result;
	};
	CHECK(
		names(registry.List())
		== std::vector<std::string>{ "component.set", "entity.create", "entity.destroy", "undo" });
	CHECK(names(registry.List("entity.")) == std::vector<std::string>{ "entity.create", "entity.destroy" });
	CHECK(names(registry.List("ent")) == std::vector<std::string>{ "entity.create", "entity.destroy" });
	CHECK(names(registry.List("nothing")).empty());

	const Json all = registry.ToJson();
	REQUIRE(all.is_array());
	CHECK(all.size() == 4);
	CHECK(all[0].at("name") == "component.set");
	CHECK(registry.ToJson("undo").size() == 1);
}

TEST_CASE("Automation: registering rejects invalid and duplicate commands")
{
	CommandRegistry registry;
	REQUIRE(registry.Register(Simple("a.b"), Succeeding).HasValue());
	CHECK(registry.Register(Simple("a.b"), Succeeding).GetError().Message == "command 'a.b' is already registered");
	CHECK(registry.Register(Simple("a.c"), CommandHandler{}).GetError().Message == "command 'a.c' has no handler");
	CHECK_FALSE(registry.Register(Simple("Bad Name"), Succeeding).HasValue());
	CHECK(registry.GetCount() == 1);
}

TEST_CASE("Automation: unknown commands get suggestions")
{
	AutomationFixture fixture;
	const CommandResult result = fixture.Run("entity.creat");
	CHECK_FALSE(result.Ok);
	CHECK(result.Code == CommandErrorCode::UnknownCommand);
	CHECK(result.Message.starts_with("unknown command 'entity.creat'; did you mean 'entity.create'"));
	CHECK(fixture.Run("nothing.like.it").Message == "unknown command 'nothing.like.it'");
}

TEST_CASE("Automation: arguments are validated before the handler runs")
{
	CommandRegistry registry;
	int calls = 0;
	REQUIRE(
		registry
			.Register(
				CommandInfo{ .Name = "t.count",
	                         .Summary = "s",
	                         .Arguments = { IntegerArg("n", "").Require(), BooleanArg("flag", "").WithDefault(true) } },
				[&calls](EditorContext&, const Json& arguments) {
					++calls;
					return CommandResult::Success(arguments);
				})
			.HasValue());
	EditorContext context;

	const CommandResult bad = registry.Execute(context, "t.count", J(R"({"n": "three"})"));
	CHECK_FALSE(bad.Ok);
	CHECK(bad.Code == CommandErrorCode::InvalidArguments);
	CHECK(bad.Message == "argument 'n': expected an integer, found a string");
	CHECK(calls == 0);

	const CommandResult good = registry.Execute(context, "t.count", J(R"({"n": 3})"));
	REQUIRE(good.Ok);
	CHECK(good.Data == J(R"({"n": 3, "flag": true})")); // the handler sees the defaults
	CHECK(calls == 1);
}

TEST_CASE("Automation: a handler that throws does not escape the registry")
{
	CommandRegistry registry;
	REQUIRE(registry
	            .Register(
					Simple("t.throws"),
					[](EditorContext&, const Json&) -> CommandResult { throw std::runtime_error("boom"); })
	            .HasValue());
	REQUIRE(registry.Register(Simple("t.throws-int"), [](EditorContext&, const Json&) -> CommandResult { throw 42; })
	            .HasValue());
	EditorContext context;
	ScopedLogCapture log;

	const CommandResult result = registry.Execute(context, "t.throws");
	CHECK_FALSE(result.Ok);
	CHECK(result.Code == CommandErrorCode::Internal);
	CHECK(result.Message == "'t.throws' failed unexpectedly: boom");
	CHECK(log.Contains(LogLevel::Error, "threw an exception: boom"));

	const CommandResult other = registry.Execute(context, "t.throws-int");
	CHECK(other.Code == CommandErrorCode::Internal);
}

TEST_CASE("Automation: preconditions are checked before the handler runs")
{
	CommandRegistry registry;
	int calls = 0;
	const CommandHandler counting = [&calls](EditorContext&, const Json&) {
		++calls;
		return CommandResult::Success();
	};
	REQUIRE(registry.Register(Simple("t.project", CommandFlags::RequiresProject), counting).HasValue());
	REQUIRE(registry.Register(Simple("t.scene", CommandFlags::RequiresScene), counting).HasValue());
	REQUIRE(registry.Register(Simple("t.editor", CommandFlags::EditorOnly), counting).HasValue());
	REQUIRE(registry.Register(Simple("t.writes", CommandFlags::WritesFiles), counting).HasValue());

	EditorContext context;
	CHECK(registry.Execute(context, "t.project").Code == CommandErrorCode::NoProject);
	CHECK(registry.Execute(context, "t.scene").Code == CommandErrorCode::NoProject);
	CHECK(registry.Execute(context, "t.project").Message.find("needs an open project") != std::string::npos);

	const CommandResult editorOnly = registry.Execute(context, "t.editor");
	CHECK(editorOnly.Code == CommandErrorCode::Unsupported);
	CHECK(editorOnly.Message == "'t.editor' needs the running editor; this host is headless");
	context.SetHost(HostKind::Editor);
	CHECK(registry.Execute(context, "t.editor").Ok);

	CHECK(registry.Execute(context, "t.writes").Ok);
	context.SetDryRun(true);
	const CommandResult dryRun = registry.Execute(context, "t.writes");
	CHECK(dryRun.Code == CommandErrorCode::Unsupported);
	CHECK(dryRun.Message.find("dry run") != std::string::npos);
	CHECK(calls == 2);

	AutomationFixture fixture;
	fixture.CreateProject();
	REQUIRE(registry.Execute(fixture.Context, "t.project").Ok);
	REQUIRE(registry.Execute(fixture.Context, "t.scene").Ok); // project.new opened the start scene
	REQUIRE(fixture.Context.CloseScene("Scenes/Main.gscene", true).HasValue());
	const CommandResult noScene = registry.Execute(fixture.Context, "t.scene");
	CHECK(noScene.Code == CommandErrorCode::NoScene);
	CHECK(noScene.Message.find("needs an active scene") != std::string::npos);
}

TEST_CASE("Automation: a failing undoable command leaves the scene as it was")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string crate = fixture.Create("Crate");
	const uint64_t before = fixture.Hash();

	CommandRegistry registry;
	REQUIRE(
		registry
			.Register(
				CommandInfo{ .Name = "t.half",
	                         .Summary = "Renames, then fails",
	                         .Flags = CommandFlags::Mutating | CommandFlags::Undoable | CommandFlags::RequiresScene },
				[&fixture, &crate](EditorContext& context, const Json&) {
					// Two changes through the undo stack, then a failure.
					const CommandResult first = fixture.Commands.Execute(
						context, "entity.rename", J(R"({"entity": ")" + crate + R"(", "name": "Renamed"})"));
					CHECK(first.Ok);
					const CommandResult second =
						fixture.Commands.Execute(context, "entity.create", J(R"({"name": "Extra"})"));
					CHECK(second.Ok);
					return CommandResult::Failure(CommandErrorCode::Failed, "gave up");
				})
			.HasValue());

	const CommandResult result = registry.Execute(fixture.Context, "t.half");
	CHECK_FALSE(result.Ok);
	CHECK(result.Message == "gave up");
	CHECK(fixture.Hash() == before);
	CHECK(fixture.Scene().GetEntityCount() == 1);
	CHECK(fixture.Context.GetUndoStack().GetUndoDescription() == "Create entity 'Crate'");
	CHECK(fixture.Context.GetUndoStack().GetGroupDepth() == 0);
}
