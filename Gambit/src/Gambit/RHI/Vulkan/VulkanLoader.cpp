#include "gbpch.h"

#include "Gambit/RHI/Vulkan/VulkanLoader.h"

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
		// Function-local statics are initialized once, thread safely; the library stays loaded until exit, after every
		// device and GLFW are gone.
		static const LoadedLibrary s_Loaded = Load();
		if (s_Loaded.GetInstanceProcAddr == nullptr)
		{
			return Error(s_Loaded.Failure);
		}
		return s_Loaded.GetInstanceProcAddr;
	}
}
