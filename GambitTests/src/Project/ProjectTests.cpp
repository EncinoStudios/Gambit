#include <doctest/doctest.h>

#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/DataTable.h"
#include "Gambit/Asset/SceneAsset.h"
#include "Gambit/Core/FileSystem.h"
#include "Gambit/ECS/World.h"
#include "Gambit/Project/Project.h"
#include "Gambit/Serialization/SceneSerializer.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::string ValidationError(const ProjectSettings& settings)
	{
		const Result<void> result = ValidateProjectSettings(settings);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	}

	ProjectSettings ValidSettings()
	{
		ProjectSettings settings;
		settings.Name = "Game";
		return settings;
	}

	const char* MinimalProject = R"({"Version": 1, "Name": "Minimal"})";
}

TEST_CASE("Project: a new project is scaffolded with folders and default files")
{
	ScopedTempDirectory directory;
	const std::filesystem::path root = directory.GetPath() / "Games" / "Tetris";
	ScopedLogCapture log;
	const Result<Ref<Project>> created = Project::New(root, "Tetris");
	REQUIRE(created.HasValue());
	const Project& project = **created;

	CHECK(project.GetDirectory() == std::filesystem::absolute(root).lexically_normal());
	CHECK(project.GetFilePath().filename() == "Tetris.gproject");
	CHECK(project.GetSettings().Name == "Tetris");
	CHECK(project.GetSettings().StartScene == "Scenes/Main.gscene");
	CHECK(project.GetSettings().Export.OutputName == "Tetris");
	for (const char* folder : { "Assets", "Scripts", "Data", "Scenes", "Comments" })
	{
		CHECK_MESSAGE(std::filesystem::is_directory(root / folder), folder);
	}
	CHECK(std::filesystem::exists(root / "Tetris.gproject"));
	CHECK(std::filesystem::exists(root / "InputActions.json"));
	CHECK(std::filesystem::exists(root / "Scenes" / "Main.gscene"));

	// The default input mapping is valid, and the default scene loads and is empty.
	const Result<InputActions> actions = project.LoadInputActions();
	REQUIRE(actions.HasValue());
	CHECK(actions->Find("Move") != InvalidInputAction);
	CHECK(actions->Find("Jump") != InvalidInputAction);
	World world(project.GetTypeRegistry());
	LoadReport report;
	const Result<SceneMetadata> scene = SceneSerializer::Load(world, project.GetVFS(), "Scenes/Main.gscene", report);
	REQUIRE(scene.HasValue());
	CHECK(scene->Name == "Main");
	CHECK(world.GetEntityCount() == 0);
	CHECK(world.GetSeed() == project.GetSettings().DefaultSeed);

	// The VFS is rooted in the project, and the assets are found by a scan.
	CHECK(project.GetVFS().Exists("Scenes/Main.gscene"));
	const ScanReport scan = project.GetAssets().Scan();
	CHECK(scan.AssetCount == 1); // the scene
	CHECK(project.GetAssets().GetType(project.GetAssets().GetHandle("Scenes/Main.gscene")) == AssetType::Scene);
	CHECK(project.GetAssetDirectories() == std::vector<std::string>{ "Assets", "Scenes", "Data", "Scripts" });
}

TEST_CASE("Project: the project file is stable JSON")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const Ref<Project> project = *Project::New(directory.GetPath() / "P", "Demo");
	const std::string text = ReadTextFile(project->GetFilePath());
	CHECK(text == R"({
  "Version": 1,
  "Name": "Demo",
  "StartScene": "Scenes/Main.gscene",
  "Directories": {
    "Assets": "Assets",
    "Scripts": "Scripts",
    "Data": "Data",
    "Scenes": "Scenes",
    "Comments": "Comments"
  },
  "SimulationHz": 60,
  "DefaultSeed": 1,
  "InputActions": "InputActions.json",
  "Rendering": {
    "Environment": "0x0000000000000000",
    "Tonemapper": "ACES",
    "Exposure": 1.0
  },
  "Export": {
    "OutputName": "Demo",
    "Compress": true
  }
}
)");
	LoadReport report;
	const Result<ProjectSettings> parsed =
		Project::ParseSettings(project->GetTypeRegistry(), text, "Demo.gproject", report);
	REQUIRE(parsed.HasValue());
	CHECK(report.IsEmpty());
	CHECK(*Project::SerializeSettings(project->GetTypeRegistry(), *parsed) == text);
}

TEST_CASE("Project: a project loads from its file or its directory")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const std::filesystem::path root = directory.GetPath() / "Demo";
	{
		const Ref<Project> created = *Project::New(root, "Demo");
		ProjectSettings settings = created->GetSettings();
		settings.SimulationHz = 120;
		settings.DefaultSeed = 99;
		settings.Rendering.Tonemap = Tonemapper::PBRNeutral;
		settings.Rendering.Exposure = 1.5f;
		settings.Rendering.Environment = UUID(0xFEED);
		settings.Export.Compress = false;
		settings.Directories.Data = "Tables";
		REQUIRE(created->SetSettings(settings).HasValue());
		REQUIRE(created->Save().HasValue());
	}

	for (const std::filesystem::path& path : { root, root / "Demo.gproject" })
	{
		const Result<Ref<Project>> loaded = Project::Load(path);
		REQUIRE(loaded.HasValue());
		const ProjectSettings& settings = (*loaded)->GetSettings();
		CHECK(settings.Name == "Demo");
		CHECK(settings.SimulationHz == 120);
		CHECK(settings.DefaultSeed == 99);
		CHECK(settings.Rendering.Tonemap == Tonemapper::PBRNeutral);
		CHECK(settings.Rendering.Exposure == 1.5f);
		CHECK(settings.Rendering.Environment == UUID(0xFEED));
		CHECK_FALSE(settings.Export.Compress);
		CHECK(settings.Directories.Data == "Tables");
		CHECK((*loaded)->GetDirectory() == std::filesystem::absolute(root).lexically_normal());
		CHECK((*loaded)->GetAssetDirectories() == std::vector<std::string>{ "Assets", "Scenes", "Tables", "Scripts" });
	}
}

TEST_CASE("Project: files are read through the project VFS and assets are loaded by handle")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const Ref<Project> project = *Project::New(directory.GetPath() / "Demo", "Demo");
	REQUIRE(project->GetVFS().WriteText("Data/Weapons.csv", "Id,Damage:float\npistol,12\n").HasValue());
	project->GetAssets().Scan();
	const AssetHandle handle = project->GetAssets().GetHandle("Data/Weapons.csv");
	const Ref<DataTable> table = project->GetAssets().GetAsset<DataTable>(handle);
	REQUIRE(table != nullptr);
	CHECK(*table->GetValue("pistol", "Damage") == FieldValue(12.0));

	// Moving a directory in the settings changes what is scanned.
	ProjectSettings settings = project->GetSettings();
	settings.Directories.Data = "Tables";
	REQUIRE(project->SetSettings(settings).HasValue());
	project->GetAssets().Scan();
	CHECK(project->GetAssets().GetHandle("Data/Weapons.csv") == AssetHandle());
}

TEST_CASE("Project: unknown settings are warnings and missing ones take defaults")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	std::filesystem::create_directories(directory.GetPath());
	const std::filesystem::path file = directory.GetPath() / "Minimal.gproject";
	REQUIRE(
		FileSystem::WriteText(file, R"({"Version": 1, "Name": "Minimal", "Nonsense": 1, "Rendering": {"Bloom": true}})")
			.HasValue());
	const Result<Ref<Project>> loaded = Project::Load(file);
	REQUIRE(loaded.HasValue());
	const ProjectSettings& settings = (*loaded)->GetSettings();
	CHECK(settings.SimulationHz == 60);
	CHECK(settings.StartScene.empty());
	CHECK(settings.Directories.Assets == "Assets");
	CHECK(settings.Rendering.Exposure == 1.0f);
	CHECK(log.Contains(LogLevel::Warn, "/Nonsense: unknown field"));
	CHECK(log.Contains(LogLevel::Warn, "/Rendering/Bloom: unknown field"));
}

TEST_CASE("Project: invalid project files are refused with a reason")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const TypeRegistry& types = TypeRegistry::Get();
	const auto parseError = [&](const char* text) {
		LoadReport report;
		const Result<ProjectSettings> result = Project::ParseSettings(types, text, "bad.gproject", report);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(parseError("{").find("bad.gproject: parse error") == 0);
	CHECK(parseError("[]").find("must be a JSON object") != std::string::npos);
	CHECK(parseError("{}").find("no \"Version\"") != std::string::npos);
	CHECK(parseError(R"({"Version": 2, "Name": "X"})").find("newer Gambit") != std::string::npos);
	CHECK(parseError(R"({"Version": 1})").find("not a valid project name") != std::string::npos);
	CHECK(parseError(R"({"Version": 1, "Name": 7})") == "bad.gproject: /Name: expected a string, found a number");
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "SimulationHz": "fast"})").find("/SimulationHz: expected an integer")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "SimulationHz": 0})").find("SimulationHz must be between 1 and 1000")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "SimulationHz": 5000})").find("SimulationHz must be between")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "Rendering": {"Tonemapper": "Filmic"}})")
			.find("/Rendering/Tonemapper: 'Filmic' is not a value of enum 'Tonemapper'")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "Rendering": {"Exposure": -1}})")
			.find("Exposure must be a positive number")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "Directories": {"Assets": "../Elsewhere"}})")
			.find("Directories.Assets")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "Directories": {"Data": "Assets"}})")
			.find("used by another directory")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "StartScene": "/abs/Main.gscene"})").find("StartScene")
		!= std::string::npos);
	CHECK(
		parseError(R"({"Version": 1, "Name": "X", "InputActions": ""})").find("InputActions must not be empty")
		!= std::string::npos);

	// Loading: wrong kinds of paths.
	CHECK_FALSE(Project::Load(directory.GetPath() / "missing").HasValue());
	std::filesystem::create_directories(directory.GetPath() / "Empty");
	const Result<Ref<Project>> empty = Project::Load(directory.GetPath() / "Empty");
	REQUIRE_FALSE(empty.HasValue());
	CHECK(empty.GetError().Message.find("contains no .gproject file") != std::string::npos);
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "Empty" / "A.gproject", MinimalProject).HasValue());
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "Empty" / "B.gproject", MinimalProject).HasValue());
	const Result<Ref<Project>> ambiguous = Project::Load(directory.GetPath() / "Empty");
	REQUIRE_FALSE(ambiguous.HasValue());
	CHECK(ambiguous.GetError().Message.find("contains 2 .gproject files") != std::string::npos);
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "notes.txt", "x").HasValue());
	CHECK(
		Project::Load(directory.GetPath() / "notes.txt").GetError().Message.find("not a .gproject file")
		!= std::string::npos);
	CHECK(
		Project::Load(directory.GetPath() / "Empty" / "A.gproject").HasValue()); // a file in a folder of several works
	REQUIRE(FileSystem::WriteText(directory.GetPath() / "Bad.gproject", "{ nope").HasValue());
	const Result<Ref<Project>> broken = Project::Load(directory.GetPath() / "Bad.gproject");
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.find("Bad.gproject: parse error") != std::string::npos);
}

TEST_CASE("Project: creating a project refuses bad names and existing projects, and keeps existing files")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	for (const char* name :
	     { "", "a/b", "a\\b", "what?", "star*", "tab\t", "dots.", "trailing ", "NUL", "con", "..", "bad\xFF" })
	{
		CHECK_MESSAGE(!Project::New(directory.GetPath() / "Bad", name).HasValue(), name);
	}
	CHECK_FALSE(Project::New(directory.GetPath() / "Bad", std::string(65, 'x')).HasValue());
	CHECK(Project::New(directory.GetPath() / "Long", std::string(64, 'x')).HasValue());
	CHECK(Project::New(directory.GetPath() / "Spaces", "My Game 2").HasValue());
	CHECK(Project::New(directory.GetPath() / "Unicode", "Caf\xC3\xA9").HasValue());

	const std::filesystem::path root = directory.GetPath() / "Existing";
	std::filesystem::create_directories(root / "Scenes");
	REQUIRE(FileSystem::WriteText(root / "Scenes" / "Main.gscene", "my scene").HasValue());
	REQUIRE(FileSystem::WriteText(root / "InputActions.json", "my input").HasValue());
	const Result<Ref<Project>> created = Project::New(root, "Game");
	REQUIRE(created.HasValue());
	CHECK(ReadTextFile(root / "Scenes" / "Main.gscene") == "my scene"); // not overwritten
	CHECK(ReadTextFile(root / "InputActions.json") == "my input");

	const Result<Ref<Project>> again = Project::New(root, "Other");
	REQUIRE_FALSE(again.HasValue());
	CHECK(again.GetError().Message.find("already contains the project file 'Game.gproject'") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(root / "Other.gproject"));

	// A directory with several project files is no place for yet another one.
	REQUIRE(FileSystem::WriteText(root / "Copy.gproject", ReadTextFile(root / "Game.gproject")).HasValue());
	const Result<Ref<Project>> third = Project::New(root, "Third");
	REQUIRE_FALSE(third.HasValue());
	CHECK(third.GetError().Message.find("contains 2 .gproject files") != std::string::npos);
	CHECK_FALSE(std::filesystem::exists(root / "Third.gproject"));
}

TEST_CASE("Project: settings are validated before they are taken")
{
	CHECK(ValidateProjectSettings(ValidSettings()).HasValue());
	ProjectSettings settings = ValidSettings();

	settings.Name = "bad/name";
	CHECK(ValidationError(settings).find("not a valid project name") != std::string::npos);
	settings = ValidSettings();
	settings.SimulationHz = 1001;
	CHECK(ValidationError(settings).find("SimulationHz") != std::string::npos);
	settings = ValidSettings();
	settings.Rendering.Exposure = std::numeric_limits<float>::quiet_NaN();
	CHECK(ValidationError(settings).find("Exposure") != std::string::npos);
	settings = ValidSettings();
	settings.Export.OutputName = "what?";
	CHECK(ValidationError(settings).find("Export.OutputName") != std::string::npos);
	settings = ValidSettings();
	settings.Directories.Scripts = "Assets/../Scripts";
	CHECK(ValidationError(settings).find("Directories.Scripts") != std::string::npos);
	settings = ValidSettings();
	settings.Directories.Comments = "";
	CHECK(ValidationError(settings).find("Directories.Comments must not be empty") != std::string::npos);
	settings = ValidSettings();
	settings.Directories.Comments = "Assets";
	CHECK(ValidationError(settings).find("used by another directory") != std::string::npos);
	settings = ValidSettings();
	settings.StartScene = "Scenes\\Main.gscene";
	CHECK(ValidationError(settings).find("StartScene") != std::string::npos);
	settings = ValidSettings();
	settings.InputActionsPath = "C:/input.json";
	CHECK(ValidationError(settings).find("InputActions") != std::string::npos);

	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const Ref<Project> project = *Project::New(directory.GetPath() / "P", "P");
	ProjectSettings invalid = project->GetSettings();
	invalid.SimulationHz = 0;
	CHECK_FALSE(project->SetSettings(invalid).HasValue());
	CHECK(project->GetSettings().SimulationHz == 60); // unchanged
}

TEST_CASE("Project: one project is active at a time")
{
	ScopedTempDirectory directory;
	ScopedLogCapture log;
	const Ref<Project> first = *Project::New(directory.GetPath() / "A", "A");
	const Ref<Project> second = *Project::New(directory.GetPath() / "B", "B");
	const Ref<Project> before = Project::GetActive();
	Project::SetActive(first);
	CHECK(Project::GetActive() == first);
	Project::SetActive(second);
	CHECK(Project::GetActive() == second);
	CHECK(first->GetSettings().Name == "A"); // other instances keep working
	Project::SetActive(nullptr);
	CHECK(Project::GetActive() == nullptr);
	Project::SetActive(before);
}

TEST_CASE("Project: project names")
{
	CHECK(IsValidProjectName("Tetris"));
	CHECK(IsValidProjectName("My Game 2"));
	CHECK(IsValidProjectName("Caf\xC3\xA9"));
	CHECK(IsValidProjectName("a.b"));
	CHECK_FALSE(IsValidProjectName(""));
	CHECK_FALSE(IsValidProjectName("a:b"));
	CHECK_FALSE(IsValidProjectName("a|b"));
	CHECK_FALSE(IsValidProjectName("a\"b"));
	CHECK_FALSE(IsValidProjectName("a<b"));
	CHECK_FALSE(IsValidProjectName("."));
	CHECK_FALSE(IsValidProjectName("COM1"));
	CHECK_FALSE(IsValidProjectName("\xC3\x28"));
}
