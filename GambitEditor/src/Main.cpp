#include "Gambit/Core/EntryPoint.h"
#include "Gambit/Core/Version.h"

namespace Gambit
{
	Scope<Application> CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "GambitEditor";
		specification.Window.Title = "Gambit Editor";
		specification.Window.Width = 1600;
		specification.Window.Height = 900;

		if (const Result<void> parsed = ApplyStandardArguments(args, specification); !parsed)
		{
			GB_ERROR("{}", parsed.GetError().Message);
			GB_ERROR("Usage: GambitEditor [--headless] [--frames N] [--hz N]");
			return nullptr;
		}

		GB_INFO("GambitEditor {}", VersionString);
		return CreateScope<Application>(std::move(specification));
	}
}
