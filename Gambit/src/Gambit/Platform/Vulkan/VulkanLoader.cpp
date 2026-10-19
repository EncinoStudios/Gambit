#include "gbpch.h"

#include "Gambit/Platform/Vulkan/VulkanLoader.h"

#include <vulkan/vulkan.hpp>

namespace Gambit
{
	namespace
	{
		struct LoadedLibrary
		{
			Scope<vk::detail::DynamicLoader> Library;
			PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
			std::string Failure;
		};

		LoadedLibrary Load()
		{
			LoadedLibrary loaded;
			try
			{
				loaded.Library = CreateScope<vk::detail::DynamicLoader>();
			}
			catch (const std::exception& error)
			{
				loaded.Failure = std::format("cannot load the Vulkan loader library ({})", error.what());
				return loaded;
			}
			loaded.GetInstanceProcAddr =
				loaded.Library->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
			if (loaded.GetInstanceProcAddr == nullptr)
			{
				loaded.Failure = "the Vulkan loader library has no vkGetInstanceProcAddr";
				loaded.Library.reset();
			}
			return loaded;
		}
	}

	Result<PFN_vkGetInstanceProcAddr> VulkanLoader::GetInstanceProcAddr()
	{
		// Function-local statics are initialized once, thread safely. The library is released on purpose: a static
		// object would unload it during static destruction, possibly before a device or a GLFW window that another
		// static owns calls vkDestroy* through it. The process's exit frees it.
		static const LoadedLibrary* const s_Loaded = CreateScope<LoadedLibrary>(Load()).release();
		if (s_Loaded->GetInstanceProcAddr == nullptr)
		{
			return Error(s_Loaded->Failure);
		}
		return s_Loaded->GetInstanceProcAddr;
	}
}
