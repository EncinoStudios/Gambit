#include "Gambit/Core/EntryPoint.h"
#include "Gambit/Core/Version.h"

namespace Gambit
{
	Scope<Application> CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "GambitRuntime";
		specification.Window.Title = "Gambit";

		if (const Result<void> parsed = ApplyStandardArguments(args, specification); !parsed)
		{
			GB_ERROR("{}", parsed.GetError().Message);
			GB_ERROR("Usage: GambitRuntime [--headless] [--frames N] [--hz N]");
			return nullptr;
		}

		GB_INFO("GambitRuntime {}", VersionString);
		return CreateScope<Application>(std::move(specification));
	}
}
