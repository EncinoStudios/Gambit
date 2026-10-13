#include "gbpch.h"

#include "Gambit/Renderer/Texture.h"

#include "Gambit/Asset/AssetManager.h"
#include "Gambit/Reflection/TypeRegistry.h"
#include "Gambit/Serialization/FieldSerializer.h"

#include <stb_image.h>

#include <limits>

namespace Gambit
{
	namespace
	{
		const StructInfo& GetSettingsInfo(const TypeRegistry& types)
		{
			const Ref<const StructInfo> info = types.FindStruct<TextureImportSettings>();
			GB_CORE_VERIFY(info != nullptr, "the TypeRegistry lacks the renderer types (RegisterEngineTypes)");
			return *info;
		}
	}

	uint32_t ComputeMipLevelCount(uint32_t width, uint32_t height) noexcept
	{
		const uint32_t largest = std::max(std::max(width, height), 1u);
		return static_cast<uint32_t>(std::bit_width(largest));
	}

	Texture2D::Texture2D(Image image, const TextureImportSettings& settings)
		: m_Image(std::move(image)), m_Settings(settings)
	{
		GB_CORE_VERIFY(!m_Image.IsEmpty(), "a texture needs an image");
		if (IsHdr())
		{
			m_Settings.Srgb = false;
		}
		m_MipLevelCount = m_Settings.GenerateMips ? ComputeMipLevelCount(m_Image.GetWidth(), m_Image.GetHeight()) : 1;
	}

	std::string_view ToString(DefaultTexture texture) noexcept
	{
		switch (texture)
		{
			case DefaultTexture::White:
				return "White";
			case DefaultTexture::Black:
				return "Black";
			case DefaultTexture::FlatNormal:
				return "FlatNormal";
			case DefaultTexture::Checker:
				return "Checker";
		}
		return "Unknown";
	}

	AssetHandle GetDefaultTextureHandle(DefaultTexture texture) noexcept
	{
		return AssetHandle(0x201 + static_cast<uint64_t>(texture));
	}

	Ref<Texture2D> CreateDefaultTexture(DefaultTexture texture)
	{
		const auto solid = [](const glm::vec4& color, bool srgb) {
			Image image(1, 1, ImageFormat::RGBA8);
			image.SetPixel(0, 0, color);
			TextureImportSettings settings;
			settings.Srgb = srgb;
			settings.GenerateMips = false;
			return CreateRef<Texture2D>(std::move(image), settings);
		};
		switch (texture)
		{
			case DefaultTexture::White:
				return solid(glm::vec4(1.0f), true);
			case DefaultTexture::Black:
				return solid(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), true);
			case DefaultTexture::FlatNormal:
				return solid(glm::vec4(128.0f / 255.0f, 128.0f / 255.0f, 1.0f, 1.0f), false);
			case DefaultTexture::Checker:
				break;
		}
		constexpr uint32_t Size = 64;
		constexpr uint32_t Square = 8;
		Image image(Size, Size, ImageFormat::RGBA8);
		for (uint32_t y = 0; y < Size; ++y)
		{
			for (uint32_t x = 0; x < Size; ++x)
			{
				const bool light = ((x / Square) + (y / Square)) % 2 == 0;
				const float value = light ? LinearToSrgb(0.6f) : LinearToSrgb(0.15f);
				image.SetPixel(x, y, glm::vec4(value, value, value, 1.0f));
			}
		}
		return CreateRef<Texture2D>(std::move(image), TextureImportSettings{});
	}

	void RegisterDefaultTextures(AssetManager& manager)
	{
		for (const DefaultTexture texture : AllDefaultTextures)
		{
			manager.RegisterBuiltinAsset(
				GetDefaultTextureHandle(texture), AssetType::Texture, std::format("Textures/{}", ToString(texture)),
				[texture] { return Ref<Asset>(CreateDefaultTexture(texture)); });
		}
	}

	Result<Image> DecodeTextureImage(BufferView data)
	{
		if (data.Size() > static_cast<size_t>(std::numeric_limits<int>::max()))
		{
			return Error("the image file is too large");
		}
		const auto* bytes = reinterpret_cast<const stbi_uc*>(data.Data());
		const int length = static_cast<int>(data.Size());
		int width = 0;
		int height = 0;
		int channels = 0;
		if (stbi_info_from_memory(bytes, length, &width, &height, &channels) == 0)
		{
			return Error::Format("not a readable image (PNG, JPEG, TGA, BMP or HDR): {}", stbi_failure_reason());
		}
		if (stbi_is_hdr_from_memory(bytes, length) != 0)
		{
			float* pixels = stbi_loadf_from_memory(bytes, length, &width, &height, &channels, 4);
			if (pixels == nullptr)
			{
				return Error::Format("cannot decode the HDR image: {}", stbi_failure_reason());
			}
			const size_t size = size_t(width) * size_t(height) * 4 * sizeof(float);
			std::vector<std::byte> copy(size);
			std::memcpy(copy.data(), pixels, size);
			stbi_image_free(pixels);
			return Image::Create(
				static_cast<uint32_t>(width), static_cast<uint32_t>(height), ImageFormat::RGBA32F, std::move(copy));
		}
		stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
		if (pixels == nullptr)
		{
			return Error::Format("cannot decode the image: {}", stbi_failure_reason());
		}
		const size_t size = size_t(width) * size_t(height) * 4;
		std::vector<std::byte> copy(size);
		std::memcpy(copy.data(), pixels, size);
		stbi_image_free(pixels);
		return Image::Create(
			static_cast<uint32_t>(width), static_cast<uint32_t>(height), ImageFormat::RGBA8, std::move(copy));
	}

	Result<TextureImportSettings>
	ParseTextureImportSettings(const TypeRegistry& types, const Json& settings, LoadReport& report)
	{
		TextureImportSettings parsed;
		if (Result<void> read = StructFromJson(GetSettingsInfo(types), settings, &parsed, "/ImportSettings", report);
		    !read)
		{
			return read.GetError();
		}
		return parsed;
	}

	Result<Json> SerializeTextureImportSettings(const TypeRegistry& types, const TextureImportSettings& settings)
	{
		return StructToJson(GetSettingsInfo(types), &settings, "/ImportSettings");
	}

	Result<Ref<Asset>> TextureImporter::Load(const AssetLoadContext& context) const
	{
		Result<TextureImportSettings> settings =
			ParseTextureImportSettings(context.Types, context.Metadata.ImportSettings, context.Report);
		if (!settings)
		{
			return AddSource(AssetMetadata::GetMetaPath(context.Record.Path), settings.GetError());
		}
		Result<Buffer> bytes = context.Vfs.ReadBinary(context.Record.Path);
		if (!bytes)
		{
			return bytes.GetError();
		}
		Result<Image> image = DecodeTextureImage(*bytes);
		if (!image)
		{
			return image.GetError();
		}
		return Ref<Asset>(CreateRef<Texture2D>(std::move(*image), *settings));
	}

	void RegisterTextureTypes(TypeRegistry& registry)
	{
		registry.RegisterEnum<TextureFilter>("TextureFilter")
			.Value("Linear", TextureFilter::Linear)
			.Value("Nearest", TextureFilter::Nearest);
		registry.RegisterEnum<TextureWrap>("TextureWrap")
			.Value("Repeat", TextureWrap::Repeat)
			.Value("ClampToEdge", TextureWrap::ClampToEdge)
			.Value("MirroredRepeat", TextureWrap::MirroredRepeat);
		registry.RegisterStruct<TextureImportSettings>("TextureImportSettings")
			.Field(
				"Srgb", &TextureImportSettings::Srgb,
				Attr::Tooltip{ "Color data (albedo, emissive); off for normal maps, roughness and masks" })
			.Field(
				"GenerateMips", &TextureImportSettings::GenerateMips,
				Attr::Tooltip{ "A full mip chain for minification; off for UI and pixel art" })
			.Field("Filter", &TextureImportSettings::Filter)
			.Field("WrapU", &TextureImportSettings::WrapU)
			.Field("WrapV", &TextureImportSettings::WrapV);
	}
}
