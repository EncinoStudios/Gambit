#pragma once

#include "Gambit/Core/Result.h"

#include <vulkan/vulkan_core.h>

namespace Gambit
{
	// The Vulkan loader library of the process (vulkan-1.dll, libvulkan.so.1, libvulkan.1.dylib or MoltenVK), loaded on
	// first use and kept until the process ends. The graphics devices and GLFW (which creates window surfaces) both
	// take their Vulkan entry points from it, so they always talk to the same loader: on macOS GLFW would otherwise
	// search for a library of its own and could pick another one than Vulkan-Hpp. Thread safe.
	class VulkanLoader
	{
	public:
		// The loader's vkGetInstanceProcAddr. Fails, with the same error on every call, when there is no loader.
		[[nodiscard]] static Result<PFN_vkGetInstanceProcAddr> GetInstanceProcAddr();
	};
}
