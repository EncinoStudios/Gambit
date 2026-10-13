#include "gbpch.h"

#include "Gambit/Renderer/Material.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/FieldSerializer.h"

#include <set>

namespace Gambit
{
	namespace
	{
		constexpr const char* FormatName = "material";

		const StructInfo& GetMaterialInfo(const TypeRegistry& types)
		{
			const Ref<const StructInfo> info = types.FindStruct<MaterialData>();
			GB_CORE_VERIFY(info != nullptr, "the TypeRegistry lacks the renderer types (RegisterEngineTypes)");
			return *info;
		}

		Result<void> Validate(const MaterialData& data)
		{
			const float values[] = { data.BaseColor.r,      data.BaseColor.g,     data.BaseColor.b,
				                     data.BaseColor.a,      data.EmissiveColor.r, data.EmissiveColor.g,
				                     data.EmissiveColor.b,  data.UVTiling.x,      data.UVTiling.y,
				                     data.UVOffset.x,       data.UVOffset.y,      data.Metallic,
				                     data.Roughness,        data.NormalScale,     data.OcclusionStrength,
				                     data.EmissiveStrength, data.AlphaCutoff };
			if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); }))
			{
				return Error("material values must be finite numbers");
			}
			return {};
		}
	}

	Material::Material(MaterialData data)
		: m_Data(std::move(data))
	{}

	void Material::SetData(const MaterialData& data)
	{
		m_Data = data;
		++m_Version;
	}

	std::vector<AssetHandle> Material::GetDependencies() const
	{
		std::set<AssetHandle> handles;
		for (const AssetHandle handle : { m_Data.BaseColorTexture, m_Data.MetallicRoughnessTexture,
		                                  m_Data.NormalTexture, m_Data.OcclusionTexture, m_Data.EmissiveTexture })
		{
			if (handle.IsValid())
			{
				handles.insert(handle);
			}
		}
		return { handles.begin(), handles.end() };
	}

	const MigrationTable& MaterialSerializer::GetMigrations()
	{
		return GetNoMigrations();
	}

	Result<MaterialData> MaterialSerializer::Parse(
		const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report)
	{
		Result<Json> document = ParseJson(text, source);
		if (!document)
		{
			return document.GetError();
		}
		if (Result<void> upgraded = GetMigrations().Upgrade(*document, FormatVersion, FormatName); !upgraded)
		{
			return AddSource(source, upgraded.GetError());
		}
		document->erase("Version");
		MaterialData data;
		if (Result<void> read = StructFromJson(GetMaterialInfo(types), *document, &data, "", report); !read)
		{
			return AddSource(source, read.GetError());
		}
		if (Result<void> valid = Validate(data); !valid)
		{
			return AddSource(source, valid.GetError());
		}
		return data;
	}

	Result<std::string> MaterialSerializer::Serialize(const TypeRegistry& types, const MaterialData& data)
	{
		if (Result<void> valid = Validate(data); !valid)
		{
			return valid.GetError();
		}
		Result<Json> members = StructToJson(GetMaterialInfo(types), &data);
		if (!members)
		{
			return members.GetError();
		}
		Json document = Json::object();
		document["Version"] = FormatVersion;
		for (const auto& [key, member] : members->items())
		{
			document[key] = member;
		}
		return DumpJson(document);
	}

	Result<void>
	MaterialSerializer::Save(const TypeRegistry& types, const MaterialData& data, VFS& vfs, std::string_view path)
	{
		Result<std::string> text = Serialize(types, data);
		if (!text)
		{
			return AddSource(path, text.GetError());
		}
		return vfs.WriteText(path, *text);
	}

	Result<Ref<Asset>> MaterialImporter::Load(const AssetLoadContext& context) const
	{
		Result<std::string> text = context.Vfs.ReadText(context.Record.Path);
		if (!text)
		{
			return text.GetError();
		}
		Result<MaterialData> data =
			MaterialSerializer::Parse(context.Types, *text, context.Record.Path, context.Report);
		if (!data)
		{
			return data.GetError();
		}
		return Ref<Asset>(CreateRef<Material>(std::move(*data)));
	}

	void RegisterDefaultMaterial(AssetManager& manager)
	{
		manager.RegisterBuiltinAsset(Material::DefaultHandle, AssetType::Material, "Materials/Default", [] {
			return Ref<Asset>(CreateRef<Material>());
		});
	}

	void RegisterMaterialTypes(TypeRegistry& registry)
	{
		registry.RegisterEnum<AlphaMode>("AlphaMode")
			.Value("Opaque", AlphaMode::Opaque)
			.Value("Mask", AlphaMode::Mask)
			.Value("Blend", AlphaMode::Blend);

		const Attr::AssetType texture{ "Texture" };
		const Attr::Range unit{ 0.0, 1.0 };
		registry.RegisterStruct<MaterialData>("Material")
			.Field("BaseColor", &MaterialData::BaseColor, Attr::Color{}, Attr::Tooltip{ "Linear RGBA" })
			.Field("BaseColorTexture", &MaterialData::BaseColorTexture, texture)
			.Field("Metallic", &MaterialData::Metallic, unit)
			.Field("Roughness", &MaterialData::Roughness, unit)
			.Field(
				"MetallicRoughnessTexture", &MaterialData::MetallicRoughnessTexture, texture,
				Attr::Tooltip{ "Roughness in green, metallic in blue (glTF)" })
			.Field("NormalTexture", &MaterialData::NormalTexture, texture)
			.Field("NormalScale", &MaterialData::NormalScale, Attr::Range{ 0.0, 2.0 })
			.Field("OcclusionTexture", &MaterialData::OcclusionTexture, texture, Attr::Tooltip{ "Red channel" })
			.Field("OcclusionStrength", &MaterialData::OcclusionStrength, unit)
			.Field("EmissiveColor", &MaterialData::EmissiveColor, Attr::Color{})
			.Field("EmissiveTexture", &MaterialData::EmissiveTexture, texture)
			.Field(
				"EmissiveStrength", &MaterialData::EmissiveStrength, Attr::Range{ 0.0, 1000.0 },
				Attr::Tooltip{ "Multiplies the emissive color (KHR_materials_emissive_strength)" })
			.Field("AlphaMode", &MaterialData::Alpha)
			.Field("AlphaCutoff", &MaterialData::AlphaCutoff, unit, Attr::Tooltip{ "Mask mode only" })
			.Field("DoubleSided", &MaterialData::DoubleSided, Attr::Tooltip{ "Draw back faces, with flipped normals" })
			.Field("UVTiling", &MaterialData::UVTiling)
			.Field("UVOffset", &MaterialData::UVOffset);
	}
}
