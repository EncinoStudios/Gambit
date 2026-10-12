#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Asset/AssetType.h"
#include "Gambit/Asset/Components/AssetComponents.h"
#include "Gambit/Asset/DataTable.h"
#include "Gambit/Serialization/EntityData.h"

#include <string>

using namespace Gambit;
using namespace Gambit::Tests;

TEST_CASE("Asset: every asset handle field of the engine's types names a known asset type")
{
	const TypeRegistry& registry = TypeRegistry::Get();
	size_t assetFields = 0;
	const auto check = [&](std::string_view owner, const FieldInfo& field) {
		const FieldType valueType = field.Type == FieldType::Array ? field.ElementType : field.Type;
		if (valueType == FieldType::AssetHandle)
		{
			++assetFields;
			CHECK_MESSAGE(
				ParseAssetType(field.Attributes.AssetType).has_value(),
				owner << "." << field.Name << " accepts '" << field.Attributes.AssetType << "'");
		}
	};
	for (const ComponentInfo* component : registry.GetComponents())
	{
		for (const FieldInfo& field : component->GetFields())
		{
			check(component->GetName(), field);
		}
	}
	for (const StructInfo* info : registry.GetStructs())
	{
		for (const FieldInfo& field : info->GetFields())
		{
			check(info->GetName(), field);
		}
	}
	CHECK(assetFields >= 3); // PrefabInstance.Prefab, DataRef.Table, Rendering.Environment
}

TEST_CASE("Asset: the engine registers the asset components")
{
	const TypeRegistry& registry = TypeRegistry::Get();
	const ComponentInfo& instance = registry.GetComponent<PrefabInstanceComponent>();
	CHECK(instance.GetName() == "PrefabInstance");
	CHECK(instance.FindField("Prefab")->Type == FieldType::AssetHandle);
	CHECK(instance.FindField("Prefab")->Attributes.AssetType == "Prefab");
	CHECK(instance.FindField("SourceEntity")->Type == FieldType::UUID);
	CHECK(instance.FindField("SourceEntity")->HasFlag(FieldFlags::ReadOnly));
	CHECK(instance.HasFlag(ComponentFlags::Serializable));

	const ComponentInfo& ref = registry.GetComponent<DataRefComponent>();
	CHECK(ref.GetName() == "DataRef");
	CHECK(ref.FindField("Table")->Attributes.AssetType == "DataTable");
	CHECK(ref.FindField("RowId")->Type == FieldType::String);
}

TEST_CASE("Asset: a data reference is serialized with the table handle and the row id")
{
	TestWorld fixture;
	Entity entity = fixture.Scene->CreateEntityWithUUID(UUID(1), "Gun");
	auto& ref = entity.AddComponent<DataRefComponent>();
	ref.Table = UUID(0xDA7A);
	ref.RowId = "pistol";
	const std::vector<EntityData> data = CaptureWorld(*fixture.Scene);
	const Result<Json> json = EntitiesToJson(fixture.Registry, data);
	REQUIRE(json.HasValue());
	CHECK((*json)[0]["Components"]["DataRef"] == Json::parse(R"({"Table": "0x000000000000DA7A", "RowId": "pistol"})"));
	CHECK(CollectAssetReferences(fixture.Registry, data) == std::vector<AssetHandle>{ UUID(0xDA7A) });

	TestWorld target;
	LoadReport report;
	REQUIRE(SpawnEntities(*target.Scene, *EntitiesFromJson(target.Registry, *json, "", report)).HasValue());
	CHECK(target.Scene->ComputeStateHash() == fixture.Scene->ComputeStateHash());
}

TEST_CASE("Asset: a data reference finds its row through the asset manager")
{
	TempDirectory directory;
	TestWorld fixture;
	directory.WriteFile("Data/Weapons.csv", "Id,Damage:float\npistol,12\nrifle,30\n");
	AssetManager assets(directory.GetVFSRef(), { "Data" }, fixture.Registry);
	assets.Scan();

	Entity entity = fixture.Scene->CreateEntity("Gun");
	auto& ref = entity.AddComponent<DataRefComponent>();
	ref.Table = assets.GetHandle("Data/Weapons.csv");
	ref.RowId = "rifle";

	const Ref<DataTable> table = assets.GetAsset<DataTable>(ref.Table);
	REQUIRE(table != nullptr);
	CHECK(*table->GetValue(ref.RowId, "Damage") == FieldValue(30.0));
	CHECK_FALSE(table->GetValue("missing", "Damage").HasValue());
}
