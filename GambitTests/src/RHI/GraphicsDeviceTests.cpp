#include <doctest/doctest.h>

#include "TestSupport.h"
#include "TestUtils/Golden.h"
#include "TestUtils/GpuTest.h"

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/PipelineUtils.h"
#include "Gambit/RHI/ShaderCache.h"

#include <glm/gtc/packing.hpp>
#include <nvrhi/utils.h>

#include <array>
#include <cstring>
#include <functional>
#include <vector>

using namespace Gambit;

namespace
{
	// A 2D texture that NVRHI may copy to and from between command lists (it keeps a known resting state).
	nvrhi::TextureHandle CreateTexture(
		GraphicsDevice& device, uint32_t width, uint32_t height, nvrhi::Format format, bool renderTarget,
		uint32_t mipLevels = 1, uint32_t arraySize = 1)
	{
		const nvrhi::TextureDesc desc =
			nvrhi::TextureDesc()
				.setWidth(width)
				.setHeight(height)
				.setFormat(format)
				.setMipLevels(mipLevels)
				.setArraySize(arraySize)
				.setDimension(
					arraySize > 1 ? nvrhi::TextureDimension::Texture2DArray : nvrhi::TextureDimension::Texture2D)
				.setIsRenderTarget(renderTarget)
				.setInitialState(nvrhi::ResourceStates::ShaderResource)
				.setKeepInitialState(true)
				.setDebugName("Test texture");
		nvrhi::TextureHandle texture = device.GetNvrhiDevice()->createTexture(desc);
		REQUIRE(texture != nullptr);
		return texture;
	}

	// Records commands into a fresh command list and executes it.
	void Execute(GraphicsDevice& device, const std::function<void(nvrhi::ICommandList&)>& record)
	{
		const nvrhi::CommandListHandle commandList = device.GetNvrhiDevice()->createCommandList();
		REQUIRE(commandList != nullptr);
		commandList->open();
		record(*commandList);
		commandList->close();
		device.GetNvrhiDevice()->executeCommandList(commandList);
	}

	Image ReadBack(GraphicsDevice& device, nvrhi::ITexture* texture, uint32_t mipLevel = 0, uint32_t arraySlice = 0)
	{
		Result<Image> image = device.ReadbackTexture(texture, mipLevel, arraySlice);
		const std::string problem = image.HasValue() ? std::string() : image.GetError().Message;
		REQUIRE_MESSAGE(image.HasValue(), problem);
		return std::move(*image);
	}

	// Bytes with a different value in every position, so that swapped rows or channels show up.
	std::vector<std::byte> MakeBytes(size_t count, uint32_t seed)
	{
		std::vector<std::byte> bytes(count);
		for (size_t i = 0; i < count; ++i)
		{
			bytes[i] = std::byte(static_cast<uint8_t>((i * 31 + seed * 7 + (i >> 8)) & 0xFF));
		}
		return bytes;
	}

	uint16_t Half(float value)
	{
		return glm::packHalf1x16(value);
	}
}

TEST_CASE("RHI: the shared headless device describes itself" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	const GraphicsDeviceInfo& info = device.GetInfo();
	CHECK_FALSE(info.Name.empty());
	CHECK(info.ApiVersion.starts_with("1."));
	CHECK(info.Limits.MaxTextureDimension2D >= 4096);
	CHECK(info.Limits.MaxTextureArrayLayers >= 256);
	CHECK(info.Limits.MaxColorAttachments >= 4);
	CHECK(info.Limits.MaxPushConstantsSize >= 128);
	CHECK(info.Limits.MaxBoundDescriptorSets >= 4);
	CHECK(info.Limits.MaxComputeWorkGroupInvocations >= 128);
	CHECK(info.Limits.MaxComputeWorkGroupSize.x >= 128);
	CHECK(info.Limits.MinUniformBufferOffsetAlignment >= 1);
	if (!info.ValidationEnabled)
	{
		MESSAGE("the Vulkan validation layer is not installed: only NVRHI's validation checks these tests");
	}

	CHECK(device.IsHeadless());
	CHECK_FALSE(device.IsDeviceLost());
	CHECK(device.GetBackBufferCount() == 2);
	CHECK(device.GetBackBufferSize() == glm::uvec2(256, 256));
	CHECK(device.GetBackBufferFormat() == nvrhi::Format::SRGBA8_UNORM);
	CHECK(device.GetNvrhiDevice()->getGraphicsAPI() == nvrhi::GraphicsAPI::VULKAN);
	CHECK(device.GetValidationErrorCount() == 0);
}

TEST_CASE("RHI: GraphicsDevice::Create rejects invalid specifications")
{
	GraphicsDeviceSpecification specification;
	specification.Headless = true;

	specification.FramesInFlight = 0;
	Result<Scope<GraphicsDevice>> device = GraphicsDevice::Create(specification);
	REQUIRE_FALSE(device.HasValue());
	CHECK(HasGraphicsDeviceError(device.GetError(), GraphicsDeviceError::InvalidSpecification));

	specification.FramesInFlight = GraphicsDevice::MaxFramesInFlight + 1;
	CHECK(HasGraphicsDeviceError(
		GraphicsDevice::Create(specification).GetError(), GraphicsDeviceError::InvalidSpecification));

	specification.FramesInFlight = 2;
	specification.BackBufferWidth = 0;
	CHECK(HasGraphicsDeviceError(
		GraphicsDevice::Create(specification).GetError(), GraphicsDeviceError::InvalidSpecification));

	specification.BackBufferWidth = 64;
	specification.Headless = false; // windowed without a window
	CHECK(HasGraphicsDeviceError(
		GraphicsDevice::Create(specification).GetError(), GraphicsDeviceError::InvalidSpecification));
}

TEST_CASE("RHI: a second headless device with other settings works next to the shared one" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(shared);

	GraphicsDeviceSpecification specification;
	specification.ApplicationName = "Second device";
	specification.EnableValidation = true;
	specification.Headless = true;
	specification.BackBufferFormat = nvrhi::Format::RGBA16_FLOAT;
	specification.BackBufferWidth = 64;
	specification.BackBufferHeight = 32;
	specification.FramesInFlight = 3;
	specification.PreferredGpuName = "no GPU has this name";

	Scope<GraphicsDevice> device;
	{
		Tests::ScopedLogCapture capture; // the unmatched preference is reported as a warning
		Result<Scope<GraphicsDevice>> created = GraphicsDevice::Create(specification);
		const std::string problem = created.HasValue() ? std::string() : created.GetError().Message;
		REQUIRE_MESSAGE(created.HasValue(), problem);
		device = std::move(*created);
		CHECK(capture.Contains(LogLevel::Warn, "no GPU has this name"));
	}
	CHECK(device->GetInfo().Name == shared.GetInfo().Name);
	CHECK(device->GetBackBufferCount() == 3);
	CHECK(device->GetBackBufferSize() == glm::uvec2(64, 32));
	CHECK(device->GetBackBufferFormat() == nvrhi::Format::RGBA16_FLOAT);

	std::vector<uint32_t> indices;
	for (int frame = 0; frame < 4; ++frame)
	{
		REQUIRE(device->BeginFrame());
		indices.push_back(device->GetCurrentBackBufferIndex());
		Execute(*device, [&](nvrhi::ICommandList& commandList) {
			nvrhi::utils::ClearColorAttachment(
				&commandList, device->GetCurrentFramebuffer(), 0, nvrhi::Color(2.0f, -1.0f, 0.5f, 1.0f));
		});
		const Image image = ReadBack(*device, device->GetCurrentBackBuffer());
		CHECK(image.GetFormat() == ImageFormat::RGBA16F);
		CHECK(image.GetPixel(63, 31) == glm::vec4(2.0f, -1.0f, 0.5f, 1.0f));
		CHECK(device->Present());
	}
	CHECK(indices == std::vector<uint32_t>{ 0, 1, 2, 0 });
	CHECK(device->GetFrameCount() == 4);
	device->WaitIdle();
	CHECK(device->GetValidationErrorCount() == 0);
}

TEST_CASE("RHI: the shared device's back buffers rotate with the frames" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	const uint64_t framesBefore = device.GetFrameCount();
	std::vector<uint32_t> indices;
	for (int frame = 0; frame < 4; ++frame)
	{
		REQUIRE(device.BeginFrame());
		indices.push_back(device.GetCurrentBackBufferIndex());
		REQUIRE(device.GetCurrentBackBuffer() != nullptr);
		REQUIRE(device.GetCurrentFramebuffer() != nullptr);
		CHECK(device.GetCurrentFramebuffer()->getDesc().colorAttachments[0].texture == device.GetCurrentBackBuffer());
		CHECK(device.Present());
	}
	CHECK(device.GetFrameCount() == framesBefore + 4);
	CHECK(indices[0] != indices[1]);
	CHECK(indices[0] == indices[2]);
	CHECK(indices[1] == indices[3]);
}

TEST_CASE("RHI: cleared textures read back exactly" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);

	const nvrhi::TextureHandle unorm = CreateTexture(device, 8, 4, nvrhi::Format::RGBA8_UNORM, true);
	const nvrhi::TextureHandle floats = CreateTexture(device, 5, 3, nvrhi::Format::RGBA32_FLOAT, true);
	Execute(device, [&](nvrhi::ICommandList& commandList) {
		commandList.clearTextureFloat(
			unorm, nvrhi::AllSubresources, nvrhi::Color(64.0f / 255.0f, 128.0f / 255.0f, 192.0f / 255.0f, 1.0f));
		commandList.clearTextureFloat(floats, nvrhi::AllSubresources, nvrhi::Color(0.1f, -2.0f, 1.0e6f, 0.5f));
	});

	const Image unormImage = ReadBack(device, unorm);
	REQUIRE(unormImage.GetWidth() == 8);
	REQUIRE(unormImage.GetHeight() == 4);
	for (uint32_t y = 0; y < 4; ++y)
	{
		const std::span<const std::byte> row = unormImage.GetRow(y);
		for (uint32_t x = 0; x < 8; ++x)
		{
			CHECK(std::to_integer<int>(row[x * 4 + 0]) == 64);
			CHECK(std::to_integer<int>(row[x * 4 + 1]) == 128);
			CHECK(std::to_integer<int>(row[x * 4 + 2]) == 192);
			CHECK(std::to_integer<int>(row[x * 4 + 3]) == 255);
		}
	}
	const Image floatImage = ReadBack(device, floats);
	CHECK(floatImage.GetFormat() == ImageFormat::RGBA32F);
	CHECK(floatImage.GetPixel(0, 0) == glm::vec4(0.1f, -2.0f, 1.0e6f, 0.5f));
	CHECK(floatImage.GetPixel(4, 2) == glm::vec4(0.1f, -2.0f, 1.0e6f, 0.5f));

	// The back buffer itself, within a frame (sRGB 1 and 0 are exact).
	REQUIRE(device.BeginFrame());
	Execute(device, [&](nvrhi::ICommandList& commandList) {
		nvrhi::utils::ClearColorAttachment(
			&commandList, device.GetCurrentFramebuffer(), 0, nvrhi::Color(1.0f, 0.0f, 1.0f, 1.0f));
	});
	const Image backBuffer = ReadBack(device, device.GetCurrentBackBuffer());
	CHECK(backBuffer.GetWidth() == 256);
	CHECK(backBuffer.GetPixel(0, 0) == glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
	CHECK(backBuffer.GetPixel(255, 255) == glm::vec4(1.0f, 0.0f, 1.0f, 1.0f));
	CHECK(device.Present());
}

TEST_CASE("RHI: the triangle matches its golden image" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	nvrhi::IDevice& nvrhiDevice = *device.GetNvrhiDevice();

	const nvrhi::TextureHandle target = CreateTexture(device, 128, 128, nvrhi::Format::RGBA8_UNORM, true);
	const nvrhi::FramebufferHandle framebuffer =
		nvrhiDevice.createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
	REQUIRE(framebuffer != nullptr);

	const Result<nvrhi::ShaderHandle> vertexShader = device.GetShaderCache().Get("Triangle.vert");
	const Result<nvrhi::ShaderHandle> pixelShader = device.GetShaderCache().Get("Triangle.frag");
	REQUIRE(vertexShader.HasValue());
	REQUIRE(pixelShader.HasValue());
	// The default raster state culls back faces: the triangle is clockwise on screen, NVRHI's front-face convention.
	nvrhi::GraphicsPipelineDesc pipelineDesc;
	pipelineDesc.setVertexShader(*vertexShader).setPixelShader(*pixelShader);
	pipelineDesc.renderState.depthStencilState.disableDepthTest().disableDepthWrite();
	const nvrhi::GraphicsPipelineHandle pipeline =
		nvrhiDevice.createGraphicsPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
	REQUIRE(pipeline != nullptr);

	Execute(device, [&](nvrhi::ICommandList& commandList) {
		nvrhi::utils::ClearColorAttachment(&commandList, framebuffer, 0, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		commandList.setGraphicsState(MakeGraphicsState(pipeline, framebuffer));
		commandList.draw(nvrhi::DrawArguments().setVertexCount(3));
	});
	Tests::CheckGoldenTexture(device, target, "Triangle");
}

TEST_CASE("RHI: the fullscreen triangle covers the target with UV (0, 0) at the top left" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	constexpr uint32_t Width = 16;
	constexpr uint32_t Height = 8;
	const nvrhi::TextureHandle target = CreateTexture(device, Width, Height, nvrhi::Format::RGBA32_FLOAT, true);
	const nvrhi::FramebufferHandle framebuffer =
		device.GetNvrhiDevice()->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(target));
	REQUIRE(framebuffer != nullptr);

	const Result<nvrhi::GraphicsPipelineHandle> pipeline =
		CreateFullscreenPipeline(device, "UvGradient.frag", framebuffer->getFramebufferInfo(), {});
	const std::string problem = pipeline.HasValue() ? std::string() : pipeline.GetError().Message;
	REQUIRE_MESSAGE(pipeline.HasValue(), problem);

	Execute(device, [&](nvrhi::ICommandList& commandList) {
		nvrhi::utils::ClearColorAttachment(&commandList, framebuffer, 0, nvrhi::Color(-1.0f));
		DrawFullscreenTriangle(commandList, MakeGraphicsState(*pipeline, framebuffer));
	});
	const Image image = ReadBack(device, target);
	for (uint32_t y = 0; y < Height; ++y)
	{
		for (uint32_t x = 0; x < Width; ++x)
		{
			const glm::vec4 uv = image.GetPixel(x, y);
			CHECK(uv.x == doctest::Approx((x + 0.5f) / Width).epsilon(1e-5));
			CHECK(uv.y == doctest::Approx((y + 0.5f) / Height).epsilon(1e-5));
			CHECK(uv.w == 1.0f);
		}
	}

	CHECK_FALSE(CreateFullscreenPipeline(device, "Missing.frag", framebuffer->getFramebufferInfo(), {}).HasValue());
	CHECK_FALSE(CreateFullscreenPipeline(device, "Triangle.vert", framebuffer->getFramebufferInfo(), {}).HasValue());
}

TEST_CASE("RHI: a compute shader writes a buffer that reads back" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	nvrhi::IDevice& nvrhiDevice = *device.GetNvrhiDevice();
	constexpr uint32_t Count = 1000; // not a multiple of the group size: the shader checks its bounds

	const nvrhi::BufferHandle buffer =
		nvrhiDevice.createBuffer(nvrhi::BufferDesc()
	                                 .setByteSize(Count * 2 * sizeof(float))
	                                 .setStructStride(sizeof(float))
	                                 .setCanHaveUAVs(true)
	                                 .setInitialState(nvrhi::ResourceStates::UnorderedAccess)
	                                 .setKeepInitialState(true)
	                                 .setDebugName("sRGB curve"));
	REQUIRE(buffer != nullptr);

	const Result<nvrhi::BindingLayoutHandle> layout = CreateBindingLayout(
		device,
		MakeBindingLayoutDesc(
			nvrhi::ShaderType::Compute, 0,
			{ nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0),
	          nvrhi::BindingLayoutItem::PushConstants(0, sizeof(uint32_t)) }));
	REQUIRE(layout.HasValue());
	const Result<nvrhi::BindingSetHandle> set = CreateBindingSet(
		device, *layout,
		{ nvrhi::BindingSetItem::StructuredBuffer_UAV(0, buffer),
	      nvrhi::BindingSetItem::PushConstants(0, sizeof(uint32_t)) });
	REQUIRE(set.HasValue());
	const std::array layouts = { *layout };
	const Result<nvrhi::ComputePipelineHandle> pipeline = CreateComputePipeline(device, "SrgbCurve.comp", layouts);
	const std::string problem = pipeline.HasValue() ? std::string() : pipeline.GetError().Message;
	REQUIRE_MESSAGE(pipeline.HasValue(), problem);

	Execute(device, [&](nvrhi::ICommandList& commandList) {
		commandList.setComputeState(nvrhi::ComputeState().setPipeline(*pipeline).addBindingSet(*set));
		const uint32_t count = Count;
		commandList.setPushConstants(&count, sizeof(count));
		commandList.dispatch((Count + 63) / 64);
	});

	const Result<Buffer> bytes = device.ReadbackBuffer(buffer, 0, Count * 2 * sizeof(float));
	REQUIRE(bytes.HasValue());
	REQUIRE(bytes->Size() == Count * 2 * sizeof(float));
	std::vector<float> values(Count * 2);
	std::memcpy(values.data(), bytes->Data(), bytes->Size());
	for (uint32_t i = 0; i < Count; ++i)
	{
		const float x = static_cast<float>(i) / static_cast<float>(Count - 1);
		CHECK(values[2 * i] == doctest::Approx(LinearToSrgb(x)).epsilon(1e-5));
		CHECK(values[2 * i + 1] == doctest::Approx(SrgbToLinear(x)).epsilon(1e-5));
	}

	// A sub-range comes back on its own.
	const Result<Buffer> tail = device.ReadbackBuffer(buffer, (Count * 2 - 2) * sizeof(float), 2 * sizeof(float));
	REQUIRE(tail.HasValue());
	float last[2] = {};
	std::memcpy(last, tail->Data(), sizeof(last));
	CHECK(last[0] == values[Count * 2 - 2]);
	CHECK(last[1] == values[Count * 2 - 1]);

	CHECK_FALSE(CreateComputePipeline(device, "Triangle.frag", layouts).HasValue());
}

TEST_CASE("RHI: textures round-trip through upload and readback" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);

	SUBCASE("RGBA8 with two mips")
	{
		const nvrhi::TextureHandle texture = CreateTexture(device, 16, 8, nvrhi::Format::RGBA8_UNORM, false, 2);
		const std::vector<std::byte> mip0 = MakeBytes(16 * 8 * 4, 1);
		const std::vector<std::byte> mip1 = MakeBytes(8 * 4 * 4, 2);
		Execute(device, [&](nvrhi::ICommandList& commandList) {
			commandList.writeTexture(texture, 0, 0, mip0.data(), 16 * 4);
			commandList.writeTexture(texture, 0, 1, mip1.data(), 8 * 4);
		});
		const Image image0 = ReadBack(device, texture, 0);
		CHECK(std::ranges::equal(image0.GetPixels(), mip0));
		const Image image1 = ReadBack(device, texture, 1);
		CHECK(image1.GetWidth() == 8);
		CHECK(image1.GetHeight() == 4);
		CHECK(std::ranges::equal(image1.GetPixels(), mip1));
	}
	SUBCASE("RGBA16F array slices")
	{
		const nvrhi::TextureHandle texture = CreateTexture(device, 7, 5, nvrhi::Format::RGBA16_FLOAT, false, 1, 3);
		std::vector<std::vector<uint16_t>> slices;
		for (uint32_t slice = 0; slice < 3; ++slice)
		{
			std::vector<uint16_t> texels;
			for (uint32_t i = 0; i < 7 * 5 * 4; ++i)
			{
				texels.push_back(Half(static_cast<float>(i) * 0.25f - 10.0f + static_cast<float>(slice) * 100.0f));
			}
			slices.push_back(std::move(texels));
		}
		Execute(device, [&](nvrhi::ICommandList& commandList) {
			for (uint32_t slice = 0; slice < 3; ++slice)
			{
				commandList.writeTexture(texture, slice, 0, slices[slice].data(), 7 * 4 * sizeof(uint16_t));
			}
		});
		for (uint32_t slice = 0; slice < 3; ++slice)
		{
			const Image image = ReadBack(device, texture, 0, slice);
			CHECK(image.GetFormat() == ImageFormat::RGBA16F);
			REQUIRE(image.GetPixels().size() == slices[slice].size() * sizeof(uint16_t));
			CHECK(std::memcmp(image.GetPixels().data(), slices[slice].data(), image.GetPixels().size()) == 0);
		}
	}
	SUBCASE("BGRA8 is swizzled to RGBA")
	{
		const nvrhi::TextureHandle texture = CreateTexture(device, 2, 1, nvrhi::Format::BGRA8_UNORM, false);
		const std::array<uint8_t, 8> bgra = { 10, 20, 30, 40, 50, 60, 70, 80 };
		Execute(
			device, [&](nvrhi::ICommandList& commandList) { commandList.writeTexture(texture, 0, 0, bgra.data(), 8); });
		const Image image = ReadBack(device, texture);
		CHECK(image.GetPixel(0, 0) == glm::vec4(30.0f, 20.0f, 10.0f, 40.0f) / 255.0f);
		CHECK(image.GetPixel(1, 0) == glm::vec4(70.0f, 60.0f, 50.0f, 80.0f) / 255.0f);
	}
	SUBCASE("narrower formats are expanded with zero color and opaque alpha")
	{
		const nvrhi::TextureHandle rg16 = CreateTexture(device, 2, 2, nvrhi::Format::RG16_FLOAT, false);
		const std::array<uint16_t, 8> rg = { Half(1.5f), Half(-2.0f), Half(3.0f), Half(0.25f),
			                                 Half(4.0f), Half(5.0f),  Half(6.0f), Half(7.0f) };
		const nvrhi::TextureHandle r32 = CreateTexture(device, 3, 1, nvrhi::Format::R32_FLOAT, false);
		const std::array<float, 3> r = { 1.0e-3f, -7.5f, 123456.0f };
		const nvrhi::TextureHandle r8 = CreateTexture(device, 1, 1, nvrhi::Format::R8_UNORM, false);
		const uint8_t red = 77;
		Execute(device, [&](nvrhi::ICommandList& commandList) {
			commandList.writeTexture(rg16, 0, 0, rg.data(), 2 * 2 * sizeof(uint16_t));
			commandList.writeTexture(r32, 0, 0, r.data(), sizeof(r));
			commandList.writeTexture(r8, 0, 0, &red, 1);
		});
		const Image rgImage = ReadBack(device, rg16);
		CHECK(rgImage.GetFormat() == ImageFormat::RGBA16F);
		CHECK(rgImage.GetPixel(0, 0) == glm::vec4(1.5f, -2.0f, 0.0f, 1.0f));
		CHECK(rgImage.GetPixel(1, 1) == glm::vec4(6.0f, 7.0f, 0.0f, 1.0f));
		const Image rImage = ReadBack(device, r32);
		CHECK(rImage.GetFormat() == ImageFormat::RGBA32F);
		CHECK(rImage.GetPixel(2, 0) == glm::vec4(123456.0f, 0.0f, 0.0f, 1.0f));
		const Image r8Image = ReadBack(device, r8);
		CHECK(r8Image.GetPixel(0, 0) == glm::vec4(77.0f / 255.0f, 0.0f, 0.0f, 1.0f));
	}
}

TEST_CASE("RHI: readback rejects what it cannot read" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	nvrhi::IDevice& nvrhiDevice = *device.GetNvrhiDevice();

	CHECK_FALSE(device.ReadbackTexture(nullptr).HasValue());
	const nvrhi::TextureHandle texture = CreateTexture(device, 4, 4, nvrhi::Format::RGBA8_UNORM, false);
	CHECK_FALSE(device.ReadbackTexture(texture, 1).HasValue());
	CHECK_FALSE(device.ReadbackTexture(texture, 0, 1).HasValue());

	const nvrhi::TextureHandle unsupported = CreateTexture(device, 4, 4, nvrhi::Format::RGBA16_UNORM, false);
	const Result<Image> unsupportedImage = device.ReadbackTexture(unsupported);
	REQUIRE_FALSE(unsupportedImage.HasValue());
	CHECK(unsupportedImage.GetError().Message.find("RGBA16_UNORM") != std::string::npos);

	const nvrhi::TextureHandle volume =
		nvrhiDevice.createTexture(nvrhi::TextureDesc()
	                                  .setDimension(nvrhi::TextureDimension::Texture3D)
	                                  .setWidth(4)
	                                  .setHeight(4)
	                                  .setDepth(4)
	                                  .setFormat(nvrhi::Format::RGBA8_UNORM)
	                                  .setInitialState(nvrhi::ResourceStates::ShaderResource)
	                                  .setKeepInitialState(true)
	                                  .setDebugName("Volume"));
	REQUIRE(volume != nullptr);
	CHECK_FALSE(device.ReadbackTexture(volume).HasValue());

	const nvrhi::TextureHandle multisampled =
		nvrhiDevice.createTexture(nvrhi::TextureDesc()
	                                  .setDimension(nvrhi::TextureDimension::Texture2DMS)
	                                  .setWidth(4)
	                                  .setHeight(4)
	                                  .setSampleCount(4)
	                                  .setFormat(nvrhi::Format::RGBA8_UNORM)
	                                  .setIsRenderTarget(true)
	                                  .setInitialState(nvrhi::ResourceStates::RenderTarget)
	                                  .setKeepInitialState(true)
	                                  .setDebugName("Multisampled"));
	REQUIRE(multisampled != nullptr);
	CHECK_FALSE(device.ReadbackTexture(multisampled).HasValue());

	const nvrhi::BufferHandle buffer =
		nvrhiDevice.createBuffer(nvrhi::BufferDesc()
	                                 .setByteSize(64)
	                                 .setInitialState(nvrhi::ResourceStates::ShaderResource)
	                                 .setKeepInitialState(true)
	                                 .setDebugName("Small buffer"));
	REQUIRE(buffer != nullptr);
	CHECK_FALSE(device.ReadbackBuffer(nullptr, 0, 4).HasValue());
	CHECK_FALSE(device.ReadbackBuffer(buffer, 0, 0).HasValue());
	CHECK_FALSE(device.ReadbackBuffer(buffer, 60, 8).HasValue());
	CHECK_FALSE(device.ReadbackBuffer(buffer, 65, 1).HasValue());
	CHECK(device.ReadbackBuffer(buffer, 0, 64).HasValue());
}

TEST_CASE("RHI: headless back buffers can be resized" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	REQUIRE(device.ResizeBackBuffers(96, 48).HasValue());
	CHECK(device.GetBackBufferSize() == glm::uvec2(96, 48));
	CHECK(device.GetBackBufferCount() == 2);

	REQUIRE(device.BeginFrame());
	CHECK_FALSE(device.ResizeBackBuffers(10, 10).HasValue()); // not between BeginFrame and Present
	Execute(device, [&](nvrhi::ICommandList& commandList) {
		nvrhi::utils::ClearColorAttachment(
			&commandList, device.GetCurrentFramebuffer(), 0, nvrhi::Color(0.0f, 1.0f, 0.0f, 1.0f));
	});
	const Image image = ReadBack(device, device.GetCurrentBackBuffer());
	CHECK(image.GetWidth() == 96);
	CHECK(image.GetHeight() == 48);
	CHECK(image.GetPixel(95, 47) == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
	CHECK(device.Present());

	CHECK_FALSE(device.ResizeBackBuffers(0, 48).HasValue());
	CHECK_FALSE(device.ResizeBackBuffers(96, Image::MaxDimension + 1).HasValue());
	CHECK(device.GetBackBufferSize() == glm::uvec2(96, 48)); // a failed resize keeps the old back buffers

	REQUIRE(device.ResizeBackBuffers(256, 256).HasValue()); // restore the shared device
	CHECK(device.GetBackBufferSize() == glm::uvec2(256, 256));
}

TEST_CASE("RHI: the shader cache creates each shader once" * doctest::test_suite("gpu"))
{
	GB_GPU_TEST(device);
	ShaderCache cache(*device.GetNvrhiDevice());
	CHECK(cache.GetSize() == 0);

	const Result<nvrhi::ShaderHandle> first = cache.Get("Triangle.vert");
	REQUIRE(first.HasValue());
	CHECK((*first)->getDesc().shaderType == nvrhi::ShaderType::Vertex);
	CHECK((*first)->getDesc().entryName == "main");
	const Result<nvrhi::ShaderHandle> again = cache.Get("Triangle.vert", nvrhi::ShaderType::Vertex);
	REQUIRE(again.HasValue());
	CHECK(again->Get() == first->Get());
	CHECK(cache.GetSize() == 1);

	CHECK_FALSE(cache.Get("Triangle.vert", nvrhi::ShaderType::Pixel).HasValue());
	CHECK_FALSE(cache.Get("Missing.frag").HasValue());
	CHECK_FALSE(cache.Get("Triangle").HasValue());
	CHECK(cache.GetSize() == 1);

	REQUIRE(cache.Get("SrgbCurve.comp").HasValue());
	CHECK(cache.GetSize() == 2);
	cache.Clear();
	CHECK(cache.GetSize() == 0);
}

TEST_CASE("RHI: binding layouts follow the D-031 convention")
{
	const nvrhi::BindingLayoutDesc desc = MakeBindingLayoutDesc(
		nvrhi::ShaderType::Pixel, 2,
		{ nvrhi::BindingLayoutItem::ConstantBuffer(0), nvrhi::BindingLayoutItem::Texture_SRV(1),
	      nvrhi::BindingLayoutItem::Sampler(2) });
	CHECK(desc.visibility == nvrhi::ShaderType::Pixel);
	CHECK(desc.registerSpace == 2);
	CHECK(desc.registerSpaceIsDescriptorSet);
	CHECK(desc.bindingOffsets.shaderResource == 0);
	CHECK(desc.bindingOffsets.sampler == 0);
	CHECK(desc.bindingOffsets.constantBuffer == 0);
	CHECK(desc.bindingOffsets.unorderedAccess == 0);
	REQUIRE(desc.bindings.size() == 3);
	CHECK(desc.bindings[1].type == nvrhi::ResourceType::Texture_SRV);
	CHECK(desc.bindings[2].slot == 2);
}
