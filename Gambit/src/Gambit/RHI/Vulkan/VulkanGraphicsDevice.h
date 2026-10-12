#pragma once

#include "Gambit/RHI/GraphicsDevice.h"
#include "Gambit/RHI/ShaderCache.h"
#include "Gambit/RHI/Vulkan/VulkanSwapchain.h"

#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace Gambit
{
	// Routes NVRHI's messages to the core logger and counts the errors among them.
	class NvrhiMessageCallback final : public nvrhi::IMessageCallback
	{
	public:
		explicit NvrhiMessageCallback(std::atomic<uint64_t>& errorCount)
			: m_ErrorCount(errorCount)
		{}

		void message(nvrhi::MessageSeverity severity, const char* messageText) override;

	private:
		std::atomic<uint64_t>& m_ErrorCount;
	};

	// GraphicsDevice on Vulkan (D-030). Vulkan is loaded at run time: the process-wide Vulkan-Hpp dispatcher that NVRHI
	// uses is initialized from a live instance (device-level calls then go through the loader's dispatch, which is
	// valid for every device), while this class's own calls use a dispatcher of its own.
	class VulkanGraphicsDevice final : public GraphicsDevice
	{
	public:
		struct PrivateTag
		{
			explicit PrivateTag() = default;
		};

		[[nodiscard]] static Result<Scope<GraphicsDevice>>
		Create(const GraphicsDeviceSpecification& specification, Window* window);

		VulkanGraphicsDevice(PrivateTag, const GraphicsDeviceSpecification& specification, Window* window);
		~VulkanGraphicsDevice() override;

		[[nodiscard]] bool BeginFrame() override;
		bool Present() override;

		[[nodiscard]] nvrhi::ITexture* GetCurrentBackBuffer() const override;
		[[nodiscard]] nvrhi::IFramebuffer* GetCurrentFramebuffer() const override;
		[[nodiscard]] uint32_t GetCurrentBackBufferIndex() const override { return m_BackBufferIndex; }
		[[nodiscard]] uint32_t GetBackBufferCount() const override
		{
			return static_cast<uint32_t>(m_BackBuffers.size());
		}
		[[nodiscard]] glm::uvec2 GetBackBufferSize() const override { return m_BackBufferSize; }
		[[nodiscard]] nvrhi::Format GetBackBufferFormat() const override { return m_BackBufferFormat; }
		[[nodiscard]] uint64_t GetFrameCount() const override { return m_FrameCount; }
		[[nodiscard]] bool IsInFrame() const override { return m_InFrame; }

		[[nodiscard]] Result<void> ResizeBackBuffers(uint32_t width, uint32_t height) override;

		[[nodiscard]] bool IsVSync() const override { return m_Specification.VSync; }
		void SetVSync(bool enabled) override { m_Specification.VSync = enabled; }

		void WaitIdle() override;

		[[nodiscard]] nvrhi::IDevice* GetNvrhiDevice() const override { return m_NvrhiDevice.Get(); }
		[[nodiscard]] const GraphicsDeviceInfo& GetInfo() const override { return m_Info; }
		[[nodiscard]] bool IsHeadless() const override { return m_Specification.Headless; }
		[[nodiscard]] bool IsDeviceLost() const override { return m_DeviceLost; }
		[[nodiscard]] uint64_t GetValidationErrorCount() const override { return m_ValidationErrorCount.load(); }

		[[nodiscard]] ShaderCache& GetShaderCache() override { return *m_ShaderCache; }

		// Called by the debug messenger (any thread).
		void OnDebugMessage(
			vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT types,
			const vk::DebugUtilsMessengerCallbackDataEXT& data);

	private:
		struct PhysicalDeviceChoice
		{
			vk::PhysicalDevice Device;
			uint32_t QueueFamily = 0;
		};

		[[nodiscard]] Result<void> Initialize();
		[[nodiscard]] Result<void> LoadVulkan();
		[[nodiscard]] Result<void> CreateInstance();
		[[nodiscard]] Result<PhysicalDeviceChoice> SelectPhysicalDevice();
		// Why `device` cannot be used (empty when it can); `queueFamily` receives the queue family to use.
		[[nodiscard]] std::vector<std::string>
		CheckPhysicalDevice(vk::PhysicalDevice device, uint32_t& queueFamily) const;
		[[nodiscard]] Result<void> CreateLogicalDevice(const PhysicalDeviceChoice& choice);
		[[nodiscard]] Result<void> CreateNvrhiDevice();
		[[nodiscard]] Result<void> CreateFrameResources();
		void FillDeviceInfo();

		// Swap chain (re)creation; the device is idle afterwards. False when nothing can be presented (zero-sized
		// window); after a failure there is no swap chain, so the next BeginFrame tries again.
		[[nodiscard]] Result<bool> BuildSwapchain();
		// Makes NVRHI textures and framebuffers of the swap chain images the back buffers.
		[[nodiscard]] Result<void> WrapSwapchainImages();
		[[nodiscard]] Result<void> CreateHeadlessBackBuffers(uint32_t width, uint32_t height);
		// Makes `textures` the back buffers (with a framebuffer each), replacing the previous ones.
		[[nodiscard]] Result<void>
		SetBackBuffers(std::vector<nvrhi::TextureHandle> textures, nvrhi::Format format, glm::uvec2 size);
		void ReleaseBackBuffers();
		void MarkDeviceLost();

		GraphicsDeviceSpecification m_Specification;
		Window* m_Window;
		GraphicsDeviceInfo m_Info;
		std::atomic<uint64_t> m_ValidationErrorCount{ 0 };
		NvrhiMessageCallback m_MessageCallback{ m_ValidationErrorCount };

		Scope<vk::detail::DynamicLoader> m_Loader;
		vk::detail::DispatchLoaderDynamic m_Dispatch;
		vk::Instance m_Instance;
		vk::DebugUtilsMessengerEXT m_Messenger;
		vk::PhysicalDevice m_PhysicalDevice;
		vk::Device m_Device;
		vk::Queue m_Queue;
		uint32_t m_QueueFamily = 0;
		std::vector<std::string> m_InstanceExtensions;
		std::vector<std::string> m_DeviceExtensions;
		bool m_ValidationLayerEnabled = false;

		nvrhi::vulkan::DeviceHandle m_VulkanNvrhiDevice;
		nvrhi::DeviceHandle m_NvrhiDevice; // the validation layer around m_VulkanNvrhiDevice, or that device itself
		Scope<ShaderCache> m_ShaderCache;
		Scope<VulkanSwapchain> m_Swapchain;

		std::vector<nvrhi::TextureHandle> m_BackBuffers;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;
		glm::uvec2 m_BackBufferSize{ 0 };
		nvrhi::Format m_BackBufferFormat = nvrhi::Format::UNKNOWN;
		uint32_t m_BackBufferIndex = 0;

		std::array<nvrhi::EventQueryHandle, MaxFramesInFlight> m_FrameQueries;
		std::array<bool, MaxFramesInFlight> m_FrameQueryPending{};
		nvrhi::CommandListHandle m_PresentCommandList;
		uint64_t m_FrameCount = 0;
		uint32_t m_FrameSlot = 0;
		bool m_InFrame = false;
		bool m_SwapchainDirty = false; // rebuild before the next acquisition (suboptimal or out of date)
		bool m_DeviceLost = false;
	};
}
