#include "gbpch.h"

#include "Gambit/RHI/Vulkan/VulkanSwapchain.h"

#include "Gambit/Platform/Window.h"

// After vulkan.hpp, so that GLFW declares its Vulkan functions (it checks for VK_VERSION_1_0).
#include <GLFW/glfw3.h>

namespace Gambit
{
	namespace
	{
		struct FormatCandidate
		{
			vk::Format Format;
			bool Srgb;
		};

		// The 8-bit formats the engine renders to, in order of preference. The shaders write linear values to sRGB
		// formats and encode themselves for UNORM ones, so only these two kinds are used.
		constexpr std::array FormatCandidates = {
			FormatCandidate{ vk::Format::eB8G8R8A8Srgb, true },
			FormatCandidate{ vk::Format::eR8G8B8A8Srgb, true },
			FormatCandidate{ vk::Format::eB8G8R8A8Unorm, false },
			FormatCandidate{ vk::Format::eR8G8B8A8Unorm, false },
		};

		std::optional<vk::Format> ChooseFormat(const std::vector<vk::SurfaceFormatKHR>& available, bool srgb)
		{
			for (const FormatCandidate& candidate : FormatCandidates)
			{
				if (candidate.Srgb != srgb)
				{
					continue;
				}
				for (const vk::SurfaceFormatKHR& format : available)
				{
					// A single UNDEFINED entry means that the surface accepts any format.
					const bool any = available.size() == 1 && format.format == vk::Format::eUndefined;
					if ((any || format.format == candidate.Format)
					    && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
					{
						return candidate.Format;
					}
				}
			}
			return std::nullopt;
		}

		vk::PresentModeKHR ChoosePresentMode(const std::vector<vk::PresentModeKHR>& available, bool vsync)
		{
			if (!vsync)
			{
				// Mailbox does not tear; immediate is the fallback; FIFO is always supported.
				for (const vk::PresentModeKHR mode : { vk::PresentModeKHR::eMailbox, vk::PresentModeKHR::eImmediate })
				{
					if (std::ranges::find(available, mode) != available.end())
					{
						return mode;
					}
				}
			}
			return vk::PresentModeKHR::eFifo;
		}

		vk::CompositeAlphaFlagBitsKHR ChooseCompositeAlpha(vk::CompositeAlphaFlagsKHR supported)
		{
			for (const vk::CompositeAlphaFlagBitsKHR mode :
			     { vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::CompositeAlphaFlagBitsKHR::eInherit,
			       vk::CompositeAlphaFlagBitsKHR::ePreMultiplied, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied })
			{
				if (supported & mode)
				{
					return mode;
				}
			}
			return vk::CompositeAlphaFlagBitsKHR::eOpaque;
		}
	}

	Result<std::vector<const char*>> VulkanSwapchain::GetRequiredInstanceExtensions()
	{
		if (glfwVulkanSupported() != GLFW_TRUE)
		{
			return Error("the window system has no Vulkan support (GLFW found no Vulkan loader or surface extension)");
		}
		uint32_t count = 0;
		const char** names = glfwGetRequiredInstanceExtensions(&count);
		if (names == nullptr || count == 0)
		{
			return Error("the window system reports no Vulkan surface extensions");
		}
		return std::vector<const char*>(names, names + count);
	}

	Result<Scope<VulkanSwapchain>> VulkanSwapchain::Create(
		vk::Instance instance, const vk::detail::DispatchLoaderDynamic& dispatch, Window& window,
		uint32_t framesInFlight)
	{
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		const VkResult result = glfwCreateWindowSurface(
			static_cast<VkInstance>(instance), static_cast<GLFWwindow*>(window.GetNativeHandle()), nullptr, &surface);
		if (result != VK_SUCCESS)
		{
			return Error(std::format(
				"cannot create a Vulkan surface for the window '{}': {}", window.GetTitle(),
				vk::to_string(static_cast<vk::Result>(result))));
		}
		return CreateScope<VulkanSwapchain>(
			PrivateTag{}, instance, dispatch, window, vk::SurfaceKHR(surface), framesInFlight);
	}

	VulkanSwapchain::VulkanSwapchain(
		PrivateTag, vk::Instance instance, const vk::detail::DispatchLoaderDynamic& dispatch, Window& window,
		vk::SurfaceKHR surface, uint32_t framesInFlight)
		: m_Instance(instance),
		  m_Dispatch(dispatch),
		  m_Window(window),
		  m_Surface(surface),
		  m_FramesInFlight(framesInFlight)
	{}

	VulkanSwapchain::~VulkanSwapchain()
	{
		if (m_Device)
		{
			DestroySwapchain(0);
			// The device is idle and about to be destroyed: nothing can be waited for any longer.
			for (const RetiredSemaphore& retired : m_RetiredSemaphores)
			{
				m_Device.destroySemaphore(retired.Semaphore, nullptr, m_Dispatch);
			}
			for (const vk::Semaphore semaphore : m_AcquireSemaphores)
			{
				m_Device.destroySemaphore(semaphore, nullptr, m_Dispatch);
			}
		}
		m_Instance.destroySurfaceKHR(m_Surface, nullptr, m_Dispatch);
	}

	bool VulkanSwapchain::CanPresent(vk::PhysicalDevice physicalDevice, uint32_t queueFamily) const
	{
		vk::Bool32 supported = VK_FALSE;
		if (physicalDevice.getSurfaceSupportKHR(queueFamily, m_Surface, &supported, m_Dispatch) != vk::Result::eSuccess
		    || supported != VK_TRUE)
		{
			return false;
		}
		uint32_t formats = 0;
		uint32_t modes = 0;
		return physicalDevice.getSurfaceFormatsKHR(m_Surface, &formats, nullptr, m_Dispatch) == vk::Result::eSuccess
			&& physicalDevice.getSurfacePresentModesKHR(m_Surface, &modes, nullptr, m_Dispatch) == vk::Result::eSuccess
			&& formats > 0 && modes > 0;
	}

	void VulkanSwapchain::SetDevice(vk::PhysicalDevice physicalDevice, vk::Device device)
	{
		GB_CORE_ASSERT(!m_Device, "VulkanSwapchain::SetDevice called twice");
		m_PhysicalDevice = physicalDevice;
		m_Device = device;
	}

	Result<bool> VulkanSwapchain::Build(bool srgb, bool vsync, uint64_t frame)
	{
		GB_CORE_ASSERT(m_Device, "VulkanSwapchain::Build before SetDevice");
		if (m_AcquireSemaphores.empty())
		{
			for (uint32_t i = 0; i < m_FramesInFlight; ++i)
			{
				m_AcquireSemaphores.push_back(m_Device.createSemaphore({}, nullptr, m_Dispatch));
			}
		}

		const glm::uvec2 framebufferSize = m_Window.GetFramebufferSize();
		const vk::SurfaceCapabilitiesKHR capabilities =
			m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface, m_Dispatch);
		vk::Extent2D extent = capabilities.currentExtent;
		if (extent.width == std::numeric_limits<uint32_t>::max())
		{
			// The surface takes its size from the swap chain: use the window's framebuffer size.
			extent.width =
				std::clamp(framebufferSize.x, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extent.height =
				std::clamp(framebufferSize.y, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}
		if (framebufferSize.x == 0 || framebufferSize.y == 0 || extent.width == 0 || extent.height == 0)
		{
			DestroySwapchain(frame);
			return false;
		}

		const std::optional<vk::Format> format =
			ChooseFormat(m_PhysicalDevice.getSurfaceFormatsKHR(m_Surface, m_Dispatch), srgb);
		if (!format)
		{
			return Error(std::format(
				"the window surface offers no 8-bit {} format with the sRGB color space", srgb ? "sRGB" : "UNORM"));
		}
		const vk::PresentModeKHR presentMode =
			ChoosePresentMode(m_PhysicalDevice.getSurfacePresentModesKHR(m_Surface, m_Dispatch), vsync);

		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount != 0)
		{
			imageCount = std::min(imageCount, capabilities.maxImageCount);
		}

		// Copies to and from the back buffer (readback, screenshots, clears) need the transfer usages.
		vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eColorAttachment;
		for (const vk::ImageUsageFlagBits optional :
		     { vk::ImageUsageFlagBits::eTransferSrc, vk::ImageUsageFlagBits::eTransferDst })
		{
			if (capabilities.supportedUsageFlags & optional)
			{
				usage |= optional;
			}
		}

		const vk::SwapchainCreateInfoKHR createInfo =
			vk::SwapchainCreateInfoKHR()
				.setSurface(m_Surface)
				.setMinImageCount(imageCount)
				.setImageFormat(*format)
				.setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear)
				.setImageExtent(extent)
				.setImageArrayLayers(1)
				.setImageUsage(usage)
				.setImageSharingMode(vk::SharingMode::eExclusive)
				.setPreTransform(capabilities.currentTransform)
				.setCompositeAlpha(ChooseCompositeAlpha(capabilities.supportedCompositeAlpha))
				.setPresentMode(presentMode)
				.setClipped(VK_TRUE)
				.setOldSwapchain(m_Swapchain);
		vk::SwapchainKHR swapchain;
		const vk::Result created = m_Device.createSwapchainKHR(&createInfo, nullptr, &swapchain, m_Dispatch);
		// The old swap chain is retired by the call even when it fails, so it goes in either case.
		DestroySwapchain(frame);
		if (created != vk::Result::eSuccess)
		{
			return Error(std::format("cannot create the swap chain: {}", vk::to_string(created)));
		}
		m_Swapchain = swapchain;

		m_Images = m_Device.getSwapchainImagesKHR(m_Swapchain, m_Dispatch);
		for (size_t i = 0; i < m_Images.size(); ++i)
		{
			m_PresentSemaphores.push_back(m_Device.createSemaphore({}, nullptr, m_Dispatch));
		}

		m_Extent = extent;
		m_Format = *format;
		m_ImageUsage = usage;
		m_BuiltForSize = framebufferSize;
		m_VSync = vsync;
		GB_CORE_INFO(
			"Swap chain: {}x{} {}, {} images, {}", extent.width, extent.height, vk::to_string(*format), m_Images.size(),
			vk::to_string(presentMode));
		return true;
	}

	void VulkanSwapchain::DestroySwapchain(uint64_t frame)
	{
		if (!m_Device)
		{
			return;
		}
		for (const vk::Semaphore semaphore : m_PresentSemaphores)
		{
			m_RetiredSemaphores.push_back({ semaphore, frame });
		}
		m_PresentSemaphores.clear();
		if (m_Swapchain)
		{
			m_Device.destroySwapchainKHR(m_Swapchain, nullptr, m_Dispatch);
			m_Swapchain = nullptr;
		}
		m_Images.clear();
		m_BuiltForSize = glm::uvec2(0);
	}

	void VulkanSwapchain::DestroyRetiredSemaphores(uint64_t frame)
	{
		std::erase_if(m_RetiredSemaphores, [&](const RetiredSemaphore& retired) {
			if (frame < retired.Frame + RetiredSemaphoreFrames)
			{
				return false;
			}
			m_Device.destroySemaphore(retired.Semaphore, nullptr, m_Dispatch);
			return true;
		});
	}

	VulkanSwapchain::Status VulkanSwapchain::Classify(vk::Result result, const char* operation) const
	{
		switch (result)
		{
			case vk::Result::eSuccess:
			case vk::Result::eSuboptimalKHR:
				return Status::Success;
			case vk::Result::eErrorOutOfDateKHR:
				return Status::OutOfDate;
			case vk::Result::eErrorDeviceLost:
				GB_CORE_ERROR("{}: the Vulkan device was lost", operation);
				return Status::DeviceLost;
			default:
				GB_CORE_ERROR("{} failed: {}", operation, vk::to_string(result));
				return Status::Failed;
		}
	}

	VulkanSwapchain::Status VulkanSwapchain::Acquire(uint32_t frameSlot, uint32_t& imageIndex, bool& suboptimal)
	{
		GB_CORE_ASSERT(m_Swapchain, "VulkanSwapchain::Acquire without a swap chain");
		const vk::Result result = m_Device.acquireNextImageKHR(
			m_Swapchain, std::numeric_limits<uint64_t>::max(), m_AcquireSemaphores.at(frameSlot), nullptr, &imageIndex,
			m_Dispatch);
		suboptimal = result == vk::Result::eSuboptimalKHR;
		return Classify(result, "Acquiring a swap chain image");
	}

	VulkanSwapchain::Status VulkanSwapchain::Present(vk::Queue queue, uint32_t imageIndex, bool& suboptimal)
	{
		GB_CORE_ASSERT(m_Swapchain, "VulkanSwapchain::Present without a swap chain");
		const vk::Semaphore waitSemaphore = m_PresentSemaphores.at(imageIndex);
		const vk::PresentInfoKHR presentInfo = vk::PresentInfoKHR()
												   .setWaitSemaphoreCount(1)
												   .setPWaitSemaphores(&waitSemaphore)
												   .setSwapchainCount(1)
												   .setPSwapchains(&m_Swapchain)
												   .setPImageIndices(&imageIndex);
		const vk::Result result = queue.presentKHR(&presentInfo, m_Dispatch);
		suboptimal = result == vk::Result::eSuboptimalKHR;
		return Classify(result, "Presenting a swap chain image");
	}
}
