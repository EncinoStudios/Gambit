#include <doctest/doctest.h>

#include "TestSupport.h"

#include "Gambit/Core/FileSystem.h"
#include "Gambit/RHI/Image.h"

#include <stb_image_write.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

using namespace Gambit;

namespace
{
	// A small RGBA8 image whose pixels all differ: red = x, green = y, blue = x + y, alpha varies too.
	Image MakePattern(uint32_t width, uint32_t height)
	{
		Image image(width, height, ImageFormat::RGBA8);
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				std::byte* pixel = image.GetRow(y).data() + x * 4;
				pixel[0] = std::byte(x * 16);
				pixel[1] = std::byte(y * 16);
				pixel[2] = std::byte(x * 8 + y * 8);
				pixel[3] = std::byte(255 - x);
			}
		}
		return image;
	}

	void AppendBytes(void* context, void* data, int size)
	{
		auto* bytes = static_cast<std::vector<std::byte>*>(context);
		const auto* begin = static_cast<const std::byte*>(data);
		bytes->insert(bytes->end(), begin, begin + size);
	}
}

TEST_CASE("RHI: Image stores pixels in each format")
{
	Image rgba8(3, 2, ImageFormat::RGBA8);
	CHECK(rgba8.GetWidth() == 3);
	CHECK(rgba8.GetHeight() == 2);
	CHECK(rgba8.GetBytesPerPixel() == 4);
	CHECK(rgba8.GetRowPitch() == 12);
	CHECK(rgba8.GetPixels().size() == 24);
	CHECK(rgba8.GetPixel(2, 1) == glm::vec4(0.0f)); // zero-initialized

	// 8-bit values clamp to [0, 1] and round to the nearest level.
	rgba8.SetPixel(2, 1, glm::vec4(0.5f, -1.0f, 2.0f, 0.25f));
	CHECK(std::to_integer<int>(rgba8.GetRow(1)[8]) == 128);
	CHECK(std::to_integer<int>(rgba8.GetRow(1)[9]) == 0);
	CHECK(std::to_integer<int>(rgba8.GetRow(1)[10]) == 255);
	CHECK(std::to_integer<int>(rgba8.GetRow(1)[11]) == 64);
	CHECK(rgba8.GetPixel(2, 1) == glm::vec4(128.0f / 255.0f, 0.0f, 1.0f, 64.0f / 255.0f));
	rgba8.SetPixel(0, 0, glm::vec4(std::numeric_limits<float>::quiet_NaN()));
	CHECK(rgba8.GetPixel(0, 0) == glm::vec4(0.0f));

	// Half floats keep values that are exactly representable and round the others.
	Image rgba16f(2, 2, ImageFormat::RGBA16F);
	CHECK(rgba16f.GetBytesPerPixel() == 8);
	rgba16f.SetPixel(1, 1, glm::vec4(0.5f, -2.0f, 1024.0f, 1.0f / 3.0f));
	const glm::vec4 half = rgba16f.GetPixel(1, 1);
	CHECK(half.x == 0.5f);
	CHECK(half.y == -2.0f);
	CHECK(half.z == 1024.0f);
	CHECK(half.w == doctest::Approx(1.0f / 3.0f).epsilon(1e-3));

	// 32-bit floats are stored exactly, out-of-range values included.
	Image rgba32f(1, 1, ImageFormat::RGBA32F);
	CHECK(rgba32f.GetBytesPerPixel() == 16);
	rgba32f.SetPixel(0, 0, glm::vec4(1.0e10f, -3.25f, 0.1f, 7.0f));
	CHECK(rgba32f.GetPixel(0, 0) == glm::vec4(1.0e10f, -3.25f, 0.1f, 7.0f));

	Image filled(4, 3, ImageFormat::RGBA16F);
	filled.Fill(glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
	for (uint32_t y = 0; y < 3; ++y)
	{
		for (uint32_t x = 0; x < 4; ++x)
		{
			CHECK(filled.GetPixel(x, y) == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f));
		}
	}

	const Image empty;
	CHECK(empty.IsEmpty());
	CHECK(empty.GetWidth() == 0);
}

TEST_CASE("RHI: Image::Create checks the size and the pixel count")
{
	Result<Image> image = Image::Create(2, 1, ImageFormat::RGBA8, std::vector<std::byte>(8, std::byte{ 7 }));
	REQUIRE(image.HasValue());
	CHECK(image->GetPixel(1, 0) == glm::vec4(7.0f / 255.0f));

	CHECK_FALSE(Image::Create(2, 1, ImageFormat::RGBA8, std::vector<std::byte>(7)).HasValue());
	CHECK_FALSE(Image::Create(2, 1, ImageFormat::RGBA16F, std::vector<std::byte>(8)).HasValue());
	CHECK_FALSE(Image::Create(0, 1, ImageFormat::RGBA8, {}).HasValue());
	CHECK_FALSE(Image::Create(Image::MaxDimension + 1, 1, ImageFormat::RGBA8, {}).HasValue());
}

TEST_CASE("RHI: sRGB transfer functions match the standard")
{
	CHECK(LinearToSrgb(0.0f) == 0.0f);
	CHECK(LinearToSrgb(1.0f) == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(LinearToSrgb(0.002f) == doctest::Approx(0.002f * 12.92f)); // the linear segment
	CHECK(LinearToSrgb(0.5f) == doctest::Approx(0.735356983f).epsilon(1e-6));
	CHECK(LinearToSrgb(0.18f) == doctest::Approx(0.461356129f).epsilon(1e-6));
	CHECK(LinearToSrgb(-1.0f) == 0.0f);
	CHECK(LinearToSrgb(5.0f) == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(SrgbToLinear(0.5f) == doctest::Approx(0.214041144f).epsilon(1e-6));
	CHECK(SrgbToLinear(0.04f) == doctest::Approx(0.04f / 12.92f));
	for (int i = 0; i <= 100; ++i)
	{
		const float value = static_cast<float>(i) / 100.0f;
		CHECK(SrgbToLinear(LinearToSrgb(value)) == doctest::Approx(value).epsilon(1e-5));
	}
}

TEST_CASE("RHI: ConvertImage changes formats and optionally applies the sRGB curve")
{
	Image linear(2, 1, ImageFormat::RGBA32F);
	linear.SetPixel(0, 0, glm::vec4(0.5f, 0.0f, 1.0f, 0.5f));
	linear.SetPixel(1, 0, glm::vec4(2.0f, -1.0f, 0.18f, 1.0f));

	const Image plain = ConvertImage(linear, ImageFormat::RGBA8);
	CHECK(plain.GetFormat() == ImageFormat::RGBA8);
	CHECK(plain.GetPixel(0, 0) == glm::vec4(128.0f / 255.0f, 0.0f, 1.0f, 128.0f / 255.0f));
	CHECK(plain.GetPixel(1, 0) == glm::vec4(1.0f, 0.0f, 46.0f / 255.0f, 1.0f));

	// sRGB encoding applies to color, never to alpha.
	const Image encoded = ConvertImage(linear, ImageFormat::RGBA8, TransferFunction::Srgb);
	CHECK(encoded.GetPixel(0, 0) == glm::vec4(188.0f / 255.0f, 0.0f, 1.0f, 128.0f / 255.0f));
	const Image decoded = ConvertImage(encoded, ImageFormat::RGBA32F, TransferFunction::Srgb);
	CHECK(decoded.GetPixel(0, 0).x == doctest::Approx(0.5f).epsilon(0.01));
	CHECK(decoded.GetPixel(0, 0).w == doctest::Approx(128.0f / 255.0f));

	const Image half = ConvertImage(linear, ImageFormat::RGBA16F, TransferFunction::Srgb); // float to float: no curve
	CHECK(half.GetPixel(0, 0) == glm::vec4(0.5f, 0.0f, 1.0f, 0.5f));
	CHECK(ConvertImage(plain, ImageFormat::RGBA8) == plain);
	CHECK(ConvertImage(Image(), ImageFormat::RGBA8).IsEmpty());
}

TEST_CASE("RHI: PNG encoding and decoding round trip")
{
	const Image pattern = MakePattern(13, 7);
	const Result<Buffer> encoded = EncodePng(pattern);
	REQUIRE(encoded.HasValue());
	CHECK(encoded->Size() > 8);
	CHECK(std::to_integer<int>(encoded->Data()[1]) == 'P');

	const Result<Image> decoded = DecodePng(*encoded);
	REQUIRE(decoded.HasValue());
	CHECK(*decoded == pattern);

	CHECK_FALSE(EncodePng(Image(2, 2, ImageFormat::RGBA16F)).HasValue());
	CHECK_FALSE(EncodePng(Image()).HasValue());
	const std::vector<std::byte> garbage(64, std::byte{ 0x42 });
	CHECK_FALSE(DecodePng(BufferView(garbage.data(), garbage.size())).HasValue());
	CHECK_FALSE(DecodePng(BufferView()).HasValue());
}

TEST_CASE("RHI: PNG decoding expands grey images to opaque RGBA")
{
	const std::vector<uint8_t> grey = { 0, 128, 255, 64 };
	std::vector<std::byte> png;
	REQUIRE(stbi_write_png_to_func(AppendBytes, &png, 2, 2, 1, grey.data(), 2) != 0);

	const Result<Image> image = DecodePng(BufferView(png.data(), png.size()));
	REQUIRE(image.HasValue());
	CHECK(image->GetFormat() == ImageFormat::RGBA8);
	CHECK(image->GetPixel(1, 0) == glm::vec4(128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, 1.0f));
	CHECK(image->GetPixel(0, 1) == glm::vec4(1.0f));
}

TEST_CASE("RHI: PNG files are saved and loaded")
{
	const Tests::ScopedTempDirectory directory;
	const std::filesystem::path path = directory.GetPath() / "pattern.png";
	const Image pattern = MakePattern(5, 9);
	REQUIRE(SavePng(pattern, path).HasValue());
	const Result<Image> loaded = LoadPng(path);
	REQUIRE(loaded.HasValue());
	CHECK(*loaded == pattern);

	const Result<Image> missing = LoadPng(directory.GetPath() / "missing.png");
	REQUIRE_FALSE(missing.HasValue());
	CHECK(HasFileError(missing.GetError(), FileError::NotFound));

	REQUIRE(FileSystem::WriteText(directory.GetPath() / "text.png", "not a png").HasValue());
	const Result<Image> text = LoadPng(directory.GetPath() / "text.png");
	REQUIRE_FALSE(text.HasValue());
	CHECK(text.GetError().Message.find("text.png") != std::string::npos);
}

TEST_CASE("RHI: ImageCompare counts the pixels beyond the tolerance")
{
	const Image expected = MakePattern(10, 10);

	const Result<ImageCompareResult> same = ImageCompare(expected, expected, 0.0f, 0.0);
	REQUIRE(same.HasValue());
	CHECK(same->Passed);
	CHECK(same->PixelCount == 100);
	CHECK(same->BadPixelCount == 0);
	CHECK(same->MaxDifference == 0.0f);
	CHECK(same->MeanDifference == 0.0);
	CHECK(same->Diff.GetWidth() == 10);
	CHECK(same->Diff.GetFormat() == ImageFormat::RGBA8);

	Image actual = expected;
	std::byte* first = actual.GetRow(0).data();
	first[0] = std::byte(std::to_integer<int>(first[0]) + 2); // within a 2-level tolerance
	actual.GetRow(5).data()[4 * 3 + 1] = std::byte(200);      // pixel (3, 5): far off
	actual.GetRow(9).data()[4 * 9 + 3] = std::byte(0);        // pixel (9, 9): alpha differs

	const float tolerance = 2.0f / 255.0f;
	const Result<ImageCompareResult> strict = ImageCompare(actual, expected, tolerance, 0.0);
	REQUIRE(strict.HasValue());
	CHECK_FALSE(strict->Passed);
	CHECK(strict->BadPixelCount == 2);
	CHECK(strict->GetBadPixelFraction() == doctest::Approx(0.02));
	CHECK(strict->MaxDifference == doctest::Approx(246.0f / 255.0f));
	CHECK(strict->MeanDifference > 0.0);
	CHECK(strict->Diff.GetPixel(3, 5).x >= 0.5f); // bad pixels are red
	CHECK(strict->Diff.GetPixel(3, 5).y == 0.0f);
	CHECK(strict->Diff.GetPixel(0, 0).x == strict->Diff.GetPixel(0, 0).y); // good pixels are grey

	const Result<ImageCompareResult> lenient = ImageCompare(actual, expected, tolerance, 0.02);
	REQUIRE(lenient.HasValue());
	CHECK(lenient->Passed);

	CHECK_FALSE(ImageCompare(MakePattern(10, 9), expected, tolerance, 1.0).HasValue());
	CHECK_FALSE(ImageCompare(Image(), expected, tolerance, 1.0).HasValue());
}

TEST_CASE("RHI: ImageCompare accepts exactly the tolerated number of 8-bit levels")
{
	// Every level against the level two (and three) above it. In float, some differences of two levels come out a
	// little above 2.0f / 255 (128 / 255 - 126 / 255, for example); they must still count as two levels.
	constexpr uint32_t Width = 253;
	Image expected(Width, 1, ImageFormat::RGBA8);
	Image twoLevelsUp(Width, 1, ImageFormat::RGBA8);
	Image threeLevelsUp(Width, 1, ImageFormat::RGBA8);
	for (uint32_t x = 0; x < Width; ++x)
	{
		for (uint32_t c = 0; c < 4; ++c)
		{
			expected.GetRow(0)[x * 4 + c] = std::byte(x);
			twoLevelsUp.GetRow(0)[x * 4 + c] = std::byte(x + 2);
			threeLevelsUp.GetRow(0)[x * 4 + c] = std::byte(x + 3);
		}
	}

	const float tolerance = 2.0f / 255.0f;
	const Result<ImageCompareResult> two = ImageCompare(twoLevelsUp, expected, tolerance, 0.0);
	REQUIRE(two.HasValue());
	CHECK(two->Passed);
	CHECK(two->BadPixelCount == 0);
	CHECK(two->MaxDifference == tolerance);

	const Result<ImageCompareResult> three = ImageCompare(threeLevelsUp, expected, tolerance, 0.0);
	REQUIRE(three.HasValue());
	CHECK_FALSE(three->Passed);
	CHECK(three->BadPixelCount == Width);
}

TEST_CASE("RHI: ImageCompare works across formats and treats NaN as different")
{
	const Image rgba8 = MakePattern(4, 4);
	const Image rgba32f = ConvertImage(rgba8, ImageFormat::RGBA32F);
	const Result<ImageCompareResult> crossFormat = ImageCompare(rgba32f, rgba8, 1e-6f, 0.0);
	REQUIRE(crossFormat.HasValue());
	CHECK(crossFormat->Passed);

	Image withNaN = rgba32f;
	withNaN.SetPixel(1, 1, glm::vec4(std::numeric_limits<float>::quiet_NaN()));
	const Result<ImageCompareResult> nanAgainstNumber = ImageCompare(withNaN, rgba32f, 1.0f, 0.0);
	REQUIRE(nanAgainstNumber.HasValue());
	CHECK(nanAgainstNumber->BadPixelCount == 1);
	CHECK(std::isinf(nanAgainstNumber->MaxDifference));

	const Result<ImageCompareResult> nanAgainstNaN = ImageCompare(withNaN, withNaN, 0.0f, 0.0);
	REQUIRE(nanAgainstNaN.HasValue());
	CHECK(nanAgainstNaN->Passed);
}
