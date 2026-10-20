#include "gbpch.h"

#include "Gambit/Automation/BuiltinCommands.h"

namespace Gambit
{
	void RegisterBuiltinCommands(CommandRegistry& registry)
	{
		Commands::RegisterProjectCommands(registry);
		Commands::RegisterSceneCommands(registry);
		Commands::RegisterEntityCommands(registry);
		Commands::RegisterComponentCommands(registry);
		Commands::RegisterEditCommands(registry);
		Commands::RegisterAssetCommands(registry);
		Commands::RegisterDataCommands(registry);
		Commands::RegisterBulkCommands(registry);
		Commands::RegisterSimulationCommands(registry);
		Commands::RegisterCommentCommands(registry);
	}
}
