#pragma once

#include "Gambit/RHI/Image.h"

#include <nvrhi/nvrhi.h>

#include <string_view>

namespace Gambit
{
	class GraphicsDevice;
}

namespace Gambit::Tests
{
	// How closely a rendered image must match its golden image.
	struct GoldenOptions
	{
		// The largest acceptable difference of a channel, in normalized units (2 / 255 = two 8-bit levels): rasterizers
		// may differ slightly in interpolation and edge coverage.
		float PerChannelTolerance = 2.0f / 255.0f;
		// The fraction of pixels allowed to exceed the tolerance.
		double MaxBadPixelFraction = 0.001;
		// How float images are turned into the 8-bit PNG: sRGB-encoded (their values are linear) or stored as they are.
		TransferFunction FloatToRgba8 = TransferFunction::Srgb;
	};

	// Compares `image` with the golden image GambitTests/Golden/<name>.png and fails the current test (doctest CHECK)
	// on a mismatch or a missing golden; then <name>.actual.png and <name>.diff.png are written to build/test-output/
	// to look at (each check first removes those of an earlier run). With GB_UPDATE_GOLDEN=1 the image is written as
	// the new golden instead and the test fails once, asking for a review (as for every regenerated reference,
	// AGENTS.md section 4).
	void CheckGolden(std::string_view name, const Image& image, const GoldenOptions& options = {});

	// Reads `texture` back (mip 0, slice 0) and checks it with CheckGolden.
	void CheckGoldenTexture(
		GraphicsDevice& device, nvrhi::ITexture* texture, std::string_view name, const GoldenOptions& options = {});
}
