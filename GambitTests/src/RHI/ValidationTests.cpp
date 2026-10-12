#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/RHI/GraphicsDevice.h"

#include <vulkan/vulkan.hpp>

using namespace Gambit;

// The zero-error checks of the GPU tests are only meaningful if errors are really counted: this test provokes them on a
// device of its own (so the shared device stays clean) through both paths, NVRHI's validation layer and the Vulkan
// debug messenger.
TEST_CASE("RHI: validation errors are logged and counted" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(shared);
	static_cast<void>(shared);

	GraphicsDeviceSpecification specification;
	specification.ApplicationName = "Validation test";
	specification.EnableValidation = true;
	specification.Headless = true;
	specification.BackBufferWidth = 8;
	specification.BackBufferHeight = 8;
	Result<Scope<GraphicsDevice>> created = GraphicsDevice::Create(specification);
	REQUIRE(created.HasValue());
	Scope<GraphicsDevice> device = std::move(*created);
	REQUIRE(device->GetValidationErrorCount() == 0);

	Tests::ScopedLogCapture capture; // the provoked errors must not reach the console

	// NVRHI's validation layer refuses a volatile buffer that is not a constant buffer.
	const nvrhi::BufferHandle buffer = device->GetNvrhiDevice()->createBuffer(
		nvrhi::BufferDesc().setByteSize(16).setIsVolatile(true).setDebugName("Bad"));
	CHECK(buffer == nullptr);
	CHECK(device->GetValidationErrorCount() == 1);
	CHECK(capture.Contains(LogLevel::Error, "NVRHI: Buffer Bad is volatile"));

	// Messages of the Vulkan debug messenger: validation errors count, warnings and general errors are only logged.
	const auto instance =
		static_cast<VkInstance>(device->GetNvrhiDevice()->getNativeObject(nvrhi::ObjectTypes::VK_Instance).pointer);
	REQUIRE(instance != VK_NULL_HANDLE);
	if (VULKAN_HPP_DEFAULT_DISPATCHER.vkSubmitDebugUtilsMessageEXT == nullptr)
	{
		MESSAGE("VK_EXT_debug_utils is not available: the messenger path is not tested");
		return;
	}
	const auto submit = [&](vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT types,
	                        const char* text) {
		const vk::DebugUtilsMessengerCallbackDataEXT data = vk::DebugUtilsMessengerCallbackDataEXT().setPMessage(text);
		vk::Instance(instance).submitDebugUtilsMessageEXT(severity, types, data);
	};
	submit(
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
		"provoked validation error");
	CHECK(device->GetValidationErrorCount() == 2);
	CHECK(capture.Contains(LogLevel::Error, "Vulkan validation: provoked validation error"));

	submit(
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning, vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
		"provoked validation warning");
	submit(
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral,
		"provoked loader error");
	CHECK(device->GetValidationErrorCount() == 2);
	CHECK(capture.Contains(LogLevel::Warn, "Vulkan validation: provoked validation warning"));
	CHECK(capture.Contains(LogLevel::Error, "Vulkan: provoked loader error"));
	// Each device counts only its own errors: the scope of GB_GPU_TEST checks that the shared device saw none.
}
