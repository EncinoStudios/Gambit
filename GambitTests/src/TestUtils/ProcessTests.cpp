#include <doctest/doctest.h>

#include "TestUtils/Process.h"

#include <filesystem>

using namespace Gambit::Tests;

TEST_CASE("TestUtils: RunProcess reports a program that cannot start")
{
	const ProcessResult result = RunProcess(std::filesystem::path("definitely") / "not" / "a" / "program", {});
	CHECK_FALSE(result.Started);
	CHECK_FALSE(result.StartError.empty());
}
