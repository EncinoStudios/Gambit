#include "gbpch.h"

#include "Gambit/Asset/BuiltinImporters.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Asset/Prefab.h"
#include "Gambit/Asset/SceneAsset.h"

namespace Gambit
{
	Result<Ref<Asset>> SceneImporter::Load(const AssetLoadContext& context) const
	{
		Result<SceneData> scene =
			SceneSerializer::Read(context.Types, context.Vfs, context.Record.Path, context.Report);
		if (!scene)
		{
			return scene.GetError();
		}
		return Ref<Asset>(CreateRef<SceneAsset>(context.Types, std::move(*scene)));
	}

	Result<Ref<Asset>> PrefabImporter::Load(const AssetLoadContext& context) const
	{
		Result<std::string> text = context.Vfs.ReadText(context.Record.Path);
		if (!text)
		{
			return AddSource(context.Record.Path, text.GetError());
		}
		Result<Ref<Prefab>> prefab = PrefabUtils::Parse(context.Types, *text, context.Record.Path, context.Report);
		if (!prefab)
		{
			return prefab.GetError();
		}
		return Ref<Asset>(std::move(*prefab));
	}

	Result<Ref<Asset>> DataTableImporter::Load(const AssetLoadContext& context) const
	{
		Result<Ref<DataTable>> table = DataTable::Load(context.Vfs, context.Record.Path);
		if (!table)
		{
			return table.GetError();
		}
		return Ref<Asset>(std::move(*table));
	}
}
