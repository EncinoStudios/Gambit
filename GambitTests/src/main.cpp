#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/Log.h"

#include <cstdlib>
#include <string_view>

namespace
{
	bool IsEnvironmentFlagSet(const char* name)
	{
		const char* value = std::getenv(name);
		return value != nullptr && *value != '\0' && std::string_view(value) != "0";
	}
}

int main(int argc, char** argv)
{
	Gambit::Log::Init(Gambit::Tests::MakeTestLogConfig());

	doctest::Context context;
	context.applyCommandLine(argc, argv);

	// GPU tests live in the "gpu" test suite (AGENTS.md section 4); GB_TEST_NO_GPU=1 skips them (D-005).
	if (IsEnvironmentFlagSet("GB_TEST_NO_GPU"))
	{
		context.addFilter("test-suite-exclude", "gpu");
	}

	const int result = context.run();
	Gambit::Log::Shutdown();
	return result;
}
