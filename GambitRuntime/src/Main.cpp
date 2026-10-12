#include "Gambit/Core/EntryPoint.h"
#include "Gambit/Core/Version.h"

#include "RuntimeLayer.h"

namespace Gambit
{
	Scope<Application> CreateApplication(ApplicationCommandLineArgs args)
	{
		ApplicationSpecification specification;
		specification.Name = "GambitRuntime";
		specification.Window.Title = "Gambit";
		specification.EnableGraphics = true;

		if (const Result<void> parsed = ApplyStandardArguments(args, specification); !parsed)
		{
			GB_ERROR("{}", parsed.GetError().Message);
			GB_ERROR("Usage: GambitRuntime [--headless] [--frames N] [--hz N] [--no-gpu]");
			return nullptr;
		}

		GB_INFO("GambitRuntime {}", VersionString);
		Scope<Application> application = CreateScope<Application>(std::move(specification));
		application->PushLayer(CreateScope<RuntimeLayer>());
		return application;
	}
}
