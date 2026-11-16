#include "gbpch.h"

#include "Gambit/Scripting/ApiRegistry.h"
#include "Gambit/Scripting/Bindings/CoreBindings.h"

namespace Gambit
{
	// The one place that knows every module with a Lua API (D-041), like RegisterEngineTypes for reflection: each
	// module contributes one registration function, called here in a fixed order. Binders run in this order, so the
	// core math types come first.
	void RegisterEngineScriptApi(ApiRegistry& registry)
	{
		Bindings::RegisterSandboxApi(registry);
		Bindings::RegisterMathApi(registry);
		Bindings::RegisterDeclarationApi(registry);
		Bindings::RegisterComponentApi(registry);
		Bindings::RegisterEntityApi(registry);
		Bindings::RegisterWorldApi(registry);
		Bindings::RegisterLibraryApi(registry);
	}
}
