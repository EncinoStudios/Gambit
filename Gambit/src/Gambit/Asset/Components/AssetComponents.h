#pragma once

#include "Gambit/Core/UUID.h"

#include <string>

// The components of the asset module (D-026, D-027). Plain data only (D-020).

namespace Gambit
{
	class TypeRegistry;

	// Marks an entity that was created from a prefab (D-026): the prefab asset and the UUID the entity has inside that
	// prefab file, which tells which part of the prefab it is. Added by PrefabUtils::Instantiate to every entity of the
	// instance.
	struct PrefabInstanceComponent
	{
		UUID Prefab;       // the handle of the Prefab asset (null for prefabs that are not assets)
		UUID SourceEntity; // the entity's UUID in the prefab: an identifier, not a reference into the world
	};

	// Refers to a row of a data table (D-027); scripts read the row with Data.Get(table, row).
	struct DataRefComponent
	{
		UUID Table;        // the handle of the DataTable asset
		std::string RowId; // the id (first column) of the row
	};

	// Registers "PrefabInstance" and "DataRef".
	void RegisterAssetComponents(TypeRegistry& registry);
}
