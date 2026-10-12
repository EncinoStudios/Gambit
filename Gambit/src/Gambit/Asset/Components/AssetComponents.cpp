#include "gbpch.h"

#include "Gambit/Asset/Components/AssetComponents.h"

#include "Gambit/Reflection/TypeRegistry.h"

namespace Gambit
{
	void RegisterAssetComponents(TypeRegistry& registry)
	{
		registry.RegisterComponent<PrefabInstanceComponent>("PrefabInstance")
			.Field("Prefab", &PrefabInstanceComponent::Prefab, Attr::AssetType{ "Prefab" })
			.Field("SourceEntity", &PrefabInstanceComponent::SourceEntity, Attr::PlainUUID{}, Attr::ReadOnly{});

		registry.RegisterComponent<DataRefComponent>("DataRef")
			.Field("Table", &DataRefComponent::Table, Attr::AssetType{ "DataTable" })
			.Field(
				"RowId", &DataRefComponent::RowId, Attr::Tooltip{ "The id of the row, the first column of the table" });
	}
}
