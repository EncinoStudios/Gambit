#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/Core/Application.h"
#include "Gambit/RHI/GraphicsDevice.h"

#include <nvrhi/utils.h>

#include <string>
#include <vector>

using namespace Gambit;

namespace
{
	// Clears the back buffer in OnRender and records what the frame looked like.
	class ClearLayer final : public Layer
	{
	public:
		ClearLayer()
			: Layer("Clear")
		{}

		void OnRender(float /*interpolationAlpha*/) override
		{
			GraphicsDevice& device = Application::Get().GetGraphicsDevice();
			const nvrhi::CommandListHandle commandList = device.GetNvrhiDevice()->createCommandList();
			commandList->open();
			nvrhi::utils::ClearColorAttachment(
				commandList, device.GetCurrentFramebuffer(), 0, nvrhi::Color(1.0f, 0.0f, 0.0f, 1.0f));
			commandList->close();
			device.GetNvrhiDevice()->executeCommandList(commandList);

			BackBufferIndices.push_back(device.GetCurrentBackBufferIndex());
			Result<Image> image = device.ReadbackTexture(device.GetCurrentBackBuffer());
			if (image)
			{
				LastFrame = std::move(*image);
			}
		}

		std::vector<uint32_t> BackBufferIndices;
		Image LastFrame;
	};
}

TEST_CASE(
	"Core: Application renders its layers between the graphics device's BeginFrame and Present"
	* doctest::test_suite("gpu"))
{
	if (Tests::AreGpuTestsDisabled())
	{
		MESSAGE("skipped: GPU tests are disabled (GB_TEST_NO_GPU=1)");
		return;
	}
	ApplicationSpecification specification;
	specification.Name = "GraphicsApplication";
	specification.Headless = true;
	specification.EnableGraphics = true;
	specification.Graphics.EnableValidation = true;
	specification.Graphics.BackBufferWidth = 32;
	specification.Graphics.BackBufferHeight = 16;
	Application application(specification);
	auto* layer = static_cast<ClearLayer*>(application.PushLayer(CreateScope<ClearLayer>()));

	const Result<void> initialized = application.Initialize();
	if (!initialized
	    && (HasGraphicsDeviceError(initialized.GetError(), GraphicsDeviceError::VulkanUnavailable)
	        || HasGraphicsDeviceError(initialized.GetError(), GraphicsDeviceError::NoSuitableDevice)))
	{
		MESSAGE("skipped: ", initialized.GetError().Message);
		return;
	}
	const std::string problem = initialized.HasValue() ? std::string() : initialized.GetError().Message;
	REQUIRE_MESSAGE(initialized.HasValue(), problem);
	REQUIRE(application.HasGraphicsDevice());
	GraphicsDevice& device = application.GetGraphicsDevice();
	CHECK(device.IsHeadless()); // taken from the application's Headless
	CHECK(device.GetBackBufferSize() == glm::uvec2(32, 16));

	application.RunFrames(3);
	CHECK(device.GetFrameCount() == 3);
	CHECK(layer->BackBufferIndices == std::vector<uint32_t>{ 0, 1, 0 });
	REQUIRE_FALSE(layer->LastFrame.IsEmpty());
	CHECK(layer->LastFrame.GetPixel(31, 15) == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

	device.WaitIdle();
	CHECK(device.GetValidationErrorCount() == 0);
}

TEST_CASE("Core: ApplyStandardArguments --no-gpu turns the graphics device off")
{
	ApplicationSpecification specification;
	specification.EnableGraphics = true;
	const char* argv[] = { "app", "--headless" };
	REQUIRE(ApplyStandardArguments(ApplicationCommandLineArgs(2, argv), specification).HasValue());
	CHECK(specification.EnableGraphics);

	const char* noGpu[] = { "app", "--no-gpu" };
	REQUIRE(ApplyStandardArguments(ApplicationCommandLineArgs(2, noGpu), specification).HasValue());
	CHECK_FALSE(specification.EnableGraphics);
}

TEST_CASE("Core: Application without graphics has no graphics device")
{
	ApplicationSpecification specification;
	specification.Name = "NoGraphics";
	specification.Headless = true;
	Application application(specification);
	REQUIRE(application.Initialize().HasValue());
	CHECK_FALSE(application.HasGraphicsDevice());
}
