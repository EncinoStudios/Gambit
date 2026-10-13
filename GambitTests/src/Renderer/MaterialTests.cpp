#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"
#include "TestSupport.h"

#include "Gambit/Renderer/Material.h"

#include <string>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	MaterialData MakeEveryFieldSet()
	{
		MaterialData data;
		data.BaseColor = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);
		data.BaseColorTexture = UUID(0x11);
		data.Metallic = 0.75f;
		data.Roughness = 0.125f;
		data.MetallicRoughnessTexture = UUID(0x12);
		data.NormalTexture = UUID(0x13);
		data.NormalScale = 0.5f;
		data.OcclusionTexture = UUID(0x14);
		data.OcclusionStrength = 0.25f;
		data.EmissiveColor = glm::vec3(1.0f, 0.5f, 0.0f);
		data.EmissiveTexture = UUID(0x15);
		data.EmissiveStrength = 20.0f;
		data.Alpha = AlphaMode::Blend;
		data.AlphaCutoff = 0.3f;
		data.DoubleSided = true;
		data.UVTiling = glm::vec2(2.0f, 3.0f);
		data.UVOffset = glm::vec2(0.5f, -0.25f);
		return data;
	}
}

TEST_CASE("Renderer: materials round-trip through .gmat files")
{
	const EngineTypes types;
	const MaterialData data = MakeEveryFieldSet();
	const Result<std::string> text = MaterialSerializer::Serialize(types.Registry, data);
	REQUIRE(text.HasValue());
	// Readable, stable JSON: the version first, enums by name, handles as hex strings.
	CHECK(text->starts_with("{\n  \"Version\": 1,\n  \"BaseColor\": [\n"));
	CHECK(text->find("\"AlphaMode\": \"Blend\"") != std::string::npos);
	CHECK(text->find("\"BaseColorTexture\": \"0x0000000000000011\"") != std::string::npos);
	CHECK(text->find("\"Roughness\": 0.125") != std::string::npos);

	LoadReport report;
	const Result<MaterialData> parsed = MaterialSerializer::Parse(types.Registry, *text, "Wood.gmat", report);
	REQUIRE(parsed.HasValue());
	CHECK(*parsed == data);
	CHECK(report.IsEmpty());
	const Result<std::string> again = MaterialSerializer::Serialize(types.Registry, *parsed);
	REQUIRE(again.HasValue());
	CHECK(*again == *text);

	// Missing members keep their defaults, unknown ones are warnings.
	LoadReport partialReport;
	const Result<MaterialData> partial = MaterialSerializer::Parse(
		types.Registry, R"({ "Version": 1, "Metallic": 1.0, "Sheen": 0.5 })", "Metal.gmat", partialReport);
	REQUIRE(partial.HasValue());
	CHECK(partial->Metallic == 1.0f);
	CHECK(partial->Roughness == MaterialData{}.Roughness);
	REQUIRE(partialReport.GetWarnings().size() == 1);
	CHECK(partialReport.GetWarnings()[0].Path == "/Sheen");

	const auto failsWith = [&](std::string_view json, std::string_view text) {
		LoadReport ignored;
		const Result<MaterialData> result = MaterialSerializer::Parse(types.Registry, json, "Bad.gmat", ignored);
		REQUIRE_FALSE(result.HasValue());
		CHECK_MESSAGE(result.GetError().Message.find(text) != std::string::npos, result.GetError().Message);
	};
	failsWith(R"({ "Version": 1, "AlphaMode": "Glass" })", "/AlphaMode");
	failsWith(R"({ "Version": 1, "Roughness": "rough" })", "/Roughness");
	failsWith(R"({ "Version": 1, "Metallic": "NaN" })", "finite");
	failsWith(R"({ "Version": 7 })", "Bad.gmat");
	failsWith("[", "Bad.gmat");

	MaterialData infinite;
	infinite.EmissiveStrength = std::numeric_limits<float>::infinity();
	CHECK_FALSE(MaterialSerializer::Serialize(types.Registry, infinite).HasValue());
}

TEST_CASE("Renderer: .gmat files load as material assets that list their textures")
{
	RendererProject project;
	const MaterialData data = MakeEveryFieldSet();
	REQUIRE(MaterialSerializer::Save(project.Types.Registry, data, project.Directory.GetVFS(), "Assets/Wood.gmat")
	            .HasValue());
	project.Assets.Scan();
	const AssetHandle handle = project.Assets.GetHandle("Assets/Wood.gmat");
	REQUIRE(project.Assets.GetType(handle) == AssetType::Material);
	const Ref<Material> material = project.Assets.GetAsset<Material>(handle);
	REQUIRE(material != nullptr);
	CHECK(material->GetData() == data);
	CHECK(material->GetVersion() == 1);
	CHECK(
		material->GetDependencies()
		== std::vector<AssetHandle>{ UUID(0x11), UUID(0x12), UUID(0x13), UUID(0x14), UUID(0x15) });

	// Edits bump the version, which tells the renderer to rebuild its GPU copy.
	MaterialData edited = material->GetData();
	edited.Roughness = 0.9f;
	material->SetData(edited);
	CHECK(material->GetVersion() == 2);
	CHECK(material->GetData().Roughness == 0.9f);
}

TEST_CASE("Renderer: the Material struct is reflected for the inspector and tools")
{
	const EngineTypes types;
	const Ref<const StructInfo> info = types.Registry.FindStruct<MaterialData>();
	REQUIRE(info != nullptr);
	CHECK(info->GetName() == "Material");
	REQUIRE(info->GetFields().size() == 17);
	const FieldInfo* baseColor = info->FindField("BaseColor");
	REQUIRE(baseColor != nullptr);
	CHECK(baseColor->Type == FieldType::Color4);
	const FieldInfo* texture = info->FindField("NormalTexture");
	REQUIRE(texture != nullptr);
	CHECK(texture->Type == FieldType::AssetHandle);
	CHECK(texture->Attributes.AssetType == "Texture");
	const FieldInfo* alpha = info->FindField("AlphaMode");
	REQUIRE(alpha != nullptr);
	CHECK(alpha->Type == FieldType::Enum);
}
