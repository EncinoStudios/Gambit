#include "gbpch.h"

#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	// The one place that knows every module with reflected types (D-022): each module contributes one registration
	// function, called here in a fixed order.
	void RegisterEngineTypes(TypeRegistry& registry)
	{
		RegisterCoreComponents(registry);
	}
}
