#pragma once

#include "Gambit/Asset/Asset.h"
#include "Gambit/Asset/IAssetImporter.h"
#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"
#include "Gambit/RHI/Image.h"
#include "Gambit/Serialization/Json.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace Gambit
{
	class AssetManager;
	class TypeRegistry;

	enum class TextureFilter : uint8_t
	{
		Linear, // bilinear within a mip level, linear between levels
		Nearest // the nearest texel of the nearest level (pixel art)
	};

	enum class TextureWrap : uint8_t
	{
		Repeat,
		ClampToEdge,
		MirroredRepeat
	};

	// How a texture file is imported: the ImportSettings of its .meta file, the reflected struct
	// "TextureImportSettings" (so tools edit it generically). Defaults suit color images.
	struct TextureImportSettings
	{
		bool Srgb = true;         // the RGB values are sRGB-encoded colors; false for data (normals, roughness, masks)
		bool GenerateMips = true; // a full mip chain, generated on the GPU when the texture is uploaded
		TextureFilter Filter = TextureFilter::Linear;
		TextureWrap WrapU = TextureWrap::Repeat;
		TextureWrap WrapV = TextureWrap::Repeat;

		friend bool operator==(const TextureImportSettings&, const TextureImportSettings&) = default;
	};

	// The number of levels of a full mip chain of a width x height texture: floor(log2(max(width, height))) + 1.
	[[nodiscard]] uint32_t ComputeMipLevelCount(uint32_t width, uint32_t height) noexcept;

	// A 2D texture asset (D-014): the decoded image (level 0) and how to sample it. The image is RGBA8 for 8-bit files
	// (sRGB-encoded or linear, as the settings say) and RGBA32F for HDR data (always linear). The renderer uploads it
	// and generates the other mip levels on the GPU (RenderResources).
	class Texture2D final : public Asset
	{
	public:
		static constexpr AssetType StaticType = AssetType::Texture;

		// HDR images ignore settings.Srgb (it reads as false).
		Texture2D(Image image, const TextureImportSettings& settings);

		[[nodiscard]] AssetType GetType() const noexcept override { return StaticType; }

		[[nodiscard]] const Image& GetImage() const noexcept { return m_Image; }
		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_Image.GetWidth(); }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_Image.GetHeight(); }
		[[nodiscard]] const TextureImportSettings& GetSettings() const noexcept { return m_Settings; }
		[[nodiscard]] bool IsSrgb() const noexcept { return m_Settings.Srgb; }
		[[nodiscard]] bool IsHdr() const noexcept { return m_Image.GetFormat() != ImageFormat::RGBA8; }
		// 1 without GenerateMips, the full chain with it.
		[[nodiscard]] uint32_t GetMipLevelCount() const noexcept { return m_MipLevelCount; }

	private:
		Image m_Image;
		TextureImportSettings m_Settings;
		uint32_t m_MipLevelCount = 1;
	};

	// The engine's built-in textures (D-039): what material slots without a texture sample.
	enum class DefaultTexture : uint8_t
	{
		White,      // 1x1 sRGB white: base color, emissive, metallic-roughness and occlusion factors pass unchanged
		Black,      // 1x1 sRGB black
		FlatNormal, // 1x1 linear (0.5, 0.5, 1): the unperturbed tangent-space normal
		Checker     // 64x64 sRGB checkerboard of light and dark grey squares, 8 texels wide (debugging UVs)
	};

	inline constexpr std::array<DefaultTexture, 4> AllDefaultTextures = { DefaultTexture::White, DefaultTexture::Black,
		                                                                  DefaultTexture::FlatNormal,
		                                                                  DefaultTexture::Checker };

	[[nodiscard]] std::string_view ToString(DefaultTexture texture) noexcept;
	// 0x0000000000000201 for White, then one per texture in enum order.
	[[nodiscard]] AssetHandle GetDefaultTextureHandle(DefaultTexture texture) noexcept;
	[[nodiscard]] Ref<Texture2D> CreateDefaultTexture(DefaultTexture texture);
	// Registers the default textures as built-in assets of `manager` (RegisterEngineAssets calls it).
	void RegisterDefaultTextures(AssetManager& manager);

	// Decodes an image file held in memory: PNG, JPEG, TGA and BMP (any channel count; 16-bit PNGs are reduced to 8
	// bits) as RGBA8, Radiance HDR (.hdr) as RGBA32F. Grey and RGB images gain opaque alpha.
	[[nodiscard]] Result<Image> DecodeTextureImage(BufferView data);

	// The import settings of a .meta file (missing members take their defaults, unknown ones are warnings) and back.
	[[nodiscard]] Result<TextureImportSettings>
	ParseTextureImportSettings(const TypeRegistry& types, const Json& settings, LoadReport& report);
	[[nodiscard]] Result<Json>
	SerializeTextureImportSettings(const TypeRegistry& types, const TextureImportSettings& settings);

	// Loads .png, .jpg, .jpeg, .tga and .bmp files as Texture2D, through the VFS.
	class TextureImporter final : public IAssetImporter
	{
	public:
		[[nodiscard]] AssetType GetType() const noexcept override { return AssetType::Texture; }
		[[nodiscard]] Result<Ref<Asset>> Load(const AssetLoadContext& context) const override;
	};

	// Registers the enums "TextureFilter" and "TextureWrap" and the struct "TextureImportSettings".
	void RegisterTextureTypes(TypeRegistry& registry);
}
