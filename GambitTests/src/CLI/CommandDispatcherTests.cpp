#include <doctest/doctest.h>

#include "Cli/CommandDispatcher.h"

#include "Gambit/Core/Version.h"

#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace Gambit::Cli;

	struct Captured
	{
		int ExitCode = 0;
		std::string Out;
		std::string Err;
	};

	Captured Run(const CommandDispatcher& dispatcher, const std::vector<std::string>& args)
	{
		std::ostringstream out;
		std::ostringstream err;
		const CommandContext context{ out, err };
		Captured result;
		result.ExitCode = dispatcher.Run(args, context);
		result.Out = out.str();
		result.Err = err.str();
		return result;
	}

	const CommandHandler SucceedingHandler = [](const CommandContext&, std::span<const std::string>) {
		return ExitSuccess;
	};

	// A command with only the fields a test cares about.
	Command MakeCommand(
		std::string name, CommandHandler handler = SucceedingHandler, std::vector<std::string> aliases = {},
		std::string summary = "")
	{
		Command command;
		command.Name = std::move(name);
		command.Summary = std::move(summary);
		command.Aliases = std::move(aliases);
		command.Handler = std::move(handler);
		return command;
	}

	Command MakeEcho()
	{
		return Command{
			.Name = "echo",
			.Summary = "Print the arguments",
			.Usage = "<text>...",
			.Aliases = { "--echo" },
			.Handler =
				[](const CommandContext& context, std::span<const std::string> args) {
					for (const std::string& arg : args)
					{
						context.Out << arg << '\n';
					}
					return ExitSuccess;
				},
		};
	}
}

TEST_CASE("CLI: dispatches to the registered handler with the remaining arguments")
{
	CommandDispatcher dispatcher("tool");
	REQUIRE(dispatcher.Register(MakeEcho()).HasValue());

	const Captured result = Run(dispatcher, { "echo", "a", "b c" });
	CHECK(result.ExitCode == ExitSuccess);
	CHECK(result.Out == "a\nb c\n");
	CHECK(result.Err.empty());

	CHECK(Run(dispatcher, { "--echo", "x" }).Out == "x\n");
}

TEST_CASE("CLI: handler exit codes are returned unchanged")
{
	CommandDispatcher dispatcher("tool");
	const CommandHandler failing = [](const CommandContext&, std::span<const std::string>) { return 17; };
	REQUIRE(dispatcher.Register(MakeCommand("fail", failing, {}, "Always fails")).HasValue());
	CHECK(Run(dispatcher, { "fail" }).ExitCode == 17);
}

TEST_CASE("CLI: no arguments prints the command list to stderr and is a usage error")
{
	CommandDispatcher dispatcher("tool");
	REQUIRE(dispatcher.Register(MakeEcho()).HasValue());

	const Captured result = Run(dispatcher, {});
	CHECK(result.ExitCode == ExitUsage);
	CHECK(result.Out.empty());
	CHECK(result.Err.starts_with("Usage: tool <command> [options]\n"));
	CHECK(result.Err.find("echo") != std::string::npos);
}

TEST_CASE("CLI: unknown commands are a usage error that points to help")
{
	CommandDispatcher dispatcher("tool");

	const Captured result = Run(dispatcher, { "nope" });
	CHECK(result.ExitCode == ExitUsage);
	CHECK(result.Out.empty());
	CHECK(result.Err == "tool: unknown command 'nope'\nRun 'tool --help' for the list of commands.\n");
}

TEST_CASE("CLI: help lists commands sorted by name")
{
	CommandDispatcher dispatcher("tool");
	REQUIRE(dispatcher.Register(MakeEcho()).HasValue());
	REQUIRE(dispatcher.Register(MakeCommand("alpha", SucceedingHandler, {}, "First command")).HasValue());

	for (const char* spelling : { "help", "--help", "-h" })
	{
		const Captured result = Run(dispatcher, { spelling });
		INFO("spelling: ", spelling);
		CHECK(result.ExitCode == ExitSuccess);
		CHECK(result.Err.empty());
		const size_t alpha = result.Out.find("  alpha  First command\n");
		const size_t echo = result.Out.find("  echo   Print the arguments\n");
		const size_t help = result.Out.find("  help   Show help for tool");
		REQUIRE(alpha != std::string::npos);
		REQUIRE(echo != std::string::npos);
		REQUIRE(help != std::string::npos);
		CHECK(alpha < echo);
		CHECK(echo < help);
	}
}

TEST_CASE("CLI: help <command> and <command> --help show the usage of one command")
{
	CommandDispatcher dispatcher("tool");
	REQUIRE(dispatcher.Register(MakeEcho()).HasValue());

	const std::string expected = "Usage: tool echo <text>...\n\nPrint the arguments\n\nAliases: --echo\n";
	const Captured viaHelp = Run(dispatcher, { "help", "echo" });
	CHECK(viaHelp.ExitCode == ExitSuccess);
	CHECK(viaHelp.Out == expected);

	const Captured viaFlag = Run(dispatcher, { "echo", "--help" });
	CHECK(viaFlag.ExitCode == ExitSuccess);
	CHECK(viaFlag.Out == expected);
	CHECK(Run(dispatcher, { "echo", "-h" }).Out == expected);

	// help also resolves aliases.
	CHECK(Run(dispatcher, { "help", "--echo" }).Out == expected);
}

TEST_CASE("CLI: help rejects unknown commands and extra arguments")
{
	CommandDispatcher dispatcher("tool");

	const Captured unknown = Run(dispatcher, { "help", "nope" });
	CHECK(unknown.ExitCode == ExitUsage);
	CHECK(unknown.Err == "tool: unknown command 'nope'\n");

	const Captured tooMany = Run(dispatcher, { "help", "a", "b" });
	CHECK(tooMany.ExitCode == ExitUsage);
	CHECK(tooMany.Out.empty());
}

TEST_CASE("CLI: registration validates names, handlers and duplicates")
{
	CommandDispatcher dispatcher("tool");

	CHECK_FALSE(dispatcher.Register(MakeCommand("")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("Upper")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("has space")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("1digit")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("--flag")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("nohandler", CommandHandler{})).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("badalias", SucceedingHandler, { "" })).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("badalias", SucceedingHandler, { "a b" })).HasValue());

	REQUIRE(dispatcher.Register(MakeCommand("good-name2", SucceedingHandler, { "-g" })).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("good-name2")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("other", SucceedingHandler, { "-g" })).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("other", SucceedingHandler, { "help" })).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("help")).HasValue());
	CHECK_FALSE(dispatcher.Register(MakeCommand("self", SucceedingHandler, { "self" })).HasValue());

	const Gambit::Result<void> duplicate = dispatcher.Register(MakeCommand("good-name2"));
	REQUIRE_FALSE(duplicate.HasValue());
	CHECK(duplicate.GetError().Message == "command 'good-name2' is already registered");

	// A rejected registration must not leave anything behind.
	CHECK(dispatcher.Find("other") == nullptr);
	CHECK(dispatcher.Find("badalias") == nullptr);
	CHECK(dispatcher.Find("self") == nullptr);
}

TEST_CASE("CLI: Find resolves names and aliases")
{
	CommandDispatcher dispatcher("tool");
	REQUIRE(dispatcher.Register(MakeEcho()).HasValue());

	REQUIRE(dispatcher.Find("echo") != nullptr);
	CHECK(dispatcher.Find("echo") == dispatcher.Find("--echo"));
	CHECK(dispatcher.Find("ech") == nullptr);
	CHECK(dispatcher.Find("") == nullptr);
	CHECK(dispatcher.GetProgramName() == "tool");
}

TEST_CASE("CLI: the built-in version command prints the engine version")
{
	CommandDispatcher dispatcher("gambit");
	RegisterBuiltinCommands(dispatcher);

	const std::string expected = std::string("gambit ") + std::string(Gambit::VersionString) + "\n";
	for (const char* spelling : { "version", "--version", "-V" })
	{
		const Captured result = Run(dispatcher, { spelling });
		INFO("spelling: ", spelling);
		CHECK(result.ExitCode == ExitSuccess);
		CHECK(result.Out == expected);
		CHECK(result.Err.empty());
	}

	const Captured extra = Run(dispatcher, { "version", "extra" });
	CHECK(extra.ExitCode == ExitUsage);
	CHECK(extra.Out.empty());
	CHECK(extra.Err == "gambit: 'version' takes no arguments\n");

	const Captured help = Run(dispatcher, { "--help" });
	CHECK(help.ExitCode == ExitSuccess);
	CHECK(help.Out.starts_with("Usage: gambit <command> [options]\n"));
	CHECK(help.Out.find("version") != std::string::npos);
}
