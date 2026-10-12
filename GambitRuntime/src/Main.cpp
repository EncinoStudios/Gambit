#include "Gambit/Core/Log.h"
#include "Gambit/Core/Version.h"

int main()
{
	using namespace Gambit;

	Log::Init();
	GB_INFO("GambitRuntime {}", VersionString);
	Log::Shutdown();
	return 0;
}
