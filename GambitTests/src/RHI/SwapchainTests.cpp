#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/Platform/Window.h"
#include "Gambit/RHI/GraphicsDevice.h"

#include <GLFW/glfw3.h>
#include <nvrhi/utils.h>

#include <string>

using namespace Gambit;

namespace
{
	// Clears the back buffer and presents; returns what the back buffer read back as in the middle of the frame.
	glm::vec4 RenderFrame(GraphicsDevice& device, const nvrhi::Color& color)
	{
		REQUIRE(device.BeginFrame());
		const nvrhi::CommandListHandle commandList = device.GetNvrhiDevice()->createCommandList();
		commandList->open();
		nvrhi::utils::ClearColorAttachment(commandList, device.GetCurrentFramebuffer(), 0, color);
		commandList->close();
		device.GetNvrhiDevice()->executeCommandList(commandList);
		const Result<Image> image = device.ReadbackTexture(device.GetCurrentBackBuffer());
		const std::string problem = image.HasValue() ? std::string() : image.GetError().Message;
		REQUIRE_MESSAGE(image.HasValue(), problem);
		CHECK(image->GetWidth() == device.GetBackBufferSize().x);
		CHECK(image->GetHeight() == device.GetBackBufferSize().y);
		CHECK(device.Present());
		return image->GetPixel(image->GetWidth() / 2, image->GetHeight() / 2);
	}
}

// Needs a display (Xvfb in CI) and a Vulkan device with presentation support; it returns early, passing, without either
// (like the other window tests) or when GPU tests are disabled.
TEST_CASE(
	"RHI: a windowed device presents, follows resizes and recreates its swap chain" * doctest::test_suite("window"))
{
	if (Tests::AreGpuTestsDisabled())
	{
		MESSAGE("skipped: GPU tests are disabled (GB_TEST_NO_GPU=1)");
		return;
	}
	WindowSpecification windowSpecification;
	windowSpecification.Title = "Gambit swap chain test";
	windowSpecification.Width = 320;
	windowSpecification.Height = 240;
	windowSpecification.Visible = false;
	Result<Scope<Window>> window = Window::Create(windowSpecification);
	if (!window)
	{
		MESSAGE("skipped: no window can be opened in this environment: ", window.GetError().Message);
		return;
	}

	GraphicsDeviceSpecification specification;
	specification.ApplicationName = "Swap chain test";
	specification.EnableValidation = true;
	specification.VSync = false;
	Result<Scope<GraphicsDevice>> created = GraphicsDevice::Create(specification, window->get());
	if (!created
	    && (HasGraphicsDeviceError(created.GetError(), GraphicsDeviceError::VulkanUnavailable)
	        || HasGraphicsDeviceError(created.GetError(), GraphicsDeviceError::NoSuitableDevice)))
	{
		MESSAGE("skipped: ", created.GetError().Message);
		return;
	}
	const std::string problem = created.HasValue() ? std::string() : created.GetError().Message;
	REQUIRE_MESSAGE(created.HasValue(), problem);
	Scope<GraphicsDevice> device = std::move(*created);

	CHECK_FALSE(device->IsHeadless());
	CHECK(device->GetBackBufferSize() == (*window)->GetFramebufferSize());
	CHECK(device->GetBackBufferCount() >= 2);
	CHECK(nvrhi::getFormatInfo(device->GetBackBufferFormat()).isSRGB);
	CHECK_FALSE(device->ResizeBackBuffers(64, 64).HasValue()); // a swap chain follows its window

	for (int frame = 0; frame < 3; ++frame)
	{
		CHECK(RenderFrame(*device, nvrhi::Color(0.0f, 0.0f, 1.0f, 1.0f)) == glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
	}
	CHECK(device->GetFrameCount() == 3);

	// A new window size gets a new swap chain at the next frame.
	glfwSetWindowSize(static_cast<GLFWwindow*>((*window)->GetNativeHandle()), 400, 300);
	(*window)->PollEvents();
	const glm::uvec2 resized = (*window)->GetFramebufferSize();
	REQUIRE(resized != glm::uvec2(320, 240));
	CHECK(RenderFrame(*device, nvrhi::Color(1.0f, 1.0f, 0.0f, 1.0f)) == glm::vec4(1.0f, 1.0f, 0.0f, 1.0f));
	CHECK(device->GetBackBufferSize() == resized);

	// So does a new VSync setting.
	device->SetVSync(true);
	CHECK(RenderFrame(*device, nvrhi::Color(0.0f, 1.0f, 0.0f, 1.0f)) == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
	CHECK(device->IsVSync());
	CHECK(device->GetFrameCount() == 5);

	device->WaitIdle();
	CHECK(device->GetValidationErrorCount() == 0);
	device.reset(); // before the window it presents to
}
