#pragma once

#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Math.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace Gambit
{
	// Pixel layout of a CPU Image: four channels, red first, of 8-bit unsigned normalized, 16-bit or 32-bit float
	// values.
	enum class ImageFormat : uint8_t
	{
		RGBA8,
		RGBA16F,
		RGBA32F
	};

	[[nodiscard]] std::string_view ToString(ImageFormat format) noexcept;
	[[nodiscard]] uint32_t GetBytesPerPixel(ImageFormat format) noexcept;

	// How 8-bit values relate to the linear values of float images in ConvertImage.
	enum class TransferFunction : uint8_t
	{
		Linear, // 8-bit values are linear: value / 255
		Srgb    // 8-bit values are sRGB encoded (IEC 61966-2-1); alpha always stays linear
	};

	// The exact piecewise sRGB transfer functions; inputs are clamped to [0, 1]. ColorSpace.glsl has the GPU versions.
	[[nodiscard]] float LinearToSrgb(float linear) noexcept;
	[[nodiscard]] float SrgbToLinear(float srgb) noexcept;

	// A CPU-side 2D image: width x height pixels in one of the ImageFormats, rows tightly packed from the top row down.
	// This is what texture readback produces, what PNG files load into and what golden-image tests compare.
	class Image
	{
	public:
		// Images are limited to this many pixels in each direction (the largest texture size the RHI supports).
		static constexpr uint32_t MaxDimension = 16384;

		// An empty image (0 x 0).
		Image() = default;
		// A width x height image with all bytes zero (transparent black). Both sizes must be 1..MaxDimension.
		Image(uint32_t width, uint32_t height, ImageFormat format);

		// An image owning `pixels`, which must hold exactly width * height * GetBytesPerPixel(format) bytes.
		[[nodiscard]] static Result<Image>
		Create(uint32_t width, uint32_t height, ImageFormat format, std::vector<std::byte> pixels);

		[[nodiscard]] uint32_t GetWidth() const noexcept { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const noexcept { return m_Height; }
		[[nodiscard]] ImageFormat GetFormat() const noexcept { return m_Format; }
		[[nodiscard]] bool IsEmpty() const noexcept { return m_Pixels.empty(); }
		[[nodiscard]] uint32_t GetBytesPerPixel() const noexcept { return Gambit::GetBytesPerPixel(m_Format); }
		[[nodiscard]] size_t GetRowPitch() const noexcept { return size_t(m_Width) * GetBytesPerPixel(); }

		[[nodiscard]] std::span<std::byte> GetPixels() noexcept { return m_Pixels; }
		[[nodiscard]] std::span<const std::byte> GetPixels() const noexcept { return m_Pixels; }
		// The bytes of row `y` (0 = top).
		[[nodiscard]] std::span<std::byte> GetRow(uint32_t y);
		[[nodiscard]] std::span<const std::byte> GetRow(uint32_t y) const;

		// The pixel at (x, y) as floats; RGBA8 values are returned as value / 255 (no transfer function applied).
		[[nodiscard]] glm::vec4 GetPixel(uint32_t x, uint32_t y) const;
		// Stores `value`; RGBA8 clamps to [0, 1] and rounds to the nearest of the 256 levels, RGBA16F rounds to half.
		void SetPixel(uint32_t x, uint32_t y, const glm::vec4& value);
		void Fill(const glm::vec4& value);

		[[nodiscard]] bool operator==(const Image& other) const = default;

	private:
		[[nodiscard]] size_t GetPixelOffset(uint32_t x, uint32_t y) const;

		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		ImageFormat m_Format = ImageFormat::RGBA8;
		std::vector<std::byte> m_Pixels;
	};

	// `source` in `format`. Float to RGBA8 clamps to [0, 1] and rounds; with TransferFunction::Srgb the RGB channels
	// are sRGB-encoded on the way to RGBA8 and decoded on the way from it. Float to float conversions ignore
	// `transfer`.
	[[nodiscard]] Image
	ConvertImage(const Image& source, ImageFormat format, TransferFunction transfer = TransferFunction::Linear);

	// PNG files hold 8-bit RGBA (stb_image / stb_image_write). Encoding requires an RGBA8 image; decoding accepts any
	// 8- or 16-bit PNG, and the other 8-bit formats stb_image reads (JPEG, TGA, BMP), and returns RGBA8 (grey and RGB
	// gain opaque alpha, 16-bit values are reduced to 8 bits). DecodeTextureImage (Renderer) adds Radiance HDR.
	[[nodiscard]] Result<Buffer> EncodePng(const Image& image);
	[[nodiscard]] Result<Image> DecodePng(BufferView data);
	// File variants of the above for tools and tests; engine content goes through the VFS and the in-memory versions.
	[[nodiscard]] Result<void> SavePng(const Image& image, const std::filesystem::path& path);
	[[nodiscard]] Result<Image> LoadPng(const std::filesystem::path& path);

	// The outcome of ImageCompare. A pixel is bad when any channel differs by more than the tolerance.
	struct ImageCompareResult
	{
		bool Passed = false;         // the fraction of bad pixels is at most the allowed fraction
		uint64_t PixelCount = 0;     // width * height
		uint64_t BadPixelCount = 0;  // pixels with a channel difference above the tolerance
		float MaxDifference = 0.0f;  // the largest channel difference, in normalized units (RGBA8: levels / 255)
		double MeanDifference = 0.0; // the mean channel difference over all channels of all pixels
		Image Diff;                  // RGBA8: bad pixels red (brighter = larger difference), others a dimmed grey copy

		[[nodiscard]] double GetBadPixelFraction() const noexcept
		{
			return PixelCount != 0 ? double(BadPixelCount) / double(PixelCount) : 0.0;
		}
	};

	// Compares `actual` with `expected` channel by channel in normalized units (RGBA8 values / 255, float values as
	// they are), so images of different formats can be compared. `perChannelTolerance` is the largest acceptable
	// difference of a channel (2.0f / 255 accepts two 8-bit levels); the comparison passes when at most
	// `maxBadPixelFraction` of the pixels exceed it. Fails (as an error, not a mismatch) when the sizes differ or an
	// image is empty.
	[[nodiscard]] Result<ImageCompareResult>
	ImageCompare(const Image& actual, const Image& expected, float perChannelTolerance, double maxBadPixelFraction);
}
