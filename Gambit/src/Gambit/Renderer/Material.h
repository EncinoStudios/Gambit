#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Asset/IAssetImporter.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Core/VFS.h"
#include "Gambit/Math/Math.h"
#include "Gambit/Serialization/Json.h"
#include "Gambit/Serialization/Migration.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace Gambit
{
	class AssetManager;
	class TypeRegistry;

	// How a material's alpha is used (glTF 2.0 alphaMode).
	enum class AlphaMode : uint8_t
	{
		Opaque, // alpha is ignored
		Mask,   // fragments with alpha below AlphaCutoff are discarded, the rest are opaque
		Blend   // alpha blended over what is behind, drawn after the opaque geometry, back to front
	};

	// The parameters of the metallic-roughness PBR material (glTF 2.0 core plus KHR_materials_emissive_strength): plain
	// data, reflected as the struct "Material" so that the .gmat file, the inspector and scripts handle it generically
	// (D-022). Factors multiply their textures; texture handles may be null (then the factor alone counts).
	struct MaterialData
	{
		glm::vec4 BaseColor{ 1.0f }; // linear RGBA
		UUID BaseColorTexture;       // sRGB
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		UUID MetallicRoughnessTexture; // linear: roughness in G, metallic in B
		UUID NormalTexture;            // linear, tangent space (MikkTSpace, +Y up the image)
		float NormalScale = 1.0f;
		UUID OcclusionTexture; // linear, R
		float OcclusionStrength = 1.0f;
		glm::vec3 EmissiveColor{ 0.0f }; // linear
		UUID EmissiveTexture;            // sRGB
		float EmissiveStrength = 1.0f;   // multiplies EmissiveColor (nits per unit, for physical light units)
		AlphaMode Alpha = AlphaMode::Opaque;
		float AlphaCutoff = 0.5f;
		bool DoubleSided = false;
		glm::vec2 UVTiling{ 1.0f }; // UV' = UV * UVTiling + UVOffset, for every texture
		glm::vec2 UVOffset{ 0.0f };

		friend bool operator==(const MaterialData&, const MaterialData&) = default;
	};

	// A material asset (D-014): a .gmat file, a material of a glTF model (a sub-asset, D-038) or the built-in default
	// material. Edits through SetData bump the version, which tells the renderer to rebuild its GPU copy.
	class Material final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Material;
		// The built-in default material (D-039): white, dielectric, roughness 0.5. Meshes without a material use it.
		static constexpr AssetHandle DefaultHandle{ 0x301 };

		explicit Material(MaterialData data = {});

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }
		// The textures.
		[[nodiscard]] std::vector<AssetHandle> GetDependencies() const override;

		[[nodiscard]] const MaterialData& GetData() const noexcept { return m_Data; }
		void SetData(const MaterialData& data);
		// Starts at 1 and grows with every SetData.
		[[nodiscard]] uint64_t GetVersion() const noexcept { return m_Version; }

	private:
		MaterialData m_Data;
		uint64_t m_Version = 1;
	};

	// The .gmat file format (D-025): the reflected struct "Material" after "Version":
	//   { "Version": 1, "BaseColor": [1.0, 1.0, 1.0, 1.0], "BaseColorTexture": "0x...", "Metallic": 0.0, ... }
	// Missing fields take their defaults, unknown ones are warnings, malformed values errors with JSON pointers.
	class MaterialSerializer
	{
	public:
		static constexpr uint32_t FormatVersion = 1;
		static constexpr std::string_view FileExtension = ".gmat";

		[[nodiscard]] static const MigrationTable& GetMigrations();
		[[nodiscard]] static Result<MaterialData>
		Parse(const TypeRegistry& types, std::string_view text, std::string_view source, LoadReport& report);
		[[nodiscard]] static Result<std::string> Serialize(const TypeRegistry& types, const MaterialData& data);
		[[nodiscard]] static Result<void>
		Save(const TypeRegistry& types, const MaterialData& data, VFS& vfs, std::string_view path);
	};

	// Loads .gmat files as Material.
	class MaterialImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Material; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};

	// Registers the default material as a built-in asset of `manager` (RegisterEngineAssets calls it).
	void RegisterDefaultMaterial(AssetManager& manager);

	// Registers the enum "AlphaMode" and the struct "Material".
	void RegisterMaterialTypes(TypeRegistry& registry);
}
