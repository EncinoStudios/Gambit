#pragma once

#include "Gambit/Core/Base.h"
#include "Gambit/Core/ErrorBurstLimiter.h"
#include "Gambit/Core/Result.h"
#include "Gambit/Math/Math.h"

#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

namespace Gambit
{
	class Window;

	// A window's Vulkan surface and swap chain, plus the semaphores that order image acquisition, rendering and
	// presentation: one acquire semaphore per frame slot (reused once that slot's previous frame has finished on the
	// GPU) and one present semaphore per swap chain image (reused once the image has been acquired again, which implies
	// that its previous presentation has consumed the semaphore). Internal to VulkanGraphicsDevice.
	class VulkanSwapchain
	{
	public:
		enum class Status : uint8_t
		{
			Success,
			OutOfDate, // the swap chain no longer matches the surface: rebuild it
			DeviceLost,
			Failed
		};

		struct PrivateTag
		{
			explicit PrivateTag() = default;
		};

		// Creates the window's surface. The swap chain itself is made by Build, once the device exists.
		[[nodiscard]] static Result<Scope<VulkanSwapchain>> Create(
			vk::Instance instance, const vk::detail::DispatchLoaderDynamic& dispatch, Window& window,
			uint32_t framesInFlight);

		VulkanSwapchain(
			PrivateTag, vk::Instance instance, const vk::detail::DispatchLoaderDynamic& dispatch, Window& window,
			vk::SurfaceKHR surface, uint32_t framesInFlight);
		~VulkanSwapchain();
		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

		// The Vulkan instance extensions that surfaces of this platform need.
		[[nodiscard]] static Result<std::vector<const char*>> GetRequiredInstanceExtensions();

		[[nodiscard]] vk::SurfaceKHR GetSurface() const noexcept { return m_Surface; }
		// Whether the queue family of `physicalDevice` can present to the surface and the surface has formats and
		// modes.
		[[nodiscard]] bool CanPresent(vk::PhysicalDevice physicalDevice, uint32_t queueFamily) const;

		// Must be called once, before Build: the device the swap chain belongs to.
		void SetDevice(vk::PhysicalDevice physicalDevice, vk::Device device);

		// (Re)creates the swap chain for the window's current framebuffer size, an 8-bit sRGB or UNORM format and the
		// present mode for `vsync`; `frame` is the number of frames presented so far. The device must be idle. Returns
		// false, keeping no swap chain, when the framebuffer has no area (a minimized window). On an error there is no
		// swap chain afterwards either.
		[[nodiscard]] Result<bool> Build(bool srgb, bool vsync, uint64_t frame);
		// Destroys the swap chain (not the surface) and retires its present semaphores. The device must be idle.
		void DestroySwapchain(uint64_t frame);
		// Destroys the retired present semaphores that are old enough (call once per frame, after the frame wait).
		void DestroyRetiredSemaphores(uint64_t frame);

		[[nodiscard]] bool HasSwapchain() const noexcept { return static_cast<bool>(m_Swapchain); }
		// The framebuffer size the swap chain was built for; the window having another size means it must be rebuilt.
		[[nodiscard]] glm::uvec2 GetBuiltForSize() const noexcept { return m_BuiltForSize; }
		[[nodiscard]] vk::Extent2D GetExtent() const noexcept { return m_Extent; }
		[[nodiscard]] vk::Format GetFormat() const noexcept { return m_Format; }
		[[nodiscard]] vk::ImageUsageFlags GetImageUsage() const noexcept { return m_ImageUsage; }
		[[nodiscard]] const std::vector<vk::Image>& GetImages() const noexcept { return m_Images; }
		[[nodiscard]] bool IsVSync() const noexcept { return m_VSync; }

		// Acquires the next image, to be signalled on GetAcquireSemaphore(frameSlot). Success includes a suboptimal
		// swap chain (the image is valid; the caller rebuilds after presenting).
		[[nodiscard]] Status Acquire(uint32_t frameSlot, uint32_t& imageIndex, bool& suboptimal);
		// Presents `imageIndex` once GetPresentSemaphore(imageIndex) is signalled.
		[[nodiscard]] Status Present(vk::Queue queue, uint32_t imageIndex, bool& suboptimal);

		[[nodiscard]] vk::Semaphore GetAcquireSemaphore(uint32_t frameSlot) const
		{
			return m_AcquireSemaphores.at(frameSlot);
		}
		[[nodiscard]] vk::Semaphore GetPresentSemaphore(uint32_t imageIndex) const
		{
			return m_PresentSemaphores.at(imageIndex);
		}

	private:
		// Waiting for the device does not cover the semaphore waits of earlier presentations (without
		// VK_EXT_swapchain_maintenance1 nothing tells when they are done), so the present semaphores of a replaced swap
		// chain are destroyed only once this many more frames have been presented and waited for.
		static constexpr uint64_t RetiredSemaphoreFrames = 8;

		struct RetiredSemaphore
		{
			vk::Semaphore Semaphore;
			uint64_t Frame = 0; // the frame count when it was retired
		};

		// Failures that repeat every frame (a lost surface) are logged once per burst by `errors`.
		[[nodiscard]] static Status Classify(vk::Result result, const char* operation, ErrorBurstLimiter& errors);

		vk::Instance m_Instance;
		const vk::detail::DispatchLoaderDynamic& m_Dispatch;
		Window& m_Window;
		vk::SurfaceKHR m_Surface;
		uint32_t m_FramesInFlight;

		vk::PhysicalDevice m_PhysicalDevice;
		vk::Device m_Device;
		vk::SwapchainKHR m_Swapchain;
		vk::Extent2D m_Extent;
		vk::Format m_Format = vk::Format::eUndefined;
		vk::ImageUsageFlags m_ImageUsage;
		glm::uvec2 m_BuiltForSize{ 0 };
		bool m_VSync = true;
		std::vector<vk::Image> m_Images;
		std::vector<vk::Semaphore> m_AcquireSemaphores;
		std::vector<vk::Semaphore> m_PresentSemaphores;
		std::vector<RetiredSemaphore> m_RetiredSemaphores;
		ErrorBurstLimiter m_AcquireErrors{ "Acquiring a swap chain image" };
		ErrorBurstLimiter m_PresentErrors{ "Presenting a swap chain image" };
	};
}
