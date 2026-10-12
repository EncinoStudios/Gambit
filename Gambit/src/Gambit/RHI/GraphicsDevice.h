#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/Buffer.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Math.h"
#include "Gambit/RHI/Image.h"

#include <nvrhi/nvrhi.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Gambit
{
	class ShaderCache;
	class Window;

	// Error::Code values of GraphicsDevice::Create, so that callers can tell "this machine cannot render" (the first
	// two) from a broken setup.
	enum class GraphicsDeviceError : int
	{
		Unspecified = 0,
		VulkanUnavailable,    // no Vulkan loader, no instance, or no Vulkan device at all
		NoSuitableDevice,     // Vulkan devices exist, but none has the required version, features and queues
		InvalidSpecification, // the specification or the window cannot be used
		InitializationFailed  // a Vulkan or NVRHI call failed while setting up the device or the swap chain
	};

	[[nodiscard]] inline bool HasGraphicsDeviceError(const Error& error, GraphicsDeviceError code) noexcept
	{
		return error.Code == static_cast<int>(code);
	}

	enum class GpuType : uint8_t
	{
		Other,
		Integrated,
		Discrete,
		Virtual,
		Cpu
	};

	[[nodiscard]] std::string_view ToString(GpuType type) noexcept;

	// How to create a GraphicsDevice. The defaults suit a windowed game.
	struct GraphicsDeviceSpecification
	{
		std::string ApplicationName = "Gambit";
		// The Khronos validation layer plus NVRHI's validation layer. When the Khronos layer is not installed a warning
		// is logged and the device runs without it (D-030: on in Debug when available).
#if defined(GB_DEBUG)
		bool EnableValidation = true;
#else
		bool EnableValidation = false;
#endif
		bool VSync = true;
		// GPU choice, among the devices that meet the requirements: the one at this index of the device list (logged at
		// creation), else the first whose name contains PreferredGpuName (case-insensitive), else the best type
		// (discrete, integrated, virtual, CPU). A preference that matches no usable device is reported and ignored.
		std::optional<uint32_t> PreferredGpuIndex;
		std::string PreferredGpuName;
		// Offscreen back buffers instead of a window's swap chain; the frame API is the same.
		bool Headless = false;
		// Headless: the exact format of the back buffers. Windowed: an sRGB format asks for an 8-bit sRGB swap chain,
		// any other format for an 8-bit UNORM one; GetBackBufferFormat says what the surface provided.
		nvrhi::Format BackBufferFormat = nvrhi::Format::SRGBA8_UNORM;
		// Headless back-buffer size in pixels; a swap chain follows the window's framebuffer size instead.
		uint32_t BackBufferWidth = 1280;
		uint32_t BackBufferHeight = 720;
		// How many frames the CPU may prepare while the GPU still works on earlier ones (1 to MaxFramesInFlight).
		uint32_t FramesInFlight = 2;
	};

	// Optional device features. They are enabled when the GPU supports them; check before relying on one.
	struct GraphicsDeviceFeatures
	{
		bool GeometryShader = false;
		bool SamplerAnisotropy = false;
		bool ImageCubeArray = false;
		bool FillModeNonSolid = false; // wireframe rasterization
		bool DepthClamp = false;
		bool IndependentBlend = false;
		bool TextureCompressionBC = false;
		bool ShaderStorageImageWriteWithoutFormat = false;
		bool ShaderDrawParameters = false; // gl_BaseInstance, gl_BaseVertex, gl_DrawID
	};

	// Device limits that renderers commonly need.
	struct GraphicsDeviceLimits
	{
		uint32_t MaxTextureDimension2D = 0;
		uint32_t MaxTextureDimensionCube = 0;
		uint32_t MaxTextureArrayLayers = 0;
		uint32_t MaxColorAttachments = 0;
		uint32_t MaxPushConstantsSize = 0;
		uint32_t MaxBoundDescriptorSets = 0;
		uint32_t MaxComputeWorkGroupInvocations = 0;
		glm::uvec3 MaxComputeWorkGroupSize{ 0 };
		glm::uvec3 MaxComputeWorkGroupCount{ 0 };
		float MaxSamplerAnisotropy = 0.0f;
		uint64_t MinUniformBufferOffsetAlignment = 0;
		uint64_t MinStorageBufferOffsetAlignment = 0;
		float TimestampPeriodNs = 0.0f;
	};

	// What the device is, for logs, the editor and capability checks.
	struct GraphicsDeviceInfo
	{
		std::string Name;
		GpuType Type = GpuType::Other;
		uint32_t VendorId = 0;
		uint32_t DeviceId = 0;
		std::string ApiVersion; // the device's Vulkan version, "1.4.318"
		std::string DriverName; // "llvmpipe"; empty when the driver does not say
		std::string DriverInfo; // "Mesa 25.2.8 (LLVM 20.1.2)"
		bool ValidationEnabled = false;
		GraphicsDeviceFeatures Features;
		GraphicsDeviceLimits Limits;
	};

	// The GPU (D-030): a Vulkan device driven through NVRHI. It presents to a window (GLFW surface and swap chain) or
	// renders into offscreen back buffers (headless); the frame API is the same for both:
	//
	//     if (device.BeginFrame())
	//     {
	//         // record and execute command lists that draw into device.GetCurrentFramebuffer()
	//         device.Present();
	//     }
	//
	// Use it from one thread (the main thread). Every NVRHI object created with it must be released before the device
	// is destroyed. Several devices may exist at the same time.
	class GraphicsDevice
	{
	public:
		static constexpr uint32_t MaxFramesInFlight = 4;

		virtual ~GraphicsDevice() = default;
		GraphicsDevice(const GraphicsDevice&) = delete;
		GraphicsDevice& operator=(const GraphicsDevice&) = delete;

		// Creates the device, presenting to `window` unless the specification is headless (then `window` must be null).
		// The error's Code is a GraphicsDeviceError; the message lists what is missing when no GPU is suitable.
		[[nodiscard]] static Result<Scope<GraphicsDevice>>
		Create(const GraphicsDeviceSpecification& specification, Window* window = nullptr);

		// Starts a frame: waits until the GPU has finished the frame that last used this frame slot, then acquires the
		// next back buffer (recreating the swap chain when the window's size or the VSync setting changed, or the swap
		// chain went out of date). Returns false when nothing can be rendered now (zero-sized window, lost device);
		// then skip rendering and Present.
		[[nodiscard]] virtual bool BeginFrame() = 0;
		// Finishes the frame of a successful BeginFrame: presents the back buffer (windowed) and lets NVRHI release
		// resources the GPU no longer uses. Returns false when the device was lost.
		virtual bool Present() = 0;

		// The back buffer of the current frame and a framebuffer with it as the only color attachment. Valid between
		// BeginFrame and Present; never keep them across frames, a resize replaces them.
		[[nodiscard]] virtual nvrhi::ITexture* GetCurrentBackBuffer() const = 0;
		[[nodiscard]] virtual nvrhi::IFramebuffer* GetCurrentFramebuffer() const = 0;
		[[nodiscard]] virtual uint32_t GetCurrentBackBufferIndex() const = 0;
		[[nodiscard]] virtual uint32_t GetBackBufferCount() const = 0;
		[[nodiscard]] virtual glm::uvec2 GetBackBufferSize() const = 0;
		[[nodiscard]] virtual nvrhi::Format GetBackBufferFormat() const = 0;
		// Frames presented so far.
		[[nodiscard]] virtual uint64_t GetFrameCount() const = 0;
		// Whether a successful BeginFrame still awaits its Present.
		[[nodiscard]] virtual bool IsInFrame() const = 0;

		// Headless only: replaces the back buffers with new ones of this size, after waiting for the GPU. A swap chain
		// follows its window instead, so this fails for a windowed device. Not allowed between BeginFrame and Present.
		[[nodiscard]] virtual Result<void> ResizeBackBuffers(uint32_t width, uint32_t height) = 0;

		[[nodiscard]] virtual bool IsVSync() const = 0;
		// Takes effect at the next BeginFrame, which recreates the swap chain with the matching present mode.
		virtual void SetVSync(bool enabled) = 0;

		// Waits until the GPU has finished all submitted work, then releases what NVRHI kept alive for that work.
		virtual void WaitIdle() = 0;

		[[nodiscard]] virtual nvrhi::IDevice* GetNvrhiDevice() const = 0;
		[[nodiscard]] virtual const GraphicsDeviceInfo& GetInfo() const = 0;
		[[nodiscard]] virtual bool IsHeadless() const = 0;
		[[nodiscard]] virtual bool IsDeviceLost() const = 0;
		// Errors reported by the Vulkan validation layer and by NVRHI (its validation layer and runtime checks) since
		// the device was created. They are logged as they happen; tests assert that the count stays 0.
		[[nodiscard]] virtual uint64_t GetValidationErrorCount() const = 0;

		// The shader cache of this device (ShaderLibrary shaders turned into NVRHI shader objects once).
		[[nodiscard]] virtual ShaderCache& GetShaderCache() = 0;

		// Copies one mip level of one array slice (a cube face is a slice) of a 2D texture to the CPU, waiting for the
		// GPU. The texture's state between command lists must be known to NVRHI (keepInitialState or a permanent state,
		// as for back buffers). Formats: R8/RG8/RGBA8/BGRA8 UNORM or sRGB (bytes copied as stored) -> RGBA8,
		// R16F/RG16F/RGBA16F -> RGBA16F, R32F/RG32F/RGBA32F -> RGBA32F; missing channels read as 0 and alpha as 1.
		[[nodiscard]] Result<Image>
		ReadbackTexture(nvrhi::ITexture* texture, uint32_t mipLevel = 0, uint32_t arraySlice = 0);
		// Copies `size` bytes from `offset` of a buffer to the CPU, waiting for the GPU. The same state rule applies.
		[[nodiscard]] Result<Buffer> ReadbackBuffer(nvrhi::IBuffer* buffer, uint64_t offset, uint64_t size);

	protected:
		GraphicsDevice() = default;
	};
}
