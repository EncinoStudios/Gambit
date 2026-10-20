#include <doctest/doctest.h>

#include "Cli/CommandLine.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Cli;

namespace
{
	using Words = std::vector<std::string>;
}

TEST_CASE("CLI: global options are taken out wherever they stand")
{
	const Words line = { "--project", "Games/Tetris", "entity",         "create",
		                 "--json",    "--name",       "Crate",          "--scene=Scenes/A.gscene",
		                 "--dry-run", "--args",       "{\"index\": 1}", "--keep-going",
		                 "-h" };
	const Result<SplitCommandLine> split = SplitGlobalOptions(line);
	REQUIRE(split.HasValue());
	CHECK(split->Options.Project == "Games/Tetris");
	CHECK(split->Options.Scene == "Scenes/A.gscene");
	CHECK(split->Options.ArgsJson == "{\"index\": 1}");
	CHECK(split->Options.AsJson);
	CHECK(split->Options.DryRun);
	CHECK(split->Options.KeepGoing);
	CHECK(split->Options.Help);
	CHECK(split->Rest == Words{ "entity", "create", "--name", "Crate" });

	const Result<SplitCommandLine> plain = SplitGlobalOptions(Words{ "entity", "list" });
	REQUIRE(plain.HasValue());
	CHECK_FALSE(plain->Options.Project.has_value());
	CHECK_FALSE(plain->Options.AsJson);
	CHECK_FALSE(plain->Options.Help);
	CHECK(plain->Rest == Words{ "entity", "list" });

	const Result<SplitCommandLine> equals = SplitGlobalOptions(Words{ "--project=with=equals", "--args={}" });
	REQUIRE(equals.HasValue());
	CHECK(equals->Options.Project == "with=equals");
	CHECK(equals->Options.ArgsJson == "{}");

	for (const char* option : { "--project", "--scene", "--args" })
	{
		const Result<SplitCommandLine> missing = SplitGlobalOptions(Words{ "entity", "list", option });
		REQUIRE_FALSE(missing.HasValue());
		CHECK(missing.GetError().Message == std::string("option ") + option + " needs a value");
	}
}
