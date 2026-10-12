#pragma once

#include "Gambit/RHI/GraphicsDevice.h"

#include <cstdint>
#include <string>

namespace Gambit::Tests
{
	// Whether GPU tests are switched off (GB_TEST_NO_GPU=1, D-005).
	[[nodiscard]] bool AreGpuTestsDisabled();

	// The headless graphics device the GPU tests share (validation on, 256x256 SRGBA8 back buffers), created on first
	// use so that the tests do not each pay for a device. Null when GPU tests are disabled or the machine has no usable
	// Vulkan device; GetGpuSkipReason then says why. Any other creation failure fails the calling test.
	[[nodiscard]] GraphicsDevice* GetSharedGraphicsDevice();
	[[nodiscard]] const std::string& GetGpuSkipReason();

	// Destroys the shared device (main() calls it before the log shuts down) and returns the number of errors logged
	// while it was torn down, such as Vulkan objects that a test leaked.
	[[nodiscard]] uint64_t ShutdownSharedGraphicsDevice();

	// Scope of one GPU test: provides the shared device, and checks on exit that the test caused no validation errors.
	// Converts to false when there is no device (the test should then return; see GB_GPU_TEST).
	class GpuTestScope
	{
	public:
		GpuTestScope();
		~GpuTestScope();
		GpuTestScope(const GpuTestScope&) = delete;
		GpuTestScope& operator=(const GpuTestScope&) = delete;

		[[nodiscard]] explicit operator bool() const noexcept { return m_Device != nullptr; }
		[[nodiscard]] GraphicsDevice& GetDevice() const { return *m_Device; }

	private:
		GraphicsDevice* m_Device;
		uint64_t m_ErrorsAtStart = 0;
	};
}

// Starts a GPU test: declares `GraphicsDevice& name` (the shared headless device) or, without a GPU, reports why and
// returns, so the test passes without testing anything (D-005). Use it first in every test of the "gpu" suite.
#define GB_GPU_TEST(name)                                                                                              \
	const ::Gambit::Tests::GpuTestScope gbGpuTestScope;                                                                \
	if (!gbGpuTestScope)                                                                                               \
	{                                                                                                                  \
		MESSAGE("skipped: ", ::Gambit::Tests::GetGpuSkipReason());                                                     \
		return;                                                                                                        \
	}                                                                                                                  \
	::Gambit::GraphicsDevice& name = gbGpuTestScope.GetDevice()
