#include <doctest/doctest.h>

#include "ECS/TestWorld.h"
#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Asset/DataTable.h"
#include "Gambit/Asset/Prefab.h"
#include "Gambit/Asset/SceneAsset.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	// An importer for ".tex" style test assets that counts its loads.
	class CountingTexture final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Texture;
		explicit CountingTexture(std::string content)
			: Content(std::move(content))
		{}
		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		std::string Content;
	};

	class CountingTextureImporter final : public IAssetImporter
	{
	public:
		explicit CountingTextureImporter(int& loads)
			: m_Loads(loads)
		{}
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Texture; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override
		{
			++m_Loads;
			Result<std::string> text = context.Vfs.ReadText(context.Record.Path);
			if (!text)
			{
				return text.GetError();
			}
			if (*text == "corrupt")
			{
				return Error("the texture is corrupt");
			}
			Ref<CountingTexture> texture = CreateRef<CountingTexture>(*text);
			return Ref<Asset>(std::move(texture));
		}

	private:
		int& m_Loads;
	};

	struct ProjectFixture
	{
		TempDirectory Directory;
		TestWorld Types; // owns the TypeRegistry the manager reads scenes with
		AssetManager Assets;

		ProjectFixture()
			: Assets(Directory.GetVFSRef(), { "Assets", "Scenes", "Data" }, Types.Registry)
		{}
	};

	const char* TwoRowTable = "Id,Damage:float\npistol,10\nrifle,30\n";
}

TEST_CASE("Asset: the manager scans a project and knows paths, handles and types")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/Weapons.csv", TwoRowTable);
	project.Directory.WriteFile("Scenes/Main.gscene", R"({"Version": 1, "Entities": []})");
	project.Directory.WriteFile("Assets/Crate.png", "png");

	const ScanReport report = project.Assets.Scan();
	CHECK(report.AssetCount == 3);
	const AssetHandle weapons = project.Assets.GetHandle("Data/Weapons.csv");
	REQUIRE(weapons.IsValid());
	CHECK(project.Assets.GetPath(weapons) == "Data/Weapons.csv");
	CHECK(project.Assets.GetType(weapons) == AssetType::DataTable);
	CHECK(project.Assets.Find(weapons)->Path == "Data/Weapons.csv");
	CHECK(project.Assets.FindByPath("Assets/Crate.png")->Type == AssetType::Texture);
	CHECK_FALSE(project.Assets.GetHandle("Data/Nope.csv").IsValid());
	CHECK(project.Assets.GetPath(UUID(12345)).empty());
	CHECK(project.Assets.GetType(UUID(12345)) == AssetType::None);
	CHECK(project.Assets.GetRegistry().GetCount() == 3);
	CHECK(&project.Assets.GetVFS() == &project.Directory.GetVFS());
}

TEST_CASE("Asset: assets are loaded on demand and cached")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/Weapons.csv", TwoRowTable);
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Data/Weapons.csv");

	CHECK(project.Assets.GetState(handle) == AssetState::Unloaded);
	CHECK_FALSE(project.Assets.IsLoaded(handle));
	const Ref<DataTable> table = project.Assets.GetAsset<DataTable>(handle);
	REQUIRE(table != nullptr);
	CHECK(table->GetHandle() == handle);
	CHECK(table->GetPath() == "Data/Weapons.csv");
	CHECK(table->GetRowCount() == 2);
	CHECK(project.Assets.IsLoaded(handle));
	CHECK(project.Assets.GetState(handle) == AssetState::Loaded);

	// The second request returns the same object.
	CHECK(project.Assets.GetAsset<DataTable>(handle) == table);
	const Result<Ref<Asset>> generic = project.Assets.LoadAsset(handle);
	REQUIRE(generic.HasValue());
	CHECK(generic->get() == table.get());
}

TEST_CASE("Asset: scenes and prefabs load as assets with their dependencies")
{
	ScopedLogCapture log; // making the prefab warns about the references of the test component
	ProjectFixture project;
	World& world = *project.Types.Scene;
	Entity crate = world.CreateEntityWithUUID(UUID(10), "Crate");
	crate.AddComponent<AllTypesComponent>().Texture = UUID(0x77);
	Entity lid = world.CreateEntityWithUUID(UUID(11), "Lid");
	REQUIRE(world.SetParent(lid, crate).HasValue());
	const Result<Ref<Prefab>> prefab = PrefabUtils::CreateFromEntity(world, crate);
	REQUIRE(prefab.HasValue());
	REQUIRE(PrefabUtils::Save(project.Types.Registry, **prefab, project.Directory.GetVFS(), "Assets/Crate.gprefab")
	            .HasValue());
	REQUIRE(SceneSerializer::Save(
				world, SceneMetadata{ "Level", Json::object() }, project.Directory.GetVFS(), "Scenes/Level.gscene")
	            .HasValue());
	project.Assets.Scan();

	const AssetHandle prefabHandle = project.Assets.GetHandle("Assets/Crate.gprefab");
	const Ref<Prefab> loadedPrefab = project.Assets.GetAsset<Prefab>(prefabHandle);
	REQUIRE(loadedPrefab != nullptr);
	CHECK(loadedPrefab->GetEntities().size() == 2);
	CHECK(loadedPrefab->GetHandle() == prefabHandle);
	CHECK(loadedPrefab->GetDependencies() == std::vector<AssetHandle>{ UUID(2), UUID(0x77) });

	const AssetHandle sceneHandle = project.Assets.GetHandle("Scenes/Level.gscene");
	const Ref<SceneAsset> scene = project.Assets.GetAsset<SceneAsset>(sceneHandle);
	REQUIRE(scene != nullptr);
	CHECK(scene->GetData().Metadata.Name == "Level");
	CHECK(scene->GetData().Entities.size() == 2);
	CHECK(*project.Assets.GetDependencies(sceneHandle) == std::vector<AssetHandle>{ UUID(2), UUID(0x77) });
	TestWorld target;
	REQUIRE(SceneSerializer::Apply(*target.Scene, scene->GetData()).HasValue());
	CHECK(target.Scene->ComputeStateHash() == world.ComputeStateHash());
}

TEST_CASE("Asset: asking for the wrong kind of asset fails cleanly")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/Weapons.csv", TwoRowTable);
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Data/Weapons.csv");

	ScopedLogCapture log;
	CHECK(project.Assets.GetAsset<Prefab>(handle) == nullptr);
	CHECK(log.Contains(LogLevel::Error, "is a DataTable, not a Prefab"));
	CHECK(project.Assets.GetAsset<DataTable>(UUID(424242)) == nullptr); // unknown handle
	const Result<Ref<Asset>> unknown = project.Assets.LoadAsset(UUID(424242));
	REQUIRE_FALSE(unknown.HasValue());
	CHECK(unknown.GetError().Message.find("no asset with handle") != std::string::npos);
	CHECK(project.Assets.GetAsset<DataTable>(handle) != nullptr); // the table itself is unharmed
}

TEST_CASE("Asset: importers are registered per type, and types without one cannot load")
{
	ProjectFixture project;
	project.Directory.WriteFile("Assets/Crate.png", "pixels");
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Assets/Crate.png");

	const Result<Ref<Asset>> without = project.Assets.LoadAsset(handle);
	REQUIRE_FALSE(without.HasValue());
	CHECK(without.GetError().Message.find("no importer for Texture assets") != std::string::npos);

	int loads = 0;
	project.Assets.RegisterImporter(CreateScope<CountingTextureImporter>(loads));
	const Ref<CountingTexture> texture = project.Assets.GetAsset<CountingTexture>(handle);
	REQUIRE(texture != nullptr);
	CHECK(texture->Content == "pixels");
	CHECK(loads == 1);
	CHECK(project.Assets.GetAsset<CountingTexture>(handle) == texture);
	CHECK(loads == 1); // cached

	// Import settings of the .meta file reach the importer.
	REQUIRE(project.Assets.SetImportSettings(handle, Json::parse(R"({"Srgb": false})")).HasValue());
	const Result<AssetMetadata> metadata = project.Assets.ReadMetadata(handle);
	REQUIRE(metadata.HasValue());
	CHECK(metadata->ImportSettings == Json::parse(R"({"Srgb": false})"));
	CHECK(metadata->Handle == handle);
	CHECK_FALSE(project.Assets.SetImportSettings(handle, Json::array()).HasValue());
	CHECK_FALSE(project.Assets.SetImportSettings(UUID(5), Json::object()).HasValue());
	CHECK_FALSE(project.Assets.ReadMetadata(UUID(5)).HasValue());
}

TEST_CASE("Asset: reloading replaces the cached asset, and failures are remembered until then")
{
	ProjectFixture project;
	int loads = 0;
	project.Assets.RegisterImporter(CreateScope<CountingTextureImporter>(loads));
	project.Directory.WriteFile("Assets/Crate.png", "corrupt");
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Assets/Crate.png");

	ScopedLogCapture log;
	const Result<Ref<Asset>> failed = project.Assets.LoadAsset(handle);
	REQUIRE_FALSE(failed.HasValue());
	CHECK(failed.GetError().Message.find("Assets/Crate.png: the texture is corrupt") != std::string::npos);
	CHECK(project.Assets.GetState(handle) == AssetState::Failed);
	CHECK(loads == 1);
	// Asking again neither loads nor logs again.
	const size_t logged = log.GetEntries().size();
	CHECK_FALSE(project.Assets.LoadAsset(handle).HasValue());
	CHECK(project.Assets.GetAsset<CountingTexture>(handle) == nullptr);
	CHECK(loads == 1);
	CHECK(log.GetEntries().size() == logged);

	// Fixing the file and reloading works; so does reloading a loaded asset.
	project.Directory.WriteFile("Assets/Crate.png", "fixed");
	const Result<Ref<Asset>> reloaded = project.Assets.ReloadAsset(handle);
	REQUIRE(reloaded.HasValue());
	CHECK(static_cast<CountingTexture&>(**reloaded).Content == "fixed");
	CHECK(project.Assets.GetState(handle) == AssetState::Loaded);
	project.Directory.WriteFile("Assets/Crate.png", "again");
	const Ref<CountingTexture> before = project.Assets.GetAsset<CountingTexture>(handle);
	REQUIRE(project.Assets.ReloadAsset(handle).HasValue());
	const Ref<CountingTexture> after = project.Assets.GetAsset<CountingTexture>(handle);
	CHECK(after != before);
	CHECK(before->Content == "fixed"); // holders of the old object keep it
	CHECK(after->Content == "again");
	CHECK(loads == 3);
}

TEST_CASE("Asset: loaded assets can be unloaded")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/A.csv", "Id\na\n");
	project.Directory.WriteFile("Data/B.csv", "Id\nb\n");
	project.Assets.Scan();
	const AssetHandle a = project.Assets.GetHandle("Data/A.csv");
	const AssetHandle b = project.Assets.GetHandle("Data/B.csv");
	Ref<DataTable> held = project.Assets.GetAsset<DataTable>(a);
	REQUIRE(project.Assets.GetAsset<DataTable>(b) != nullptr); // loaded, but nobody keeps it
	CHECK(project.Assets.UnloadUnused() == 1);
	CHECK(project.Assets.IsLoaded(a));
	CHECK_FALSE(project.Assets.IsLoaded(b));
	project.Assets.UnloadAsset(a);
	CHECK_FALSE(project.Assets.IsLoaded(a));
	CHECK(held->GetRowCount() == 1); // still usable
	held.reset();
	CHECK(project.Assets.UnloadUnused() == 0);
}

TEST_CASE("Asset: a rescan drops assets that went away or moved")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/A.csv", "Id\na\n");
	project.Directory.WriteFile("Data/B.csv", "Id\nb\n");
	project.Assets.Scan();
	const AssetHandle a = project.Assets.GetHandle("Data/A.csv");
	const AssetHandle b = project.Assets.GetHandle("Data/B.csv");
	REQUIRE(project.Assets.GetAsset<DataTable>(a) != nullptr);
	REQUIRE(project.Assets.GetAsset<DataTable>(b) != nullptr);

	project.Directory.RemoveFile("Data/B.csv");
	project.Directory.RemoveFile("Data/B.csv.meta");
	project.Assets.Scan();
	CHECK(project.Assets.IsLoaded(a));
	CHECK_FALSE(project.Assets.IsLoaded(b));
	CHECK(project.Assets.GetAsset<DataTable>(b) == nullptr);
	CHECK(project.Assets.GetHandle("Data/A.csv") == a); // handles are stable across scans
}

TEST_CASE("Asset: files become assets when they are registered, and duplicates get new handles")
{
	ProjectFixture project;
	project.Directory.WriteFile("Data/New.csv", "Id\nx\n");
	const Result<AssetHandle> handle = project.Assets.RegisterFile("Data/New.csv", Json::parse(R"({"Note": 1})"));
	REQUIRE(handle.HasValue());
	CHECK(project.Assets.GetHandle("Data/New.csv") == *handle);
	CHECK(project.Directory.Exists("Data/New.csv.meta"));
	CHECK(project.Assets.ReadMetadata(*handle)->ImportSettings == Json::parse(R"({"Note": 1})"));
	CHECK(*project.Assets.RegisterFile("Data/New.csv") == *handle); // already registered
	CHECK(project.Assets.RegisterFile("Data//./New.csv").Value() == *handle);

	// A file that arrives with a valid .meta keeps its handle.
	project.Directory.WriteFile("Data/Other.csv", "Id\ny\n");
	AssetMetadata supplied;
	supplied.Handle = UUID(0x4242);
	supplied.Type = AssetType::DataTable;
	project.Directory.WriteFile("Data/Other.csv.meta", *supplied.Serialize());
	CHECK(*project.Assets.RegisterFile("Data/Other.csv") == UUID(0x4242));

	const auto registerError = [&](std::string_view path) {
		const Result<AssetHandle> result = project.Assets.RegisterFile(path);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(registerError("Data/Missing.csv").find("does not exist") != std::string::npos);
	CHECK(registerError("Data/readme.txt").find("not a known kind of asset file") != std::string::npos);
	CHECK_FALSE(project.Assets.RegisterFile("../outside.csv").HasValue());
	CHECK(registerError("").find("project-relative") != std::string::npos);
	CHECK_FALSE(project.Assets.RegisterFile("Data/Fresh.csv", Json::array()).HasValue()); // settings are an object

	// A copy of an asset with its .meta file has the handle twice; the copy gets its own.
	project.Directory.WriteFile("Data/Copy.csv", "Id\ny\n");
	project.Directory.WriteFile("Data/Copy.csv.meta", *supplied.Serialize());
	CHECK(registerError("Data/Copy.csv").find("AssignNewHandle") != std::string::npos);
	const Result<AssetHandle> fresh = project.Assets.AssignNewHandle("Data/Copy.csv");
	REQUIRE(fresh.HasValue());
	CHECK(*fresh != UUID(0x4242));
	CHECK(project.Assets.GetHandle("Data/Copy.csv") == *fresh);
	CHECK(project.Assets.GetHandle("Data/Other.csv") == UUID(0x4242));
	CHECK(project.Assets.ReadMetadata(*fresh)->Handle == *fresh);
	CHECK_FALSE(project.Assets.AssignNewHandle("Data/Missing.csv").HasValue());
	CHECK_FALSE(project.Assets.AssignNewHandle("Data/readme.txt").HasValue());

	// A scan after all this agrees with the registry.
	const AssetHandle copyHandle = *fresh;
	const ScanReport report = project.Assets.Scan();
	CHECK(report.Issues.empty());
	CHECK(project.Assets.GetHandle("Data/Copy.csv") == copyHandle);
}

TEST_CASE("Asset: dependencies of data tables come from their asset columns")
{
	ProjectFixture project;
	project.Directory.WriteFile(
		"Data/Loot.csv",
		"Id,Icon:asset\nsword,0x0000000000000011\nshield,0x0000000000000011\naxe,0x0000000000000003\nrock,\n");
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Data/Loot.csv");
	const Result<std::vector<AssetHandle>> dependencies = project.Assets.GetDependencies(handle);
	REQUIRE(dependencies.HasValue());
	CHECK(*dependencies == std::vector<AssetHandle>{ UUID(3), UUID(0x11) });
	CHECK_FALSE(project.Assets.GetDependencies(UUID(5)).HasValue());
}
