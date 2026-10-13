#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestSupport.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetRegistry.h"
#include "Gambit/Asset/SubAssetHandles.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// A file asset with sub-assets: one "Part" per line of the file, keyed "Part/<line>".
	class PartAsset final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Mesh;
		explicit PartAsset(std::string name)
			: Name(std::move(name))
		{}
		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		std::string Name;
	};

	class BundleAsset final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Model;
		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		[[nodiscard]] std::vector<SubAssetRef> GetSubAssets() const override { return Parts; }
		std::vector<SubAssetRef> Parts;
	};

	class BundleImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Model; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override
		{
			Result<std::string> text = context.Vfs.ReadText(context.Record.Path);
			if (!text)
			{
				return text.GetError();
			}
			auto bundle = CreateRef<BundleAsset>();
			size_t start = 0;
			while (start < text->size())
			{
				const size_t end = std::min(text->find('\n', start), text->size());
				const std::string line = text->substr(start, end - start);
				start = end + 1;
				if (!line.empty())
				{
					const AssetHandle handle = context.SubAssets.Acquire("Part/" + line, AssetType::Mesh);
					bundle->Parts.push_back({ handle, CreateRef<PartAsset>(line) });
				}
			}
			return Ref<Asset>(std::move(bundle));
		}
	};

	struct BundleProject
	{
		TempDirectory Directory;
		TestWorld Types; // owns the TypeRegistry
		AssetManager Assets;

		BundleProject()
			: Assets(Directory.GetVFSRef(), { "Assets" }, Types.Registry)
		{
			Assets.RegisterImporter(CreateScope<BundleImporter>());
		}
	};

	std::vector<std::string> KeysOf(const std::vector<AssetRecord>& records)
	{
		std::vector<std::string> keys;
		for (const AssetRecord& record : records)
		{
			keys.push_back(record.SubAssetKey);
		}
		return keys;
	}
}

TEST_CASE("Asset: .meta files list sub-assets, which are checked when read")
{
	AssetMetadata metadata;
	metadata.Handle = UUID(0x10);
	metadata.Type = AssetType::Model;
	metadata.SubAssets = { { "Mesh/Body", AssetType::Mesh, UUID(0x11) },
		                   { "Material/#0", AssetType::Material, UUID(0x12) } };
	const Result<std::string> text = metadata.Serialize();
	REQUIRE(text.HasValue());
	CHECK(text->find(R"("Key": "Mesh/Body")") != std::string::npos);
	LoadReport report;
	const Result<AssetMetadata> parsed = AssetMetadata::Parse(*text, "A.glb.meta", report);
	REQUIRE(parsed.HasValue());
	CHECK(parsed->SubAssets == metadata.SubAssets);
	CHECK(report.IsEmpty());

	// Without sub-assets the member is left out.
	metadata.SubAssets.clear();
	CHECK(metadata.Serialize()->find("SubAssets") == std::string::npos);

	const auto failsWith = [](std::string_view subAssets, std::string_view expected) {
		const std::string text = std::format(
			R"({{ "Version": 1, "Handle": "0x0000000000000010", "Type": "Model", "SubAssets": {} }})", subAssets);
		LoadReport ignored;
		const Result<AssetMetadata> result = AssetMetadata::Parse(text, "A.glb.meta", ignored);
		REQUIRE_FALSE(result.HasValue());
		CHECK_MESSAGE(result.GetError().Message.find(expected) != std::string::npos, result.GetError().Message);
	};
	failsWith(R"({})", "/SubAssets");
	failsWith(R"([ { "Key": "Mesh/A", "Type": "Mesh" } ])", "needs \"Key\", \"Type\" and \"Handle\"");
	failsWith(R"([ { "Key": "", "Type": "Mesh", "Handle": "0x01" } ])", "/SubAssets/0/Key");
	failsWith(R"([ { "Key": "A", "Type": "Gizmo", "Handle": "0x01" } ])", "/SubAssets/0/Type");
	failsWith(R"([ { "Key": "A", "Type": "Mesh", "Handle": "0x00" } ])", "non-null handle");
	failsWith(
		R"([ { "Key": "A", "Type": "Mesh", "Handle": "0x01" }, { "Key": "A", "Type": "Mesh", "Handle": "0x02" } ])",
		"listed twice");
	failsWith(
		R"([ { "Key": "A", "Type": "Mesh", "Handle": "0x01" }, { "Key": "B", "Type": "Mesh", "Handle": "0x01" } ])",
		"listed twice");

	LoadReport warnings;
	const Result<AssetMetadata> extra = AssetMetadata::Parse(
		R"({ "Version": 1, "SubAssets": [ { "Key": "A", "Type": "Mesh", "Handle": "0x01", "Color": "red" } ] })",
		"A.glb.meta", warnings);
	REQUIRE(extra.HasValue());
	REQUIRE(warnings.GetWarnings().size() == 1);
	CHECK(warnings.GetWarnings()[0].Path == "/SubAssets/0/Color");
}

TEST_CASE("Asset: sub-asset handles are reused by key and type, and new keys get new handles")
{
	uint64_t next = 100;
	SubAssetHandles handles(
		{ { "Mesh/A", AssetType::Mesh, UUID(1) }, { "Material/A", AssetType::Material, UUID(2) } },
		[&next] { return UUID(next++); });
	CHECK(handles.Acquire("Mesh/A", AssetType::Mesh) == UUID(1));
	CHECK_FALSE(handles.HasChanged());
	CHECK(handles.Acquire("Mesh/B", AssetType::Mesh) == UUID(100));
	CHECK(handles.Acquire("Mesh/B", AssetType::Mesh) == UUID(100)); // asking twice gives the same handle
	CHECK(handles.HasChanged());
	// A known key of another type: its old references were to another kind of asset.
	CHECK(handles.Acquire("Material/A", AssetType::Texture) == UUID(101));
	const std::vector<SubAssetEntry>& entries = handles.GetEntries();
	REQUIRE(entries.size() == 3);
	CHECK(entries[1] == SubAssetEntry{ "Material/A", AssetType::Texture, UUID(101) });
	CHECK(entries[2] == SubAssetEntry{ "Mesh/B", AssetType::Mesh, UUID(100) });

	// The generator may return handles the list holds (or the null handle): they are skipped.
	uint64_t colliding = 0;
	SubAssetHandles skip({ { "X", AssetType::Mesh, UUID(1) } }, [&colliding] { return UUID(colliding++); });
	CHECK(skip.Acquire("Y", AssetType::Mesh) == UUID(2));
}

TEST_CASE("Asset: the registry keeps sub-assets with the file that contains them")
{
	AssetRegistry registry;
	REQUIRE(registry.Add({ UUID(1), "Assets/Ship.glb", AssetType::Model }).HasValue());
	REQUIRE(registry.Add({ UUID(2), "Assets/Ship.png", AssetType::Texture }).HasValue());
	REQUIRE(registry.AddSubAsset({ UUID(10), {}, AssetType::Mesh, UUID(1), "Mesh/Hull" }).HasValue());
	// Add with a Parent adds a sub-asset too; the path always comes from the file.
	REQUIRE(registry.Add({ UUID(11), "ignored", AssetType::Material, UUID(1), "Material/Paint" }).HasValue());

	CHECK(registry.GetCount() == 2);
	CHECK(registry.GetSubAssetCount() == 2);
	const AssetRecord* hull = registry.Find(UUID(10));
	REQUIRE(hull != nullptr);
	CHECK(hull->IsSubAsset());
	CHECK(hull->Path == "Assets/Ship.glb");
	CHECK(registry.FindSubAsset(UUID(1), "Material/Paint")->Handle == UUID(11));
	CHECK(registry.FindSubAsset(UUID(1), "Mesh/Sail") == nullptr);
	CHECK(registry.FindByPath("Assets/Ship.glb")->Handle == UUID(1)); // never a sub-asset
	CHECK(KeysOf(registry.ListSubAssets(UUID(1))) == std::vector<std::string>{ "Material/Paint", "Mesh/Hull" });

	// Files sorted by path, each followed by its sub-assets sorted by key.
	const std::vector<AssetRecord> all = registry.List();
	REQUIRE(all.size() == 4);
	CHECK(all[0].Handle == UUID(1));
	CHECK(all[1].Handle == UUID(11));
	CHECK(all[2].Handle == UUID(10));
	CHECK(all[3].Handle == UUID(2));
	CHECK(registry.List(AssetType::Mesh).size() == 1);

	CHECK_FALSE(registry.AddSubAsset({ UUID(12), {}, AssetType::Mesh, UUID(99), "Mesh/X" }).HasValue());   // no file
	CHECK_FALSE(registry.AddSubAsset({ UUID(12), {}, AssetType::Mesh, UUID(10), "Mesh/X" }).HasValue());   // nested
	CHECK_FALSE(registry.AddSubAsset({ UUID(12), {}, AssetType::None, UUID(1), "Mesh/X" }).HasValue());    // no type
	CHECK_FALSE(registry.AddSubAsset({ UUID(12), {}, AssetType::Mesh, UUID(1), "" }).HasValue());          // no key
	CHECK_FALSE(registry.AddSubAsset({ UUID(2), {}, AssetType::Mesh, UUID(1), "Mesh/X" }).HasValue());     // taken
	CHECK_FALSE(registry.AddSubAsset({ UUID(12), {}, AssetType::Mesh, UUID(1), "Mesh/Hull" }).HasValue()); // key
	CHECK_FALSE(registry.Add({ UUID(10), "Assets/Other.png", AssetType::Texture }).HasValue()); // a sub-asset's handle

	CHECK(registry.Remove(UUID(11)));
	CHECK(registry.GetSubAssetCount() == 1);
	CHECK(registry.Remove(UUID(1))); // the file takes its sub-assets along
	CHECK(registry.GetSubAssetCount() == 0);
	CHECK(registry.Find(UUID(10)) == nullptr);
	CHECK(registry.GetCount() == 1);
}

TEST_CASE("Asset: a scan registers the sub-assets that .meta files list")
{
	TempDirectory directory;
	directory.WriteFile("Assets/Ship.glb", "glb");
	directory.WriteFile(
		"Assets/Ship.glb.meta",
		R"({ "Version": 1, "Handle": "0x0000000000000001", "Type": "Model", "ImportSettings": {},
		     "SubAssets": [ { "Key": "Mesh/Hull", "Type": "Mesh", "Handle": "0x0000000000000010" },
		                    { "Key": "Mesh/Copy", "Type": "Mesh", "Handle": "0x0000000000000002" } ] })");
	directory.WriteFile("Assets/Ship.png", "png");
	directory.WriteFile(
		"Assets/Ship.png.meta", R"({ "Version": 1, "Handle": "0x0000000000000002", "Type": "Texture" })");
	AssetRegistry registry;
	const ScopedLogCapture log;
	const std::array<std::string, 1> directories = { "Assets" };
	const ScanReport report = registry.Scan(directory.GetVFS(), directories);
	CHECK(report.AssetCount == 2);
	CHECK(registry.GetSubAssetCount() == 1);
	CHECK(registry.Find(UUID(0x10))->SubAssetKey == "Mesh/Hull");
	// A sub-asset whose handle a file has is a duplicate and stays unregistered.
	CHECK(report.Count(ScanIssueKind::DuplicateHandle) == 1);
	CHECK(registry.Find(UUID(2))->Path == "Assets/Ship.png");
}

TEST_CASE("Asset: loading a file registers and caches its sub-assets and records new handles")
{
	BundleProject project;
	project.Directory.WriteFile("Assets/Parts.glb", "Wheel\nAxle\n");
	project.Assets.Scan();
	const AssetHandle file = project.Assets.GetHandle("Assets/Parts.glb");
	CHECK(project.Assets.GetRegistry().GetSubAssetCount() == 0); // unknown until the first import

	const Ref<BundleAsset> bundle = project.Assets.GetAsset<BundleAsset>(file);
	REQUIRE(bundle != nullptr);
	REQUIRE(bundle->Parts.size() == 2);
	const AssetHandle wheel = bundle->Parts[0].Handle;
	const Ref<PartAsset> part = project.Assets.GetAsset<PartAsset>(wheel);
	REQUIRE(part != nullptr);
	CHECK(part->Name == "Wheel");
	CHECK(part->GetPath() == "Assets/Parts.glb");
	CHECK(part->GetHandle() == wheel);
	CHECK(project.Assets.GetType(wheel) == AssetType::Mesh);
	CHECK(project.Assets.GetPath(wheel) == "Assets/Parts.glb");
	const Result<AssetMetadata> metadata = project.Assets.ReadMetadata(wheel); // a sub-asset reads its file's
	REQUIRE(metadata.HasValue());
	REQUIRE(metadata->SubAssets.size() == 2);
	CHECK(metadata->SubAssets[0] == SubAssetEntry{ "Part/Wheel", AssetType::Mesh, wheel });
	CHECK_FALSE(project.Assets.SetImportSettings(wheel, Json::object()).HasValue()); // only the file has settings

	// Unloading a sub-asset alone: the file still holds it and hands it out again.
	project.Assets.UnloadAsset(wheel);
	CHECK_FALSE(project.Assets.IsLoaded(wheel));
	CHECK(project.Assets.GetAsset<PartAsset>(wheel) == part);
	// Unloading the file forgets its sub-assets; loading a sub-asset loads the file.
	project.Assets.UnloadAsset(file);
	CHECK_FALSE(project.Assets.IsLoaded(wheel));
	const Ref<PartAsset> reloaded = project.Assets.GetAsset<PartAsset>(wheel);
	REQUIRE(reloaded != nullptr);
	CHECK(reloaded != part);
	CHECK(project.Assets.IsLoaded(file));

	// Unused assets: the file holds its sub-assets, so they go together once nobody else holds them.
	project.Assets.UnloadAsset(file);
	CHECK(project.Assets.UnloadUnused() == 0);
	{
		const Ref<BundleAsset> held = project.Assets.GetAsset<BundleAsset>(file);
		REQUIRE(held != nullptr);
	}
	CHECK(project.Assets.UnloadUnused() == 3);

	// A copy of the file with its .meta file is a duplicate; a new handle gives the copy and its sub-assets new ones.
	project.Directory.WriteFile("Assets/Copy.glb", project.Directory.ReadFile("Assets/Parts.glb"));
	project.Directory.WriteFile("Assets/Copy.glb.meta", project.Directory.ReadFile("Assets/Parts.glb.meta"));
	{
		const ScopedLogCapture log;
		CHECK(project.Assets.Scan().Count(ScanIssueKind::DuplicateHandle) == 1);
	}
	const Result<AssetHandle> renewed = project.Assets.AssignNewHandle("Assets/Copy.glb");
	REQUIRE(renewed.HasValue());
	const std::vector<AssetRecord> copies = project.Assets.GetRegistry().ListSubAssets(*renewed);
	REQUIRE(copies.size() == 2);
	CHECK(copies[0].Handle != wheel);
	CHECK(copies[1].Handle != wheel);
	CHECK(project.Assets.GetRegistry().Find(wheel)->Parent == file);
	const Ref<BundleAsset> copy = project.Assets.GetAsset<BundleAsset>(*renewed);
	REQUIRE(copy != nullptr);
	CHECK(copy->Parts[0].Handle == project.Assets.GetRegistry().FindSubAsset(*renewed, "Part/Wheel")->Handle);
}
