#pragma once

#include "Gambit/Automation/CommandRegistry.h"

namespace Gambit
{
	// Registers every command the engine ships with (D-051); each group lives in Automation/Commands/:
	//   project.new/info
	//   scene.new/open/save/close/list/set-active
	//   entity.create/destroy/rename/reparent/duplicate/list/get/find
	//   component.add/remove/get/set/types
	//   selection.get/set, undo, redo
	//   asset.list/info/import, prefab.create/instantiate
	//   data.tables/get/set/add-row/remove-row
	//   bulk.get/set/export-csv/write-csv/import-csv
	//   sim.run, log.read
	//   comment.add/reply/resolve/reopen/list/get/delete
	// Registering twice into the same registry is a programmer error (verified).
	void RegisterBuiltinCommands(CommandRegistry& registry);

	namespace Commands
	{
		void RegisterProjectCommands(CommandRegistry& registry);
		void RegisterSceneCommands(CommandRegistry& registry);
		void RegisterEntityCommands(CommandRegistry& registry);
		void RegisterComponentCommands(CommandRegistry& registry);
		void RegisterEditCommands(CommandRegistry& registry);
		void RegisterAssetCommands(CommandRegistry& registry);
		void RegisterDataCommands(CommandRegistry& registry);
		void RegisterBulkCommands(CommandRegistry& registry);
		void RegisterSimulationCommands(CommandRegistry& registry);
		void RegisterCommentCommands(CommandRegistry& registry);
	}
}
