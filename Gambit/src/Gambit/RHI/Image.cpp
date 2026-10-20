#include "gbpch.h"

#include "Gambit/RHI/Image.h"

#include "Gambit/Core/FileSystem.h"

#include <glm/gtc/packing.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <limits>

namespace Gambit
{
	namespace
	{
		uint8_t ToUnorm8(float value) noexcept
		{
			// NaN becomes 0, like the clamp of a GPU conversion.
			const float clamped = value > 0.0f ? (value < 1.0f ? value : 1.0f) : 0.0f;
			return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
		}

		// The difference of two channel values; NaN against a number is infinitely different, NaN against NaN equal.
		float ChannelDifference(float a, float b) noexcept
		{
			const bool aNaN = std::isnan(a);
			const bool bNaN = std::isnan(b);
			if (aNaN || bNaN)
			{
				return aNaN && bNaN ? 0.0f : std::numeric_limits<float>::infinity();
			}
			if (a == b)
			{
				return 0.0f; // also equal infinities, whose difference would be NaN
			}
			return std::abs(a - b);
		}

		bool IsValidSize(uint32_t width, uint32_t height) noexcept
		{
			return width >= 1 && height >= 1 && width <= Image::MaxDimension && height <= Image::MaxDimension;
		}

		void AppendToBuffer(void* context, void* data, int size)
		{
			auto* bytes = static_cast<std::vector<std::byte>*>(context);
			const auto* begin = static_cast<const std::byte*>(data);
			bytes->insert(bytes->end(), begin, begin + size);
		}
	}

	std::string_view ToString(ImageFormat format) noexcept
	{
		switch (format)
		{
			case ImageFormat::RGBA8:
				return "RGBA8";
			case ImageFormat::RGBA16F:
				return "RGBA16F";
			case ImageFormat::RGBA32F:
				return "RGBA32F";
		}
		return "unknown";
	}

	uint32_t GetBytesPerPixel(ImageFormat format) noexcept
	{
		switch (format)
		{
			case ImageFormat::RGBA8:
				return 4;
			case ImageFormat::RGBA16F:
				return 8;
			case ImageFormat::RGBA32F:
				return 16;
		}
		return 0;
	}

	float LinearToSrgb(float linear) noexcept
	{
		const float c = linear > 0.0f ? (linear < 1.0f ? linear : 1.0f) : 0.0f;
		return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	}

	float SrgbToLinear(float srgb) noexcept
	{
		const float c = srgb > 0.0f ? (srgb < 1.0f ? srgb : 1.0f) : 0.0f;
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	Image::Image(uint32_t width, uint32_t height, ImageFormat format)
		: m_Width(width), m_Height(height), m_Format(format)
	{
		GB_CORE_ASSERT(IsValidSize(width, height), "invalid image size {}x{}", width, height);
		m_Pixels.resize(size_t(width) * height * Gambit::GetBytesPerPixel(format));
	}

	Result<Image> Image::Create(uint32_t width, uint32_t height, ImageFormat format, std::vector<std::byte> pixels)
	{
		if (!IsValidSize(width, height))
		{
			return Error(
				std::format("invalid image size {}x{} (1 to {} in each direction)", width, height, MaxDimension));
		}
		const size_t expected = size_t(width) * height * Gambit::GetBytesPerPixel(format);
		if (pixels.size() != expected)
		{
			return Error(std::format(
				"a {}x{} {} image needs {} bytes of pixels, not {}", width, height, ToString(format), expected,
				pixels.size()));
		}
		Image image;
		image.m_Width = width;
		image.m_Height = height;
		image.m_Format = format;
		image.m_Pixels = std::move(pixels);
		return image;
	}

	std::span<std::byte> Image::GetRow(uint32_t y)
	{
		GB_CORE_ASSERT(y < m_Height, "row {} is outside the {}-row image", y, m_Height);
		return std::span<std::byte>(m_Pixels).subspan(y * GetRowPitch(), GetRowPitch());
	}

	std::span<const std::byte> Image::GetRow(uint32_t y) const
	{
		GB_CORE_ASSERT(y < m_Height, "row {} is outside the {}-row image", y, m_Height);
		return std::span<const std::byte>(m_Pixels).subspan(y * GetRowPitch(), GetRowPitch());
	}

	size_t Image::GetPixelOffset(uint32_t x, uint32_t y) const
	{
		GB_CORE_ASSERT(
			x < m_Width && y < m_Height, "pixel ({}, {}) is outside the {}x{} image", x, y, m_Width, m_Height);
		return (size_t(y) * m_Width + x) * GetBytesPerPixel();
	}

	glm::vec4 Image::GetPixel(uint32_t x, uint32_t y) const
	{
		const std::byte* pixel = m_Pixels.data() + GetPixelOffset(x, y);
		glm::vec4 value(0.0f);
		switch (m_Format)
		{
			case ImageFormat::RGBA8:
				for (int c = 0; c < 4; ++c)
				{
					value[c] = static_cast<float>(std::to_integer<uint8_t>(pixel[c])) / 255.0f;
				}
				break;
			case ImageFormat::RGBA16F:
				for (int c = 0; c < 4; ++c)
				{
					uint16_t half = 0;
					std::memcpy(&half, pixel + c * sizeof(uint16_t), sizeof(half));
					value[c] = glm::unpackHalf1x16(half);
				}
				break;
			case ImageFormat::RGBA32F:
				std::memcpy(&value[0], pixel, sizeof(float) * 4);
				break;
		}
		return value;
	}

	void Image::SetPixel(uint32_t x, uint32_t y, const glm::vec4& value)
	{
		std::byte* pixel = m_Pixels.data() + GetPixelOffset(x, y);
		switch (m_Format)
		{
			case ImageFormat::RGBA8:
				for (int c = 0; c < 4; ++c)
				{
					pixel[c] = std::byte{ ToUnorm8(value[c]) };
				}
				break;
			case ImageFormat::RGBA16F:
				for (int c = 0; c < 4; ++c)
				{
					const uint16_t half = glm::packHalf1x16(value[c]);
					std::memcpy(pixel + c * sizeof(uint16_t), &half, sizeof(half));
				}
				break;
			case ImageFormat::RGBA32F:
				std::memcpy(pixel, &value[0], sizeof(float) * 4);
				break;
		}
	}

	void Image::Fill(const glm::vec4& value)
	{
		if (IsEmpty())
		{
			return;
		}
		SetPixel(0, 0, value);
		const std::span<const std::byte> first = std::span<const std::byte>(m_Pixels).first(GetBytesPerPixel());
		for (size_t offset = first.size(); offset < m_Pixels.size(); offset += first.size())
		{
			std::memcpy(m_Pixels.data() + offset, first.data(), first.size());
		}
	}

	Image ConvertImage(const Image& source, ImageFormat format, TransferFunction transfer)
	{
		if (source.IsEmpty() || source.GetFormat() == format)
		{
			return source;
		}
		const bool encode = transfer == TransferFunction::Srgb && format == ImageFormat::RGBA8;
		const bool decode = transfer == TransferFunction::Srgb && source.GetFormat() == ImageFormat::RGBA8;
		Image result(source.GetWidth(), source.GetHeight(), format);
		for (uint32_t y = 0; y < source.GetHeight(); ++y)
		{
			for (uint32_t x = 0; x < source.GetWidth(); ++x)
			{
				glm::vec4 value = source.GetPixel(x, y);
				for (int c = 0; c < 3; ++c)
				{
					if (encode)
					{
						value[c] = LinearToSrgb(value[c]);
					}
					else if (decode)
					{
						value[c] = SrgbToLinear(value[c]);
					}
				}
				result.SetPixel(x, y, value);
			}
		}
		return result;
	}

	Result<Buffer> EncodePng(const Image& image)
	{
		if (image.IsEmpty())
		{
			return Error("cannot encode an empty image as PNG");
		}
		if (image.GetFormat() != ImageFormat::RGBA8)
		{
			return Error(std::format(
				"PNG encoding needs an RGBA8 image, not {} (use ConvertImage first)", ToString(image.GetFormat())));
		}
		std::vector<std::byte> bytes;
		const int written = stbi_write_png_to_func(
			AppendToBuffer, &bytes, static_cast<int>(image.GetWidth()), static_cast<int>(image.GetHeight()), 4,
			image.GetPixels().data(), static_cast<int>(image.GetRowPitch()));
		if (written == 0 || bytes.empty())
		{
			return Error(std::format("cannot encode a {}x{} image as PNG", image.GetWidth(), image.GetHeight()));
		}
		return Buffer::Copy(bytes.data(), bytes.size());
	}

	Result<Image> DecodePng(BufferView data)
	{
		if (data.Empty() || data.Size() > size_t(std::numeric_limits<int>::max()))
		{
			return Error(std::format("cannot decode {} bytes as an image", data.Size()));
		}
		const auto* bytes = reinterpret_cast<const stbi_uc*>(data.Data());
		const int length = static_cast<int>(data.Size());
		if (stbi_info_from_memory(bytes, length, nullptr, nullptr, nullptr) == 0)
		{
			return Error(std::format("not a readable image: {}", stbi_failure_reason()));
		}
		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(bytes, length, &width, &height, &channels, 4);
		if (pixels == nullptr)
		{
			return Error(std::format("cannot decode the image: {}", stbi_failure_reason()));
		}
		const auto* begin = reinterpret_cast<const std::byte*>(pixels);
		std::vector<std::byte> copy(begin, begin + size_t(width) * size_t(height) * 4);
		stbi_image_free(pixels);
		return Image::Create(
			static_cast<uint32_t>(width), static_cast<uint32_t>(height), ImageFormat::RGBA8, std::move(copy));
	}

	Result<void> SavePng(const Image& image, const std::filesystem::path& path)
	{
		Result<Buffer> encoded = EncodePng(image);
		if (!encoded)
		{
			return encoded.GetError();
		}
		return FileSystem::WriteBinary(path, *encoded);
	}

	Result<Image> LoadPng(const std::filesystem::path& path)
	{
		const Result<Buffer> bytes = FileSystem::ReadBinary(path);
		if (!bytes)
		{
			return bytes.GetError();
		}
		Result<Image> image = DecodePng(*bytes);
		if (!image)
		{
			return Error(std::format("{}: {}", FileSystem::ToUtf8(path), image.GetError().Message));
		}
		return image;
	}

	Result<ImageCompareResult>
	ImageCompare(const Image& actual, const Image& expected, float perChannelTolerance, double maxBadPixelFraction)
	{
		if (actual.IsEmpty() || expected.IsEmpty())
		{
			return Error("cannot compare an empty image");
		}
		if (actual.GetWidth() != expected.GetWidth() || actual.GetHeight() != expected.GetHeight())
		{
			return Error(std::format(
				"the images differ in size: {}x{} against {}x{}", actual.GetWidth(), actual.GetHeight(),
				expected.GetWidth(), expected.GetHeight()));
		}

		ImageCompareResult result;
		result.PixelCount = uint64_t(actual.GetWidth()) * actual.GetHeight();
		result.Diff = Image(actual.GetWidth(), actual.GetHeight(), ImageFormat::RGBA8);
		// Two 8-bit images differ by whole levels. Their normalized difference is snapped to n / 255 because the float
		// difference of two levels is not always exact (128 / 255 - 126 / 255 > 2.0f / 255), which would make a
		// tolerance of n levels reject some differences of exactly n levels.
		const bool wholeLevels = actual.GetFormat() == ImageFormat::RGBA8 && expected.GetFormat() == ImageFormat::RGBA8;
		double differenceSum = 0.0;
		for (uint32_t y = 0; y < actual.GetHeight(); ++y)
		{
			for (uint32_t x = 0; x < actual.GetWidth(); ++x)
			{
				const glm::vec4 a = actual.GetPixel(x, y);
				const glm::vec4 b = expected.GetPixel(x, y);
				float pixelDifference = 0.0f;
				for (int c = 0; c < 4; ++c)
				{
					float difference = ChannelDifference(a[c], b[c]);
					if (wholeLevels)
					{
						difference = std::round(difference * 255.0f) / 255.0f;
					}
					pixelDifference = std::max(pixelDifference, difference);
					differenceSum += std::isinf(difference) ? 1.0 : double(difference);
				}
				result.MaxDifference = std::max(result.MaxDifference, pixelDifference);

				if (pixelDifference > perChannelTolerance)
				{
					++result.BadPixelCount;
					const float intensity = std::min(1.0f, 0.5f + pixelDifference * 4.0f);
					result.Diff.SetPixel(x, y, glm::vec4(intensity, 0.0f, 0.0f, 1.0f));
				}
				else
				{
					const float grey =
						0.3f * glm::dot(glm::clamp(glm::vec3(a), 0.0f, 1.0f), glm::vec3(0.2126f, 0.7152f, 0.0722f));
					result.Diff.SetPixel(x, y, glm::vec4(grey, grey, grey, 1.0f));
				}
			}
		}
		result.MeanDifference = differenceSum / (double(result.PixelCount) * 4.0);
		result.Passed = result.GetBadPixelFraction() <= maxBadPixelFraction;
		return result;
	}
}
