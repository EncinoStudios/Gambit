#pragma once

// Include this from exactly one source file of an executable (its Main.cpp): it defines main() in the Hazel way. The
// executable provides CreateApplication, which builds the Application from the command line and pushes its layers.

#include "Gambit/Core/Application.h"
#include "Gambit/Core/Log.h"

namespace Gambit
{
	// Defined by the executable. Returns null, after logging the reason, when the application cannot be created (for
	// example a malformed command line); main() then exits with code 2.
	Scope<Application> CreateApplication(ApplicationCommandLineArgs args);
}

int main(int argc, char** argv)
{
	Gambit::Log::Init();
	int exitCode = 2;
	{
		const Gambit::Scope<Gambit::Application> application =
			Gambit::CreateApplication(Gambit::ApplicationCommandLineArgs(argc, argv));
		if (application != nullptr)
		{
			exitCode = application->Run();
		}
	}
	Gambit::Log::Shutdown();
	return exitCode;
}
