#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/Core/Log.h"
#include "Gambit/RHI/ShaderLibrary.h"

#include "GambitTestsEmbeddedShaders.h"

#include <cstdio>

int main(int argc, char** argv)
{
	Gambit::Log::Init(Gambit::Tests::MakeTestLogConfig());

	// The test shaders (GambitTests/Shaders) join the engine's in the ShaderLibrary.
	if (const Gambit::Result<void> registered =
	        Gambit::ShaderLibrary::Register(Gambit::EmbeddedShaders::GetGambitTestsShaders());
	    !registered)
	{
		std::fprintf(stderr, "cannot register the test shaders: %s\n", registered.GetError().Message.c_str());
		return 1;
	}

	doctest::Context context;
	context.applyCommandLine(argc, argv);

	// GPU tests live in the "gpu" test suite (AGENTS.md section 4); GB_TEST_NO_GPU=1 skips them (D-005).
	if (Gambit::Tests::IsEnvironmentFlagSet("GB_TEST_NO_GPU"))
	{
		context.addFilter("test-suite-exclude", "gpu");
	}

	int result = context.run();

	// The shared GPU test device goes before the log: tearing it down reports leaked GPU objects as errors.
	if (const uint64_t errors = Gambit::Tests::ShutdownSharedGraphicsDevice(); errors != 0)
	{
		std::fprintf(
			stderr, "%llu error(s) were logged while the GPU test device was destroyed (see above)\n",
			static_cast<unsigned long long>(errors));
		if (result == 0)
		{
			result = 1;
		}
	}
	Gambit::Log::Shutdown();
	return result;
}
