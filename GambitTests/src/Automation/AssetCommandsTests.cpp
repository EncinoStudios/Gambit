#include <doctest/doctest.h>

#include "AutomationFixture.h"

#include <fstream>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::filesystem::path WriteNative(AutomationFixture& fixture, std::string_view name, std::string_view text)
	{
		const std::filesystem::path path = fixture.Directory.GetPath() / std::string(name);
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << text;
		return path;
	}

	std::string Import(AutomationFixture& fixture, const std::filesystem::path& source, std::string_view target = {})
	{
		Json arguments = Json::object();
		arguments["source"] = source.generic_string();
		if (!target.empty())
		{
			arguments["path"] = std::string(target);
		}
		return fixture.Succeed("asset.import", arguments).at("handle").get<std::string>();
	}
}

TEST_CASE("Automation: asset.import copies a file into the project and registers it")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::filesystem::path csv = WriteNative(fixture, "download/Weapons.csv", "Id,Damage:float\npistol,12.5\n");

	const Json imported = fixture.Succeed(
		"asset.import", J(R"({"source": ")" + csv.generic_string() + R"(", "path": "Data/Weapons.csv"})"));
	CHECK(imported.at("path") == "Data/Weapons.csv");
	CHECK(imported.at("type") == "DataTable");
	CHECK(imported.at("size") == 28);
	CHECK(imported.at("handle").get<std::string>().size() == 18);
	CHECK(ReadTextFile(fixture.GetProjectDirectory() / "Data/Weapons.csv") == "Id,Damage:float\npistol,12.5\n");
	const Json meta = Json::parse(ReadTextFile(fixture.GetProjectDirectory() / "Data/Weapons.csv.meta"));
	CHECK(meta.at("Handle") == imported.at("handle"));
	CHECK(meta.at("Type") == "DataTable");

	// The default place is the Assets folder; import settings go to the .meta file.
	const std::filesystem::path logo = WriteNative(fixture, "download/Logo.png", "not really a png");
	const Json logoImported = fixture.Succeed(
		"asset.import", J(R"({"source": ")" + logo.generic_string() + R"(", "importSettings": {"Srgb": false}})"));
	CHECK(logoImported.at("path") == "Assets/Logo.png");
	CHECK(logoImported.at("type") == "Texture");
	CHECK(
		Json::parse(ReadTextFile(fixture.GetProjectDirectory() / "Assets/Logo.png.meta")).at("ImportSettings")
		== J(R"({"Srgb": false})"));
	const Json info = fixture.Succeed("asset.info", J(R"({"asset": "Assets/Logo.png"})"));
	CHECK(info.at("importSettings") == J(R"({"Srgb": false})"));
}

TEST_CASE("Automation: asset.import refuses what it cannot import")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::filesystem::path csv = WriteNative(fixture, "Weapons.csv", "Id\n");
	const std::string source = csv.generic_string();
	const auto reject = [&](std::string_view extra, CommandErrorCode code) {
		return fixture.Reject("asset.import", J(R"({"source": ")" + source + R"(")" + std::string(extra) + "}"), code)
			.Message;
	};

	CHECK(
		reject(R"(, "path": "Data/Notes.txt")", CommandErrorCode::InvalidArguments)
			.find("is not a kind of file the engine imports")
		!= std::string::npos);
	CHECK(
		reject(R"(, "path": "Elsewhere/Weapons.csv")", CommandErrorCode::InvalidArguments)
			.find("outside the asset directories")
		!= std::string::npos);
	CHECK(
		reject(R"(, "path": "../Weapons.csv")", CommandErrorCode::InvalidArguments).find("not a valid project path")
		!= std::string::npos);
	fixture.Succeed("asset.import", J(R"({"source": ")" + source + R"(", "path": "Data/Weapons.csv"})"));
	CHECK(
		reject(R"(, "path": "Data/Weapons.csv")", CommandErrorCode::AlreadyExists).find("pass overwrite")
		!= std::string::npos);
	fixture.Succeed(
		"asset.import", J(R"({"source": ")" + source + R"(", "path": "Data/Weapons.csv", "overwrite": true})"));
	fixture.Reject(
		"asset.import", J(R"({"source": ")" + (fixture.Directory.GetPath() / "missing.csv").generic_string() + R"("})"),
		CommandErrorCode::NotFound);
	fixture.Reject("asset.import", J(R"({"path": "Data/X.csv"})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: asset.list and asset.info describe the assets")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string weapons = Import(fixture, WriteNative(fixture, "a/Weapons.csv", "Id\n"), "Data/Weapons.csv");
	Import(fixture, WriteNative(fixture, "a/Items.csv", "Id\n"), "Data/Items.csv");
	Import(fixture, WriteNative(fixture, "a/Crate.png", "png"), "Assets/Textures/Crate.png");
	Import(fixture, WriteNative(fixture, "a/Player.lua", "-- script"), "Scripts/Player.lua");

	const Json all = fixture.Succeed("asset.list");
	CHECK(all.at("total") == 5); // and the start scene
	std::vector<std::string> paths;
	for (const Json& asset : all.at("assets"))
	{
		paths.push_back(asset.at("path").get<std::string>());
	}
	CHECK(
		paths
		== std::vector<std::string>{ "Assets/Textures/Crate.png", "Data/Items.csv", "Data/Weapons.csv",
	                                 "Scenes/Main.gscene", "Scripts/Player.lua" });

	const auto count = [&](std::string_view arguments) {
		return fixture.Succeed("asset.list", J(arguments)).at("total").get<size_t>();
	};
	CHECK(count(R"({"type": "DataTable"})") == 2);
	CHECK(count(R"({"type": "Texture"})") == 1);
	CHECK(count(R"({"path": "Data/"})") == 2);
	CHECK(count(R"({"type": "Script", "path": "Data/"})") == 0);
	const Json page = fixture.Succeed("asset.list", J(R"({"limit": 2, "offset": 1})"));
	CHECK(page.at("count") == 2);
	CHECK(page.at("total") == 5);
	CHECK(page.at("assets")[0].at("path") == "Data/Items.csv");
	fixture.Reject("asset.list", J(R"({"type": "Sound"})"), CommandErrorCode::InvalidArguments);

	const Json byPath = fixture.Succeed("asset.info", J(R"({"asset": "Data/Weapons.csv"})"));
	CHECK(byPath.at("handle") == weapons);
	CHECK(byPath.at("type") == "DataTable");
	CHECK(byPath.at("dependencies").empty());
	CHECK(fixture.Succeed("asset.info", J(R"({"asset": ")" + weapons + R"("})")).at("path") == "Data/Weapons.csv");
	fixture.Reject("asset.info", J(R"({"asset": "Data/Nope.csv"})"), CommandErrorCode::NotFound);
	fixture.Reject("asset.info", J(R"({"asset": "0x1234"})"), CommandErrorCode::NotFound);
	fixture.Reject("asset.info", J(R"({"asset": "0xZZ"})"), CommandErrorCode::InvalidArguments);
}

TEST_CASE("Automation: prefab.create saves a subtree and prefab.instantiate adds copies")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	const std::string crate = fixture.Create(
		"Crate", J(R"({"components": {"Health": {"Current": 9}, "Transform": {"Translation": [1, 1, 1]}}})"));
	fixture.Create("Lid", J(R"({"parent": "Crate", "components": {"Transform": {"Translation": [0, 1, 0]}}})"));
	fixture.Create("Stage");

	const Json created =
		fixture.Succeed("prefab.create", J(R"({"entity": "Crate", "path": "Assets/Prefabs/Crate.gprefab"})"));
	CHECK(created.at("path") == "Assets/Prefabs/Crate.gprefab");
	CHECK(created.at("name") == "Crate");
	CHECK(created.at("entityCount") == 2);
	const Json file = Json::parse(ReadTextFile(fixture.GetProjectDirectory() / "Assets/Prefabs/Crate.gprefab"));
	CHECK(file.at("Name") == "Crate");
	CHECK(file.at("Entities").size() == 2);
	CHECK(fixture.Succeed("asset.info", J(R"({"asset": "Assets/Prefabs/Crate.gprefab"})")).at("type") == "Prefab");

	// Instances are new entities with new UUIDs that know their prefab.
	Json instance;
	fixture.CheckUndoRedo([&] {
		instance = fixture.Succeed(
			"prefab.instantiate",
			J(R"({"prefab": "Assets/Prefabs/Crate.gprefab", "parent": "Stage", "transform": {"Translation": [5, 0, 0]}})"));
	});
	CHECK(instance.at("name") == "Crate");
	CHECK(instance.at("entityCount") == 2);
	CHECK(instance.at("parent") == fixture.Succeed("entity.get", J(R"({"entity": "Stage"})")).at("uuid"));
	CHECK(instance.at("uuid") != crate);
	const Json instanceData =
		fixture.Succeed("entity.get", J(R"({"entity": ")" + instance.at("uuid").get<std::string>() + R"("})"));
	CHECK(instanceData.at("components").at("Transform").at("Translation") == J("[5.0, 0.0, 0.0]"));
	CHECK(instanceData.at("components").at("Health").at("Current") == 9.0);
	CHECK(instanceData.at("components").contains("PrefabInstance"));
	CHECK(instanceData.at("components").at("PrefabInstance").at("Prefab") == created.at("handle"));
	CHECK(instanceData.at("children").size() == 1);

	// By handle, at a position, as a root.
	const Json second = fixture.Succeed(
		"prefab.instantiate", J(R"({"prefab": ")" + created.at("handle").get<std::string>() + R"(", "index": 0})"));
	CHECK(second.at("index") == 0);
	CHECK(second.at("parent").is_null());

	// The prefab file itself does not change when the scene does.
	fixture.Succeed(
		"component.set", J(R"({"entity": ")" + crate + R"(", "component": "Health", "values": {"Current": 1}})"));
	const Json again = fixture.Succeed("prefab.instantiate", J(R"({"prefab": "Assets/Prefabs/Crate.gprefab"})"));
	CHECK(
		fixture
			.Succeed(
				"component.get",
				J(R"({"entity": ")" + again.at("uuid").get<std::string>() + R"(", "component": "Health"})"))
			.at("values")
			.at("Current")
		== 9.0);
}

TEST_CASE("Automation: prefab commands explain their refusals")
{
	AutomationFixture fixture;
	fixture.CreateProject();
	fixture.Create("Crate");
	fixture.Succeed("prefab.create", J(R"({"entity": "Crate", "path": "Assets/Crate.gprefab", "name": "Box"})"));
	CHECK(
		fixture.Succeed("asset.info", J(R"({"asset": "Assets/Crate.gprefab"})")).at("path") == "Assets/Crate.gprefab");

	fixture.Reject(
		"prefab.create", J(R"({"entity": "Crate", "path": "Assets/Crate.gprefab"})"), CommandErrorCode::AlreadyExists);
	fixture.Succeed("prefab.create", J(R"({"entity": "Crate", "path": "Assets/Crate.gprefab", "overwrite": true})"));
	fixture.Reject(
		"prefab.create", J(R"({"entity": "Crate", "path": "Assets/Crate.scene"})"), CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"prefab.create", J(R"({"entity": "Nobody", "path": "Assets/X.gprefab"})"), CommandErrorCode::NotFound);

	const uint64_t before = fixture.Hash();
	fixture.Reject("prefab.instantiate", J(R"({"prefab": "Assets/Nope.gprefab"})"), CommandErrorCode::NotFound);
	const CommandResult wrongType = fixture.Reject(
		"prefab.instantiate", J(R"({"prefab": "Scenes/Main.gscene"})"), CommandErrorCode::InvalidArguments);
	CHECK(wrongType.Message == "'Scenes/Main.gscene' is a Scene, not a prefab");
	fixture.Reject(
		"prefab.instantiate", J(R"({"prefab": "Assets/Crate.gprefab", "parent": "Nobody"})"),
		CommandErrorCode::NotFound);
	fixture.Reject(
		"prefab.instantiate", J(R"({"prefab": "Assets/Crate.gprefab", "transform": {"Translation": "x"}})"),
		CommandErrorCode::InvalidArguments);
	fixture.Reject(
		"prefab.instantiate", J(R"({"prefab": "Assets/Crate.gprefab", "transform": {"Bogus": 1}})"),
		CommandErrorCode::InvalidArguments);
	CHECK(fixture.Hash() == before);
}
