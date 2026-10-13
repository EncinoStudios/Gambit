#include <doctest/doctest.h>

#include "Renderer/RendererTestSupport.h"
#include "TestSupport.h"

#include "Gambit/Renderer/Texture.h"

#include <stb_image_write.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

using namespace Gambit;
using namespace Gambit::Tests;

namespace
{
	void AppendBytes(void* context, void* data, int size)
	{
		auto& bytes = *static_cast<std::string*>(context);
		bytes.append(static_cast<const char*>(data), static_cast<size_t>(size));
	}

	// A width x height image with `channels` channels whose texel (x, y) has the values PatternValue(x, y, channel).
	std::vector<uint8_t> MakePattern(int width, int height, int channels)
	{
		std::vector<uint8_t> pixels(static_cast<size_t>(width * height * channels));
		for (int y = 0; y < height; ++y)
		{
			for (int x = 0; x < width; ++x)
			{
				for (int channel = 0; channel < channels; ++channel)
				{
					pixels[static_cast<size_t>((y * width + x) * channels + channel)] =
						static_cast<uint8_t>(40 * x + 70 * y + 50 * channel);
				}
			}
		}
		return pixels;
	}

	uint8_t Level(float value)
	{
		return static_cast<uint8_t>(std::lround(value * 255.0f));
	}

	// A Radiance .hdr file of one row with flat (not run-length encoded) RGBE pixels. stb_image_write cannot encode
	// HDR to memory when it is built without stdio, so the test writes the format itself.
	std::string EncodeRadianceRow(std::span<const glm::vec3> pixels)
	{
		std::string file = std::format("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X {}\n", pixels.size());
		for (const glm::vec3& pixel : pixels)
		{
			const float largest = std::max({ pixel.r, pixel.g, pixel.b });
			if (largest < 1e-32f)
			{
				file.append(4, '\0');
				continue;
			}
			int exponent = 0;
			const float scale = std::frexp(largest, &exponent) * 256.0f / largest;
			for (int channel = 0; channel < 3; ++channel)
			{
				file.push_back(static_cast<char>(static_cast<uint8_t>(pixel[channel] * scale)));
			}
			file.push_back(static_cast<char>(static_cast<uint8_t>(exponent + 128)));
		}
		return file;
	}
}

TEST_CASE("Renderer: mip chains have floor(log2(largest side)) + 1 levels")
{
	CHECK(ComputeMipLevelCount(1, 1) == 1);
	CHECK(ComputeMipLevelCount(2, 1) == 2);
	CHECK(ComputeMipLevelCount(256, 256) == 9);
	CHECK(ComputeMipLevelCount(300, 100) == 9);
	CHECK(ComputeMipLevelCount(1, 1024) == 11);
	CHECK(ComputeMipLevelCount(0, 0) == 1);
}

TEST_CASE("Renderer: PNG, JPEG, TGA and BMP files import as textures through the VFS")
{
	RendererProject project;
	constexpr int Width = 3;
	constexpr int Height = 2;
	std::string png;
	std::string tga;
	std::string bmp;
	std::string jpg;
	const std::vector<uint8_t> rgb = MakePattern(Width, Height, 3);
	const std::vector<uint8_t> rgba = MakePattern(Width, Height, 4);
	const std::vector<uint8_t> grey(static_cast<size_t>(Width * Height), 128); // flat, so JPEG keeps it exactly
	REQUIRE(stbi_write_png_to_func(AppendBytes, &png, Width, Height, 3, rgb.data(), Width * 3) != 0);
	REQUIRE(stbi_write_tga_to_func(AppendBytes, &tga, Width, Height, 4, rgba.data()) != 0);
	REQUIRE(stbi_write_bmp_to_func(AppendBytes, &bmp, Width, Height, 3, rgb.data()) != 0);
	REQUIRE(stbi_write_jpg_to_func(AppendBytes, &jpg, Width, Height, 1, grey.data(), 100) != 0);
	project.Directory.WriteFile("Assets/Textures/Pattern.png", png);
	project.Directory.WriteFile("Assets/Textures/Pattern.tga", tga);
	project.Directory.WriteFile("Assets/Textures/Pattern.bmp", bmp);
	project.Directory.WriteFile("Assets/Textures/Grey.JPG", jpg);
	REQUIRE(project.Assets.Scan().AssetCount == 4);

	for (const char* path :
	     { "Assets/Textures/Pattern.png", "Assets/Textures/Pattern.tga", "Assets/Textures/Pattern.bmp" })
	{
		CAPTURE(path);
		const Ref<Texture2D> texture = project.Assets.GetAsset<Texture2D>(project.Assets.GetHandle(path));
		REQUIRE(texture != nullptr);
		CHECK(texture->GetPath() == path);
		CHECK(texture->GetWidth() == Width);
		CHECK(texture->GetHeight() == Height);
		CHECK(texture->GetImage().GetFormat() == ImageFormat::RGBA8);
		CHECK(texture->IsSrgb()); // the default import settings: a color image with mips
		CHECK_FALSE(texture->IsHdr());
		CHECK(texture->GetMipLevelCount() == 2);
		const bool hasAlpha = std::string_view(path).ends_with(".tga");
		for (int y = 0; y < Height; ++y)
		{
			for (int x = 0; x < Width; ++x)
			{
				const glm::vec4 pixel =
					texture->GetImage().GetPixel(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
				const std::vector<uint8_t>& source = hasAlpha ? rgba : rgb;
				const int channels = hasAlpha ? 4 : 3;
				for (int channel = 0; channel < 3; ++channel)
				{
					CHECK(Level(pixel[channel]) == source[static_cast<size_t>((y * Width + x) * channels + channel)]);
				}
				// RGB images gain opaque alpha.
				CHECK(Level(pixel.a) == (hasAlpha ? source[static_cast<size_t>((y * Width + x) * 4 + 3)] : 255));
			}
		}
	}

	const Ref<Texture2D> jpeg =
		project.Assets.GetAsset<Texture2D>(project.Assets.GetHandle("Assets/Textures/Grey.JPG"));
	REQUIRE(jpeg != nullptr);
	const glm::vec4 pixel = jpeg->GetImage().GetPixel(1, 1);
	CHECK(std::abs(int(Level(pixel.r)) - 128) <= 1);
	CHECK(pixel.r == pixel.g);
	CHECK(pixel.g == pixel.b);
	CHECK(pixel.a == 1.0f);
}

TEST_CASE("Renderer: texture import settings come from the .meta file")
{
	RendererProject project;
	std::string png;
	const std::vector<uint8_t> rgb = MakePattern(4, 4, 3);
	REQUIRE(stbi_write_png_to_func(AppendBytes, &png, 4, 4, 3, rgb.data(), 12) != 0);
	project.Directory.WriteFile("Assets/Normal.png", png);
	project.Directory.WriteFile("Assets/Broken.png", png);
	project.Directory.WriteFile(
		"Assets/Normal.png.meta",
		R"({ "Version": 1, "Handle": "0x0000000000ABC001", "Type": "Texture", "ImportSettings": {
			"Srgb": false, "GenerateMips": false, "Filter": "Nearest", "WrapU": "ClampToEdge", "Gamma": 2.2 } })");
	project.Directory.WriteFile(
		"Assets/Broken.png.meta",
		R"({ "Version": 1, "Handle": "0x0000000000ABC002", "Type": "Texture",
		     "ImportSettings": { "Filter": "Cubic" } })");
	project.Assets.Scan();

	const ScopedLogCapture log;
	const Ref<Texture2D> normal = project.Assets.GetAsset<Texture2D>(UUID(0xABC001));
	REQUIRE(normal != nullptr);
	CHECK_FALSE(normal->IsSrgb());
	CHECK(normal->GetMipLevelCount() == 1);
	const TextureImportSettings& settings = normal->GetSettings();
	CHECK(settings.Filter == TextureFilter::Nearest);
	CHECK(settings.WrapU == TextureWrap::ClampToEdge);
	CHECK(settings.WrapV == TextureWrap::Repeat);
	CHECK(log.Contains(LogLevel::Warn, "/ImportSettings/Gamma"));

	const Result<Ref<Asset>> broken = project.Assets.LoadAsset(UUID(0xABC002));
	REQUIRE_FALSE(broken.HasValue());
	CHECK(broken.GetError().Message.find("Assets/Broken.png.meta") != std::string::npos);
	CHECK(broken.GetError().Message.find("/ImportSettings/Filter") != std::string::npos);

	// The settings round-trip through JSON with every member written.
	TextureImportSettings custom;
	custom.Srgb = false;
	custom.WrapV = TextureWrap::MirroredRepeat;
	const Result<Json> json = SerializeTextureImportSettings(project.Types.Registry, custom);
	REQUIRE(json.HasValue());
	CHECK((*json)["WrapV"] == "MirroredRepeat");
	CHECK((*json)["GenerateMips"] == true);
	LoadReport report;
	const Result<TextureImportSettings> parsed = ParseTextureImportSettings(project.Types.Registry, *json, report);
	REQUIRE(parsed.HasValue());
	CHECK(*parsed == custom);
	CHECK(report.IsEmpty());
}

TEST_CASE("Renderer: HDR images decode to linear float textures")
{
	const std::array<glm::vec3, 2> values = { glm::vec3(0.25f, 1.5f, 12.0f), glm::vec3(0.0f, 0.5f, 100.0f) };
	const std::string hdr = EncodeRadianceRow(values);
	const Result<Image> image = DecodeTextureImage(BufferView(hdr.data(), hdr.size()));
	REQUIRE(image.HasValue());
	CHECK(image->GetFormat() == ImageFormat::RGBA32F);
	REQUIRE(image->GetWidth() == 2);
	for (uint32_t x = 0; x < 2; ++x)
	{
		const glm::vec4 pixel = image->GetPixel(x, 0);
		for (int channel = 0; channel < 3; ++channel)
		{
			// RGBE keeps about 8 bits of mantissa per pixel.
			CHECK(pixel[channel] == doctest::Approx(values[x][channel]).epsilon(0.01));
		}
		CHECK(pixel.a == 1.0f);
	}

	TextureImportSettings settings; // Srgb is on by default; HDR data is linear whatever the settings say
	const Texture2D texture(*image, settings);
	CHECK(texture.IsHdr());
	CHECK_FALSE(texture.IsSrgb());
	CHECK(texture.GetMipLevelCount() == 2);

	const std::string garbage = "not an image at all";
	const Result<Image> failed = DecodeTextureImage(BufferView(garbage.data(), garbage.size()));
	REQUIRE_FALSE(failed.HasValue());
	CHECK(failed.GetError().Message.find("not a readable image") != std::string::npos);
}

TEST_CASE("Renderer: an unreadable texture file fails to load with its path in the error")
{
	RendererProject project;
	project.Directory.WriteFile("Assets/Corrupt.png", "this is no PNG");
	project.Assets.Scan();
	const ScopedLogCapture log;
	const Result<Ref<Asset>> loaded = project.Assets.LoadAsset(project.Assets.GetHandle("Assets/Corrupt.png"));
	REQUIRE_FALSE(loaded.HasValue());
	CHECK(loaded.GetError().Message.find("Assets/Corrupt.png") != std::string::npos);
	CHECK(loaded.GetError().Message.find("not a readable image") != std::string::npos);
	CHECK(project.Assets.GetState(project.Assets.GetHandle("Assets/Corrupt.png")) == AssetState::Failed);
}

TEST_CASE("Renderer: the default textures are built-in assets with fixed handles")
{
	RendererProject project;
	const std::vector<AssetHandle> builtins = project.Assets.ListBuiltinAssets(AssetType::Texture);
	REQUIRE(builtins.size() == AllDefaultTextures.size());
	for (const DefaultTexture texture : AllDefaultTextures)
	{
		const AssetHandle handle = GetDefaultTextureHandle(texture);
		CAPTURE(ToString(texture));
		CHECK(handle.GetValue() == 0x201 + static_cast<uint64_t>(texture));
		CHECK(project.Assets.IsBuiltinAsset(handle));
		CHECK(project.Assets.GetType(handle) == AssetType::Texture);
		CHECK(project.Assets.GetPath(handle) == std::format("builtin:Textures/{}", ToString(texture)));
		CHECK(project.Assets.GetAsset<Texture2D>(handle) != nullptr);
	}

	const Ref<Texture2D> white = project.Assets.GetAsset<Texture2D>(GetDefaultTextureHandle(DefaultTexture::White));
	CHECK(white->GetWidth() == 1);
	CHECK(white->IsSrgb());
	CHECK(white->GetImage().GetPixel(0, 0) == glm::vec4(1.0f));
	const Ref<Texture2D> normal =
		project.Assets.GetAsset<Texture2D>(GetDefaultTextureHandle(DefaultTexture::FlatNormal));
	CHECK_FALSE(normal->IsSrgb());
	CHECK(Level(normal->GetImage().GetPixel(0, 0).r) == 128);
	CHECK(Level(normal->GetImage().GetPixel(0, 0).b) == 255);
	const Ref<Texture2D> checker = project.Assets.GetAsset<Texture2D>(GetDefaultTextureHandle(DefaultTexture::Checker));
	REQUIRE(checker->GetWidth() == 64);
	CHECK(checker->GetMipLevelCount() == 7);
	CHECK(checker->GetImage().GetPixel(0, 0) == checker->GetImage().GetPixel(7, 7));
	CHECK(checker->GetImage().GetPixel(0, 0) != checker->GetImage().GetPixel(8, 0));
	CHECK(checker->GetImage().GetPixel(0, 0) == checker->GetImage().GetPixel(8, 8));
}
