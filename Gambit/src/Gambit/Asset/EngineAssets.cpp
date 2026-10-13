#include "gbpch.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Asset/BuiltinImporters.h"
#include "Gambit/Renderer/RendererTypes.h"

namespace Gambit
{
	// The one place that knows every module with asset importers or built-in assets (like RegisterEngineTypes for
	// reflection): each module contributes one registration function, called here in a fixed order.
	void RegisterEngineAssets(AssetManager& manager)
	{
		manager.RegisterImporter(CreateScope<SceneImporter>());
		manager.RegisterImporter(CreateScope<PrefabImporter>());
		manager.RegisterImporter(CreateScope<DataTableImporter>());
		RegisterRendererAssets(manager);
	}
}
