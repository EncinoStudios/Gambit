#include <doctest/doctest.h>

#include "TestUtils/TempDirectory.h"

#include "Gambit/Asset/AssetMetadata.h"
#include "Gambit/Asset/AssetRegistry.h"
#include "Gambit/Asset/AssetType.h"
#include "Gambit/Core/DirectoryMount.h"

#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	std::string MetaText(uint64_t handle, const char* type, const char* settings = "{}")
	{
		AssetMetadata metadata;
		metadata.Handle = UUID(handle);
		metadata.Type = *ParseAssetType(type);
		metadata.ImportSettings = Json::parse(settings);
		return *metadata.Serialize();
	}

	const std::vector<std::string> Directories = { "Assets", "Scenes", "Data" };
}

TEST_CASE("Asset: asset types have names and are detected from file names")
{
	for (int index = 1; index <= static_cast<int>(AssetType::InputActions); ++index)
	{
		const auto type = static_cast<AssetType>(index);
		CHECK_MESSAGE(ParseAssetType(ToString(type)) == type, ToString(type));
	}
	CHECK(ToString(AssetType::None) == "None");
	CHECK_FALSE(ParseAssetType("None").has_value());
	CHECK_FALSE(ParseAssetType("texture").has_value()); // case-sensitive
	CHECK_FALSE(ParseAssetType("").has_value());

	CHECK(DetectAssetType("Scenes/Main.gscene") == AssetType::Scene);
	CHECK(DetectAssetType("Assets/Crate.gprefab") == AssetType::Prefab);
	CHECK(DetectAssetType("Assets/Crate.PNG") == AssetType::Texture); // any case
	CHECK(DetectAssetType("Assets/a.jpg") == AssetType::Texture);
	CHECK(DetectAssetType("Assets/sky.hdr") == AssetType::Environment);
	CHECK(DetectAssetType("Assets/Crate.glb") == AssetType::Mesh);
	CHECK(DetectAssetType("Assets/Crate.gltf") == AssetType::Mesh);
	CHECK(DetectAssetType("Assets/Wood.gmat") == AssetType::Material);
	CHECK(DetectAssetType("Scripts/Spin.lua") == AssetType::Script);
	CHECK(DetectAssetType("Assets/hit.wav") == AssetType::Audio);
	CHECK(DetectAssetType("Assets/Font.ttf") == AssetType::Font);
	CHECK(DetectAssetType("Assets/logo.svg") == AssetType::VectorGraphic);
	CHECK(DetectAssetType("Data/Weapons.csv") == AssetType::DataTable);
	CHECK(DetectAssetType("InputActions.json") == AssetType::InputActions);
	CHECK(DetectAssetType("Config/InputActions.json") == AssetType::InputActions);
	CHECK(DetectAssetType("Assets/Other.json") == AssetType::None);
	CHECK(DetectAssetType("Assets/Crate.png.meta") == AssetType::None); // sidecars are not assets
	CHECK(DetectAssetType("Assets/noextension") == AssetType::None);
	CHECK(DetectAssetType("Assets/.png") == AssetType::None); // a hidden file, not an extension
	CHECK(DetectAssetType("") == AssetType::None);
	CHECK(DetectAssetType("Assets.d/readme") == AssetType::None);
}

TEST_CASE("Asset: metadata round-trips and reports what is wrong")
{
	AssetMetadata metadata;
	metadata.Handle = UUID(0xABCDEF);
	metadata.Type = AssetType::Texture;
	metadata.ImportSettings = Json::parse(R"({"Srgb": true, "MaxSize": 2048})");
	const Result<std::string> text = metadata.Serialize();
	REQUIRE(text.HasValue());
	CHECK(
		*text
		== "{\n  \"Version\": 1,\n  \"Handle\": \"0x0000000000ABCDEF\",\n  \"Type\": \"Texture\",\n  "
		   "\"ImportSettings\": {\n    \"Srgb\": true,\n    \"MaxSize\": 2048\n  }\n}\n");

	LoadReport report;
	const Result<AssetMetadata> parsed = AssetMetadata::Parse(*text, "Assets/Crate.png.meta", report);
	REQUIRE(parsed.HasValue());
	CHECK(parsed->Handle == UUID(0xABCDEF));
	CHECK(parsed->Type == AssetType::Texture);
	CHECK(parsed->ImportSettings == metadata.ImportSettings);
	CHECK(report.IsEmpty());

	CHECK(AssetMetadata::GetMetaPath("Assets/Crate.png") == "Assets/Crate.png.meta");
	CHECK(AssetMetadata::IsMetaPath("Assets/Crate.png.meta"));
	CHECK_FALSE(AssetMetadata::IsMetaPath("Assets/Crate.png"));
	CHECK_FALSE(AssetMetadata::IsMetaPath(".meta"));

	// Missing members stay empty; problems are errors, unknown members warnings.
	const Result<AssetMetadata> bare = AssetMetadata::Parse(R"({"Version": 1})", "x.meta", report);
	REQUIRE(bare.HasValue());
	CHECK_FALSE(bare->Handle.IsValid());
	CHECK(bare->Type == AssetType::None);
	const Result<AssetMetadata> extra = AssetMetadata::Parse(R"({"Version": 1, "Colour": 3})", "x.meta", report);
	REQUIRE(extra.HasValue());
	CHECK(report.GetWarnings().size() == 1);

	const auto errorOf = [&](const char* json) {
		LoadReport ignored;
		const Result<AssetMetadata> result = AssetMetadata::Parse(json, "x.meta", ignored);
		REQUIRE_FALSE(result.HasValue());
		return result.GetError().Message;
	};
	CHECK(errorOf("{").find("x.meta: parse error") == 0);
	CHECK(errorOf("{}").find("no \"Version\"") != std::string::npos);
	CHECK(errorOf(R"({"Version": 9})").find("newer Gambit") != std::string::npos);
	CHECK(errorOf(R"({"Version": 1, "Handle": 5})").find("/Handle: expected a handle string") != std::string::npos);
	CHECK(errorOf(R"({"Version": 1, "Handle": "nope"})").find("/Handle") != std::string::npos);
	CHECK(
		errorOf(R"({"Version": 1, "Type": "Spaceship"})").find("/Type: expected the name of an asset type")
		!= std::string::npos);
	CHECK(
		errorOf(R"({"Version": 1, "ImportSettings": []})").find("/ImportSettings: expected an object")
		!= std::string::npos);
}

TEST_CASE("Asset: the registry keeps handles, paths and types in step")
{
	AssetRegistry registry;
	REQUIRE(registry.Add({ UUID(1), "Assets/B.png", AssetType::Texture }).HasValue());
	REQUIRE(registry.Add({ UUID(2), "Assets/A.glb", AssetType::Mesh }).HasValue());
	REQUIRE(registry.Add({ UUID(3), "Data/T.csv", AssetType::DataTable }).HasValue());
	CHECK(registry.GetCount() == 3);
	CHECK(registry.Contains(UUID(2)));
	CHECK(registry.Find(UUID(1))->Path == "Assets/B.png");
	CHECK(registry.FindByPath("Assets/A.glb")->Handle == UUID(2));
	CHECK(registry.FindByPath("Assets/Missing.png") == nullptr);
	CHECK(registry.Find(UUID(99)) == nullptr);

	// Listings are sorted by path, optionally for one type.
	const std::vector<AssetRecord> all = registry.List();
	REQUIRE(all.size() == 3);
	CHECK(all[0].Path == "Assets/A.glb");
	CHECK(all[1].Path == "Assets/B.png");
	CHECK(all[2].Path == "Data/T.csv");
	CHECK(registry.List(AssetType::Texture).size() == 1);
	CHECK(registry.List(AssetType::Audio).empty());

	// Refused: duplicates, nulls, bad paths.
	CHECK_FALSE(registry.Add({ UUID(1), "Assets/C.png", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(4), "Assets/B.png", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(), "Assets/C.png", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(5), "Assets/C.png", AssetType::None }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(5), "", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(5), "/abs/C.png", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(5), "Assets/../C.png", AssetType::Texture }).HasValue());
	CHECK_FALSE(registry.Add({ UUID(5), "Assets\\C.png", AssetType::Texture }).HasValue());
	CHECK(registry.GetCount() == 3);

	CHECK(registry.Remove(UUID(2)));
	CHECK_FALSE(registry.Remove(UUID(2)));
	CHECK(registry.GetCount() == 2);
	registry.Clear();
	CHECK(registry.GetCount() == 0);

	// Generated handles are valid and unused.
	REQUIRE(registry.Add({ UUID(8), "Assets/D.png", AssetType::Texture }).HasValue());
	for (int index = 0; index < 100; ++index)
	{
		const AssetHandle handle = registry.GenerateHandle();
		CHECK(handle.IsValid());
		CHECK_FALSE(registry.Contains(handle));
	}
}

TEST_CASE("Asset: a scan registers assets and creates the .meta files they lack")
{
	TempDirectory directory;
	directory.WriteFile("Assets/Crate.png", "png");
	directory.WriteFile("Assets/Sub/Barrel.glb", "glb");
	directory.WriteFile("Assets/readme.txt", "not an asset");
	directory.WriteFile("Scenes/Main.gscene", "{}");
	directory.WriteFile("Data/Weapons.csv", "Id\n");
	directory.WriteFile("Other/Ignored.png", "outside the asset directories");

	AssetRegistry registry;
	const ScanReport report = registry.Scan(directory.GetVFS(), Directories);
	CHECK(report.AssetCount == 4);
	CHECK(report.CreatedMetas == 4);
	CHECK(report.Issues.empty());
	CHECK(registry.GetCount() == 4);
	CHECK(registry.FindByPath("Assets/Other.png") == nullptr);
	CHECK(registry.FindByPath("Other/Ignored.png") == nullptr);
	CHECK(registry.FindByPath("Assets/readme.txt") == nullptr);
	CHECK_FALSE(directory.Exists("Assets/readme.txt.meta"));
	CHECK_FALSE(directory.Exists("Other/Ignored.png.meta"));

	const AssetRecord* crate = registry.FindByPath("Assets/Crate.png");
	REQUIRE(crate != nullptr);
	CHECK(crate->Type == AssetType::Texture);
	CHECK(crate->Handle.IsValid());
	CHECK(registry.FindByPath("Assets/Sub/Barrel.glb")->Type == AssetType::Mesh);
	CHECK(registry.FindByPath("Scenes/Main.gscene")->Type == AssetType::Scene);
	CHECK(registry.FindByPath("Data/Weapons.csv")->Type == AssetType::DataTable);

	// The .meta file holds the handle, type and (empty) settings.
	LoadReport parseReport;
	const Result<AssetMetadata> meta =
		AssetMetadata::Parse(directory.ReadFile("Assets/Crate.png.meta"), "Assets/Crate.png.meta", parseReport);
	REQUIRE(meta.HasValue());
	CHECK(meta->Handle == crate->Handle);
	CHECK(meta->Type == AssetType::Texture);
	CHECK(meta->ImportSettings == Json::object());

	// Scanning again finds the same handles and writes nothing.
	AssetRegistry again;
	const ScanReport second = again.Scan(directory.GetVFS(), Directories);
	CHECK(second.AssetCount == 4);
	CHECK(second.CreatedMetas == 0);
	CHECK(second.Issues.empty());
	for (const AssetRecord& record : registry.List())
	{
		CHECK(again.FindByPath(record.Path)->Handle == record.Handle);
	}

	// A missing directory is not an error; directories overlapping do not duplicate assets.
	AssetRegistry overlapping;
	const ScanReport third =
		overlapping.Scan(directory.GetVFS(), std::vector<std::string>{ "Assets", "Assets/Sub", "Nowhere" });
	CHECK(third.AssetCount == 2);
}

TEST_CASE("Asset: a scan keeps the handles and settings of existing .meta files")
{
	TempDirectory directory;
	directory.WriteFile("Assets/Crate.png", "png");
	directory.WriteFile("Assets/Crate.png.meta", MetaText(0x111, "Texture", R"({"Srgb": false})"));
	AssetRegistry registry;
	const ScanReport report = registry.Scan(directory.GetVFS(), Directories);
	CHECK(report.CreatedMetas == 0);
	CHECK(report.Issues.empty());
	CHECK(registry.FindByPath("Assets/Crate.png")->Handle == UUID(0x111));
	CHECK(registry.Find(UUID(0x111))->Path == "Assets/Crate.png");
	// Nothing was rewritten.
	CHECK(directory.ReadFile("Assets/Crate.png.meta") == MetaText(0x111, "Texture", R"({"Srgb": false})"));
}

TEST_CASE("Asset: a scan reports duplicate handles and keeps the first asset in path order")
{
	TempDirectory directory;
	directory.WriteFile("Assets/A.png", "a");
	directory.WriteFile("Assets/A.png.meta", MetaText(0x5, "Texture"));
	directory.WriteFile("Assets/B.png", "b"); // a copy of A together with its .meta file
	directory.WriteFile("Assets/B.png.meta", MetaText(0x5, "Texture"));
	directory.WriteFile("Assets/C.png", "c");

	ScopedLogCapture log;
	AssetRegistry registry;
	const ScanReport report = registry.Scan(directory.GetVFS(), Directories);
	CHECK(report.AssetCount == 2);
	REQUIRE(report.Count(ScanIssueKind::DuplicateHandle) == 1);
	const ScanIssue& issue = report.Issues[0];
	CHECK(issue.Path == "Assets/B.png");
	CHECK_FALSE(issue.Fixed);
	CHECK(issue.Message.find("already used by 'Assets/A.png'") != std::string::npos);
	CHECK(registry.Find(UUID(0x5))->Path == "Assets/A.png");
	CHECK(registry.FindByPath("Assets/B.png") == nullptr);
	CHECK(registry.FindByPath("Assets/C.png") != nullptr);
	CHECK(log.Contains(LogLevel::Warn, "Assets/B.png"));
	// The files are not touched: fixing duplicates is the user's decision.
	CHECK(directory.ReadFile("Assets/B.png.meta") == MetaText(0x5, "Texture"));
}

TEST_CASE("Asset: a scan reports orphaned, unreadable and inconsistent .meta files")
{
	TempDirectory directory;
	directory.WriteFile("Assets/Gone.png.meta", MetaText(0x1, "Texture"));  // its asset was deleted
	directory.WriteFile("Assets/notes.txt.meta", MetaText(0x2, "Texture")); // not an asset file
	directory.WriteFile("Assets/Broken.png", "x");
	directory.WriteFile("Assets/Broken.png.meta", "{ not json");
	directory.WriteFile("Assets/Wrong.png", "x");
	directory.WriteFile("Assets/Wrong.png.meta", MetaText(0x3, "Mesh")); // says Mesh, is a Texture
	directory.WriteFile("Assets/NoHandle.png", "x");
	directory.WriteFile(
		"Assets/NoHandle.png.meta", R"({"Version": 1, "Type": "Texture", "ImportSettings": {"Keep": 1}})");
	directory.WriteFile("Assets/notes.txt", "x");

	ScopedLogCapture log;
	AssetRegistry registry;
	const ScanReport report = registry.Scan(directory.GetVFS(), Directories);
	CHECK(report.Count(ScanIssueKind::OrphanedMeta) == 2);
	CHECK(report.Count(ScanIssueKind::InvalidMeta) == 1);
	CHECK(report.Count(ScanIssueKind::TypeMismatch) == 1);
	CHECK(report.Count(ScanIssueKind::MissingHandle) == 1);
	CHECK(log.Contains(LogLevel::Warn, "Assets/Gone.png.meta"));

	// The unreadable one is not registered and not overwritten.
	CHECK(registry.FindByPath("Assets/Broken.png") == nullptr);
	CHECK(directory.ReadFile("Assets/Broken.png.meta") == "{ not json");
	// A wrong type is reported and the file extension wins.
	CHECK(registry.FindByPath("Assets/Wrong.png")->Type == AssetType::Texture);
	CHECK(registry.FindByPath("Assets/Wrong.png")->Handle == UUID(0x3));
	// A .meta file without a handle gets one and keeps its settings.
	const AssetRecord* repaired = registry.FindByPath("Assets/NoHandle.png");
	REQUIRE(repaired != nullptr);
	LoadReport parseReport;
	const Result<AssetMetadata> meta =
		AssetMetadata::Parse(directory.ReadFile("Assets/NoHandle.png.meta"), "meta", parseReport);
	REQUIRE(meta.HasValue());
	CHECK(meta->Handle == repaired->Handle);
	CHECK(meta->ImportSettings == Json::parse(R"({"Keep": 1})"));
	for (const ScanIssue& issue : report.Issues)
	{
		CHECK((issue.Kind == ScanIssueKind::MissingHandle) == issue.Fixed);
	}
	CHECK(ToString(ScanIssueKind::OrphanedMeta) == "OrphanedMeta");
}

TEST_CASE("Asset: scanning read-only content reports instead of writing")
{
	TempDirectory directory;
	directory.WriteFile("Assets/NoMeta.png", "x");
	directory.WriteFile("Assets/Known.png", "x");
	directory.WriteFile("Assets/Known.png.meta", MetaText(0x9, "Texture"));
	directory.WriteFile("Assets/NoHandle.png", "x");
	directory.WriteFile("Assets/NoHandle.png.meta", R"({"Version": 1})");

	VFS readOnly;
	REQUIRE(readOnly.Mount("", CreateRef<DirectoryMount>(directory.GetPath(), false)).HasValue());
	AssetRegistry registry;

	ScanOptions options;
	options.WriteMetas = false;
	const ScanReport noWrite = registry.Scan(directory.GetVFS(), Directories, options);
	CHECK(noWrite.CreatedMetas == 0);
	CHECK(noWrite.Count(ScanIssueKind::MissingMeta) == 1);
	CHECK(noWrite.Count(ScanIssueKind::MissingHandle) == 1);
	CHECK(registry.GetCount() == 1);
	CHECK(registry.Find(UUID(0x9)) != nullptr);
	CHECK_FALSE(directory.Exists("Assets/NoMeta.png.meta"));

	// With writing wanted but impossible the result is the same.
	AssetRegistry attempted;
	const ScanReport failed = attempted.Scan(readOnly, Directories);
	CHECK(failed.CreatedMetas == 0);
	CHECK(failed.Count(ScanIssueKind::MissingMeta) == 1);
	CHECK(failed.Count(ScanIssueKind::MissingHandle) == 1);
	CHECK_FALSE(failed.Issues[0].Fixed);
	CHECK(attempted.GetCount() == 1);
}
