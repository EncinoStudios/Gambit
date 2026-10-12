#include "TestUtils/Golden.h"

#include "TestSupport.h"

#include "Gambit/Core/FileSystem.h"
#include "Gambit/RHI/GraphicsDevice.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

namespace Gambit::Tests
{
	namespace
	{
		// Writes the actual and diff images of a failed comparison next to each other for inspection.
		void WriteFailureOutput(std::string_view name, const Image& actual, const Image* diff)
		{
			const std::filesystem::path directory = GB_TEST_OUTPUT_DIR;
			if (!FileSystem::CreateDirectories(directory))
			{
				MESSAGE("cannot create ", FileSystem::ToUtf8(directory));
				return;
			}
			const std::filesystem::path actualPath = directory / (std::string(name) + ".actual.png");
			if (const Result<void> saved = SavePng(actual, actualPath); saved)
			{
				MESSAGE("actual image: ", FileSystem::ToUtf8(actualPath));
			}
			if (diff != nullptr)
			{
				const std::filesystem::path diffPath = directory / (std::string(name) + ".diff.png");
				if (const Result<void> saved = SavePng(*diff, diffPath); saved)
				{
					MESSAGE("difference image: ", FileSystem::ToUtf8(diffPath));
				}
			}
		}
	}

	void CheckGolden(std::string_view name, const Image& image, const GoldenOptions& options)
	{
		REQUIRE_FALSE(image.IsEmpty());
		const Image actual = ConvertImage(image, ImageFormat::RGBA8, options.FloatToRgba8);
		const std::filesystem::path goldenPath =
			std::filesystem::path(GB_TEST_GOLDEN_DIR) / (std::string(name) + ".png");

		if (IsEnvironmentFlagSet("GB_UPDATE_GOLDEN"))
		{
			const Result<void> saved = SavePng(actual, goldenPath);
			REQUIRE_MESSAGE(saved.HasValue(), "cannot write ", FileSystem::ToUtf8(goldenPath));
			FAIL_CHECK(
				"wrote the golden image ", FileSystem::ToUtf8(goldenPath),
				": review it, then run again without GB_UPDATE_GOLDEN");
			return;
		}

		const Result<Image> golden = LoadPng(goldenPath);
		if (!golden)
		{
			WriteFailureOutput(name, actual, nullptr);
			FAIL_CHECK(
				"no golden image ", FileSystem::ToUtf8(goldenPath), " (", golden.GetError().Message,
				"); create it with GB_UPDATE_GOLDEN=1");
			return;
		}

		const Result<ImageCompareResult> compared =
			ImageCompare(actual, *golden, options.PerChannelTolerance, options.MaxBadPixelFraction);
		if (!compared)
		{
			WriteFailureOutput(name, actual, nullptr);
			FAIL_CHECK("golden image ", name, ": ", compared.GetError().Message);
			return;
		}
		if (!compared->Passed)
		{
			WriteFailureOutput(name, actual, &compared->Diff);
		}
		CHECK_MESSAGE(
			compared->Passed, "golden image ", name, ": ", compared->BadPixelCount, " of ", compared->PixelCount,
			" pixels differ by more than the tolerance (largest difference ", compared->MaxDifference, ")");
	}

	void CheckGoldenTexture(
		GraphicsDevice& device, nvrhi::ITexture* texture, std::string_view name, const GoldenOptions& options)
	{
		const Result<Image> image = device.ReadbackTexture(texture);
		REQUIRE_MESSAGE(image.HasValue(), "readback for golden image ", name, " failed: ", image.GetError().Message);
		CheckGolden(name, *image, options);
	}
}
