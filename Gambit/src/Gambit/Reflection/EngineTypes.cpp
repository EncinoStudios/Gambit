#include "gbpch.h"

#include "Gambit/Asset/Components/AssetComponents.h"
#include "Gambit/ECS/Components/CoreComponents.h"
#include "Gambit/Project/ProjectSettings.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Renderer/RendererTypes.h"

namespace Gambit
{
	// The one place that knows every module with reflected types (D-022): each module contributes one registration
	// function, called here in a fixed order.
	void RegisterEngineTypes(TypeRegistry& registry)
	{
		RegisterCoreComponents(registry);
		RegisterAssetComponents(registry);
		RegisterProjectTypes(registry);
		RegisterRendererTypes(registry);
	}
}
