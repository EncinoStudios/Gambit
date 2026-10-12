#include "gbpch.h"

#include "Gambit/RHI/Vulkan/VulkanGraphicsDevice.h"

#include "Gambit/Core/Version.h"
#include "Gambit/Platform/Window.h"

#include <nvrhi/utils.h>
#include <nvrhi/validation.h>

#include <cctype>
#include <exception>

// The storage of the process-wide Vulkan-Hpp dispatcher that NVRHI calls Vulkan through; exactly one translation unit
// of the program defines it.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Gambit
{
	namespace
	{
		constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";
		// NVRHI renders with dynamic rendering and synchronization2 through their core Vulkan 1.3 entry points.
		constexpr uint32_t RequiredApiVersion = VK_API_VERSION_1_3;

		Error DeviceError(GraphicsDeviceError code, std::string message)
		{
			return Error(std::move(message), static_cast<int>(code));
		}

		// NVRHI calls Vulkan through the process-wide default dispatcher, whose function pointers must come from a live
		// instance. It is initialized from the newest instance, and from the newest remaining one when that instance
		// goes away, so that any number of devices can come and go. Only instance-level loading is used: device
		// functions are then the loader's dispatching entry points, which are valid for every device.
		struct DispatcherSource
		{
			VkInstance Instance;
			PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
		};

		std::mutex s_DispatcherMutex;
		std::vector<DispatcherSource> s_DispatcherSources;

		void AddDispatcherSource(VkInstance instance, PFN_vkGetInstanceProcAddr getInstanceProcAddr)
		{
			const std::scoped_lock lock(s_DispatcherMutex);
			s_DispatcherSources.push_back({ instance, getInstanceProcAddr });
			VULKAN_HPP_DEFAULT_DISPATCHER.init(instance, getInstanceProcAddr);
		}

		void RemoveDispatcherSource(VkInstance instance)
		{
			const std::scoped_lock lock(s_DispatcherMutex);
			std::erase_if(
				s_DispatcherSources, [&](const DispatcherSource& source) { return source.Instance == instance; });
			if (!s_DispatcherSources.empty())
			{
				const DispatcherSource& newest = s_DispatcherSources.back();
				VULKAN_HPP_DEFAULT_DISPATCHER.init(newest.Instance, newest.GetInstanceProcAddr);
			}
		}

		std::string FormatVersion(uint32_t version)
		{
			return std::format(
				"{}.{}.{}", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
				VK_API_VERSION_PATCH(version));
		}

		bool HasExtension(const std::vector<vk::ExtensionProperties>& extensions, std::string_view name)
		{
			return std::ranges::any_of(extensions, [&](const vk::ExtensionProperties& extension) {
				return name == extension.extensionName.data();
			});
		}

		GpuType ToGpuType(vk::PhysicalDeviceType type)
		{
			switch (type)
			{
				case vk::PhysicalDeviceType::eDiscreteGpu:
					return GpuType::Discrete;
				case vk::PhysicalDeviceType::eIntegratedGpu:
					return GpuType::Integrated;
				case vk::PhysicalDeviceType::eVirtualGpu:
					return GpuType::Virtual;
				case vk::PhysicalDeviceType::eCpu:
					return GpuType::Cpu;
				default:
					return GpuType::Other;
			}
		}

		// Higher is preferred: discrete > integrated > virtual > CPU > other.
		int TypeRank(GpuType type)
		{
			switch (type)
			{
				case GpuType::Discrete:
					return 4;
				case GpuType::Integrated:
					return 3;
				case GpuType::Virtual:
					return 2;
				case GpuType::Cpu:
					return 1;
				case GpuType::Other:
					return 0;
			}
			return 0;
		}

		bool ContainsIgnoringCase(std::string_view text, std::string_view part)
		{
			const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
			return !std::ranges::search(text, part, {}, lower, lower).empty();
		}

		// The optional core features (see GraphicsDeviceFeatures): enabled when supported.
		struct OptionalFeature
		{
			vk::Bool32 vk::PhysicalDeviceFeatures::*Vulkan;
			bool GraphicsDeviceFeatures::*Gambit;
		};

		constexpr std::array OptionalFeatures = {
			OptionalFeature{ &vk::PhysicalDeviceFeatures::geometryShader, &GraphicsDeviceFeatures::GeometryShader },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::samplerAnisotropy,
			                 &GraphicsDeviceFeatures::SamplerAnisotropy },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::imageCubeArray, &GraphicsDeviceFeatures::ImageCubeArray },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::fillModeNonSolid, &GraphicsDeviceFeatures::FillModeNonSolid },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::depthClamp, &GraphicsDeviceFeatures::DepthClamp },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::independentBlend, &GraphicsDeviceFeatures::IndependentBlend },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::textureCompressionBC,
			                 &GraphicsDeviceFeatures::TextureCompressionBC },
			OptionalFeature{ &vk::PhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat,
			                 &GraphicsDeviceFeatures::ShaderStorageImageWriteWithoutFormat },
		};

		VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugMessengerCallback(
			vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT types,
			const vk::DebugUtilsMessengerCallbackDataEXT* data, void* userData)
		{
			if (data != nullptr && userData != nullptr)
			{
				static_cast<VulkanGraphicsDevice*>(userData)->OnDebugMessage(severity, types, *data);
			}
			return VK_FALSE; // never abort the call that triggered the message
		}

		vk::DebugUtilsMessengerCreateInfoEXT MakeMessengerCreateInfo(void* userData)
		{
			return vk::DebugUtilsMessengerCreateInfoEXT()
				.setMessageSeverity(
					vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning
					| vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
				.setMessageType(
					vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
					| vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance)
				.setPfnUserCallback(DebugMessengerCallback)
				.setPUserData(userData);
		}
	}

	void NvrhiMessageCallback::message(nvrhi::MessageSeverity severity, const char* messageText)
	{
		const char* text = messageText != nullptr ? messageText : "(no message)";
		switch (severity)
		{
			case nvrhi::MessageSeverity::Info:
				GB_CORE_TRACE("NVRHI: {}", text);
				break;
			case nvrhi::MessageSeverity::Warning:
				GB_CORE_WARN("NVRHI: {}", text);
				break;
			case nvrhi::MessageSeverity::Error:
				++m_ErrorCount;
				GB_CORE_ERROR("NVRHI: {}", text);
				break;
			case nvrhi::MessageSeverity::Fatal:
				++m_ErrorCount;
				GB_CORE_CRITICAL("NVRHI: {}", text);
				break;
		}
	}

	Result<Scope<GraphicsDevice>>
	GraphicsDevice::Create(const GraphicsDeviceSpecification& specification, Window* window)
	{
		return VulkanGraphicsDevice::Create(specification, window);
	}

	Result<Scope<GraphicsDevice>>
	VulkanGraphicsDevice::Create(const GraphicsDeviceSpecification& specification, Window* window)
	{
		if (specification.Headless != (window == nullptr))
		{
			return DeviceError(
				GraphicsDeviceError::InvalidSpecification,
				specification.Headless ? "a headless graphics device takes no window"
									   : "a windowed graphics device needs a window");
		}
		if (specification.FramesInFlight < 1 || specification.FramesInFlight > MaxFramesInFlight)
		{
			return DeviceError(
				GraphicsDeviceError::InvalidSpecification,
				std::format("FramesInFlight must be 1 to {}, not {}", MaxFramesInFlight, specification.FramesInFlight));
		}
		if (specification.Headless
		    && (specification.BackBufferWidth == 0 || specification.BackBufferHeight == 0
		        || specification.BackBufferWidth > Image::MaxDimension
		        || specification.BackBufferHeight > Image::MaxDimension))
		{
			return DeviceError(
				GraphicsDeviceError::InvalidSpecification,
				std::format(
					"invalid back-buffer size {}x{} (1 to {} in each direction)", specification.BackBufferWidth,
					specification.BackBufferHeight, Image::MaxDimension));
		}

		Scope<VulkanGraphicsDevice> device = CreateScope<VulkanGraphicsDevice>(PrivateTag{}, specification, window);
		Result<void> initialized;
		try
		{
			initialized = device->Initialize();
		}
		catch (const vk::SystemError& error)
		{
			initialized = DeviceError(
				GraphicsDeviceError::InitializationFailed, std::format("Vulkan call failed: {}", error.what()));
		}
		catch (const std::exception& error)
		{
			initialized = DeviceError(
				GraphicsDeviceError::InitializationFailed,
				std::format("graphics device initialization failed: {}", error.what()));
		}
		if (!initialized)
		{
			return initialized.GetError(); // the destructor releases what was created
		}
		return Scope<GraphicsDevice>(std::move(device));
	}

	VulkanGraphicsDevice::VulkanGraphicsDevice(
		PrivateTag, const GraphicsDeviceSpecification& specification, Window* window)
		: m_Specification(specification), m_Window(window)
	{}

	VulkanGraphicsDevice::~VulkanGraphicsDevice()
	{
		if (m_NvrhiDevice)
		{
			WaitIdle();
		}
		m_PresentCommandList = nullptr;
		for (nvrhi::EventQueryHandle& query : m_FrameQueries)
		{
			query = nullptr;
		}
		ReleaseBackBuffers();
		m_ShaderCache.reset();
		m_NvrhiDevice = nullptr;
		m_VulkanNvrhiDevice = nullptr;
		m_Swapchain.reset();
		if (m_Device)
		{
			m_Device.destroy(nullptr, m_Dispatch);
		}
		if (m_Messenger)
		{
			m_Instance.destroyDebugUtilsMessengerEXT(m_Messenger, nullptr, m_Dispatch);
		}
		if (m_Instance)
		{
			m_Instance.destroy(nullptr, m_Dispatch);
			RemoveDispatcherSource(static_cast<VkInstance>(m_Instance));
		}
	}

	Result<void> VulkanGraphicsDevice::Initialize()
	{
		if (Result<void> result = LoadVulkan(); !result)
		{
			return result;
		}
		if (Result<void> result = CreateInstance(); !result)
		{
			return result;
		}
		if (!m_Specification.Headless)
		{
			Result<Scope<VulkanSwapchain>> swapchain =
				VulkanSwapchain::Create(m_Instance, m_Dispatch, *m_Window, m_Specification.FramesInFlight);
			if (!swapchain)
			{
				return DeviceError(GraphicsDeviceError::InitializationFailed, swapchain.GetError().Message);
			}
			m_Swapchain = std::move(*swapchain);
		}
		Result<PhysicalDeviceChoice> choice = SelectPhysicalDevice();
		if (!choice)
		{
			return choice.GetError();
		}
		if (Result<void> result = CreateLogicalDevice(*choice); !result)
		{
			return result;
		}
		if (Result<void> result = CreateNvrhiDevice(); !result)
		{
			return result;
		}
		if (Result<void> result = CreateFrameResources(); !result)
		{
			return result;
		}
		FillDeviceInfo();

		if (m_Specification.Headless)
		{
			if (Result<void> result =
			        CreateHeadlessBackBuffers(m_Specification.BackBufferWidth, m_Specification.BackBufferHeight);
			    !result)
			{
				return result;
			}
		}
		else
		{
			m_Swapchain->SetDevice(m_PhysicalDevice, m_Device);
			// A window that starts minimized gets its swap chain at the first BeginFrame with a visible framebuffer.
			if (Result<bool> built = BuildSwapchain(); !built)
			{
				return DeviceError(GraphicsDeviceError::InitializationFailed, built.GetError().Message);
			}
		}

		GB_CORE_INFO(
			"Graphics device: {} ({}, Vulkan {}, {} {}), {}, validation {}", m_Info.Name, ToString(m_Info.Type),
			m_Info.ApiVersion, m_Info.DriverName, m_Info.DriverInfo,
			m_Specification.Headless ? std::format("headless {}x{}", m_BackBufferSize.x, m_BackBufferSize.y)
									 : std::string("windowed"),
			m_Info.ValidationEnabled ? "on" : "off");
		return {};
	}

	Result<void> VulkanGraphicsDevice::LoadVulkan()
	{
		try
		{
			m_Loader = CreateScope<vk::detail::DynamicLoader>();
		}
		catch (const std::exception& error)
		{
			return DeviceError(
				GraphicsDeviceError::VulkanUnavailable,
				std::format("Vulkan is not available: cannot load the Vulkan loader library ({})", error.what()));
		}
		const auto getInstanceProcAddr = m_Loader->getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
		if (getInstanceProcAddr == nullptr)
		{
			return DeviceError(
				GraphicsDeviceError::VulkanUnavailable,
				"Vulkan is not available: the Vulkan loader library has no vkGetInstanceProcAddr");
		}
		m_Dispatch.init(getInstanceProcAddr);

		const uint32_t loaderVersion = m_Dispatch.vkEnumerateInstanceVersion != nullptr
			? vk::enumerateInstanceVersion(m_Dispatch)
			: VK_API_VERSION_1_0;
		if (loaderVersion < RequiredApiVersion)
		{
			return DeviceError(
				GraphicsDeviceError::VulkanUnavailable,
				std::format(
					"Vulkan is not available: the Vulkan loader supports version {}, {} is required",
					FormatVersion(loaderVersion), FormatVersion(RequiredApiVersion)));
		}
		return {};
	}

	Result<void> VulkanGraphicsDevice::CreateInstance()
	{
		const std::vector<vk::ExtensionProperties> available =
			vk::enumerateInstanceExtensionProperties(nullptr, m_Dispatch);
		const std::vector<vk::LayerProperties> layers = vk::enumerateInstanceLayerProperties(m_Dispatch);

		std::vector<const char*> extensions;
		if (!m_Specification.Headless)
		{
			Result<std::vector<const char*>> surfaceExtensions = VulkanSwapchain::GetRequiredInstanceExtensions();
			if (!surfaceExtensions)
			{
				return DeviceError(
					GraphicsDeviceError::VulkanUnavailable,
					std::format("Vulkan is not available: {}", surfaceExtensions.GetError().Message));
			}
			for (const char* name : *surfaceExtensions)
			{
				if (!HasExtension(available, name))
				{
					return DeviceError(
						GraphicsDeviceError::VulkanUnavailable,
						std::format("Vulkan is not available: the instance extension {} is missing", name));
				}
				extensions.push_back(name);
			}
		}
		const bool debugUtils = HasExtension(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (debugUtils)
		{
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}
		// Lists portability implementations such as MoltenVK.
		const bool portability = HasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		if (portability)
		{
			extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
		}

		std::vector<const char*> enabledLayers;
		if (m_Specification.EnableValidation)
		{
			const bool layerAvailable = std::ranges::any_of(layers, [](const vk::LayerProperties& layer) {
				return std::string_view(layer.layerName) == ValidationLayerName;
			});
			if (layerAvailable)
			{
				enabledLayers.push_back(ValidationLayerName);
				m_ValidationLayerEnabled = true;
			}
			else
			{
				GB_CORE_WARN(
					"Vulkan validation was requested, but {} is not installed; running without it",
					ValidationLayerName);
			}
		}

		const vk::ApplicationInfo applicationInfo =
			vk::ApplicationInfo()
				.setPApplicationName(m_Specification.ApplicationName.c_str())
				.setApplicationVersion(1)
				.setPEngineName("Gambit")
				.setEngineVersion(VK_MAKE_API_VERSION(0, GB_VERSION_MAJOR, GB_VERSION_MINOR, GB_VERSION_PATCH))
				.setApiVersion(RequiredApiVersion);
		vk::InstanceCreateInfo createInfo = vk::InstanceCreateInfo()
												.setPApplicationInfo(&applicationInfo)
												.setPEnabledExtensionNames(extensions)
												.setPEnabledLayerNames(enabledLayers);
		if (portability)
		{
			createInfo.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
		}
		// Chained so that messages about instance creation and destruction are reported too.
		const vk::DebugUtilsMessengerCreateInfoEXT messengerInfo = MakeMessengerCreateInfo(this);
		if (debugUtils)
		{
			createInfo.setPNext(&messengerInfo);
		}

		const vk::Result result = vk::createInstance(&createInfo, nullptr, &m_Instance, m_Dispatch);
		if (result != vk::Result::eSuccess)
		{
			m_Instance = nullptr;
			return DeviceError(
				GraphicsDeviceError::VulkanUnavailable,
				std::format("Vulkan is not available: cannot create a Vulkan instance ({})", vk::to_string(result)));
		}
		m_Dispatch.init(m_Instance);
		AddDispatcherSource(static_cast<VkInstance>(m_Instance), m_Dispatch.vkGetInstanceProcAddr);

		if (debugUtils)
		{
			m_Messenger = m_Instance.createDebugUtilsMessengerEXT(messengerInfo, nullptr, m_Dispatch);
		}
		for (const char* name : extensions)
		{
			m_InstanceExtensions.emplace_back(name);
		}
		return {};
	}

	std::vector<std::string>
	VulkanGraphicsDevice::CheckPhysicalDevice(vk::PhysicalDevice device, uint32_t& queueFamily) const
	{
		std::vector<std::string> problems;
		const vk::PhysicalDeviceProperties properties = device.getProperties(m_Dispatch);
		if (properties.apiVersion < RequiredApiVersion)
		{
			problems.push_back(std::format(
				"supports Vulkan {}, {} is required", FormatVersion(properties.apiVersion),
				FormatVersion(RequiredApiVersion)));
			return problems; // the 1.2 and 1.3 feature queries below are not valid for older devices
		}

		vk::PhysicalDeviceVulkan13Features features13;
		vk::PhysicalDeviceVulkan12Features features12;
		features12.setPNext(&features13);
		vk::PhysicalDeviceFeatures2 features;
		features.setPNext(&features12);
		device.getFeatures2(&features, m_Dispatch);
		if (!features12.timelineSemaphore)
		{
			problems.emplace_back("no timeline semaphores");
		}
		if (!features13.synchronization2)
		{
			problems.emplace_back("no synchronization2");
		}
		if (!features13.dynamicRendering)
		{
			problems.emplace_back("no dynamic rendering");
		}

		const std::vector<vk::ExtensionProperties> extensions =
			device.enumerateDeviceExtensionProperties(nullptr, m_Dispatch);
		if (m_Swapchain && !HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
		{
			problems.emplace_back("no " VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}

		const std::vector<vk::QueueFamilyProperties> families = device.getQueueFamilyProperties(m_Dispatch);
		std::optional<uint32_t> family;
		for (uint32_t i = 0; i < families.size() && !family; ++i)
		{
			const vk::QueueFlags needed = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute;
			if ((families[i].queueFlags & needed) == needed && families[i].queueCount > 0
			    && (!m_Swapchain || m_Swapchain->CanPresent(device, i)))
			{
				family = i;
			}
		}
		if (!family)
		{
			problems.emplace_back(
				m_Swapchain ? "no queue family for graphics, compute and presentation to the window"
							: "no queue family for graphics and compute");
		}
		else
		{
			queueFamily = *family;
		}

		if (m_Specification.Headless)
		{
			const vk::Format format =
				static_cast<vk::Format>(nvrhi::vulkan::convertFormat(m_Specification.BackBufferFormat));
			const vk::FormatFeatureFlags needed =
				vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
			if (format == vk::Format::eUndefined
			    || (device.getFormatProperties(format, m_Dispatch).optimalTilingFeatures & needed) != needed)
			{
				problems.push_back(std::format(
					"cannot render to {} back buffers",
					nvrhi::utils::FormatToString(m_Specification.BackBufferFormat)));
			}
		}
		return problems;
	}

	Result<VulkanGraphicsDevice::PhysicalDeviceChoice> VulkanGraphicsDevice::SelectPhysicalDevice()
	{
		const std::vector<vk::PhysicalDevice> devices = m_Instance.enumeratePhysicalDevices(m_Dispatch);
		if (devices.empty())
		{
			return DeviceError(
				GraphicsDeviceError::VulkanUnavailable, "Vulkan is not available: no Vulkan device found");
		}

		struct Candidate
		{
			uint32_t Index = 0;
			PhysicalDeviceChoice Choice;
			std::string Name;
			GpuType Type = GpuType::Other;
		};
		std::vector<Candidate> candidates;
		std::string rejections;
		for (uint32_t index = 0; index < devices.size(); ++index)
		{
			const vk::PhysicalDeviceProperties properties = devices[index].getProperties(m_Dispatch);
			const std::string name = properties.deviceName.data();
			const GpuType type = ToGpuType(properties.deviceType);
			uint32_t queueFamily = 0;
			const std::vector<std::string> problems = CheckPhysicalDevice(devices[index], queueFamily);
			std::string verdict = "usable";
			if (!problems.empty())
			{
				verdict.clear();
				for (const std::string& problem : problems)
				{
					verdict += verdict.empty() ? problem : ", " + problem;
				}
				rejections += std::format("\n  [{}] {}: {}", index, name, verdict);
			}
			else
			{
				candidates.push_back({ index, { devices[index], queueFamily }, name, type });
			}
			GB_CORE_INFO(
				"Vulkan device [{}] {} ({}, Vulkan {}): {}", index, name, ToString(type),
				FormatVersion(properties.apiVersion), verdict);
		}
		if (candidates.empty())
		{
			return DeviceError(
				GraphicsDeviceError::NoSuitableDevice, std::format("no suitable Vulkan device:{}", rejections));
		}

		const Candidate* chosen = nullptr;
		if (m_Specification.PreferredGpuIndex)
		{
			const auto it = std::ranges::find(candidates, *m_Specification.PreferredGpuIndex, &Candidate::Index);
			if (it != candidates.end())
			{
				chosen = &*it;
			}
			else
			{
				GB_CORE_WARN(
					"The preferred GPU index {} is not a usable Vulkan device; choosing automatically",
					*m_Specification.PreferredGpuIndex);
			}
		}
		if (chosen == nullptr && !m_Specification.PreferredGpuName.empty())
		{
			const auto it = std::ranges::find_if(candidates, [&](const Candidate& candidate) {
				return ContainsIgnoringCase(candidate.Name, m_Specification.PreferredGpuName);
			});
			if (it != candidates.end())
			{
				chosen = &*it;
			}
			else
			{
				GB_CORE_WARN(
					"No usable Vulkan device is called '{}'; choosing automatically", m_Specification.PreferredGpuName);
			}
		}
		if (chosen == nullptr)
		{
			// The best type; the enumeration order breaks ties.
			chosen = &*std::ranges::max_element(
				candidates, [](const Candidate& a, const Candidate& b) { return TypeRank(a.Type) < TypeRank(b.Type); });
		}
		return chosen->Choice;
	}

	Result<void> VulkanGraphicsDevice::CreateLogicalDevice(const PhysicalDeviceChoice& choice)
	{
		m_PhysicalDevice = choice.Device;
		m_QueueFamily = choice.QueueFamily;

		const std::vector<vk::ExtensionProperties> available =
			m_PhysicalDevice.enumerateDeviceExtensionProperties(nullptr, m_Dispatch);
		std::vector<const char*> extensions;
		if (m_Swapchain)
		{
			extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}
		// Portability implementations (MoltenVK) require their subset extension to be enabled.
		if (HasExtension(available, "VK_KHR_portability_subset"))
		{
			extensions.push_back("VK_KHR_portability_subset");
		}

		vk::PhysicalDeviceVulkan11Features supported11;
		vk::PhysicalDeviceFeatures2 supported;
		supported.setPNext(&supported11);
		m_PhysicalDevice.getFeatures2(&supported, m_Dispatch);

		vk::PhysicalDeviceFeatures enabledCore;
		for (const OptionalFeature& feature : OptionalFeatures)
		{
			if (supported.features.*feature.Vulkan)
			{
				enabledCore.*feature.Vulkan = VK_TRUE;
				m_Info.Features.*feature.Gambit = true;
			}
		}
		vk::PhysicalDeviceVulkan13Features enabled13;
		enabled13.setSynchronization2(VK_TRUE).setDynamicRendering(VK_TRUE);
		vk::PhysicalDeviceVulkan12Features enabled12;
		enabled12.setTimelineSemaphore(VK_TRUE).setPNext(&enabled13);
		vk::PhysicalDeviceVulkan11Features enabled11;
		enabled11.setShaderDrawParameters(supported11.shaderDrawParameters).setPNext(&enabled12);
		m_Info.Features.ShaderDrawParameters = supported11.shaderDrawParameters == VK_TRUE;
		vk::PhysicalDeviceFeatures2 enabled;
		enabled.setFeatures(enabledCore).setPNext(&enabled11);

		const float priority = 1.0f;
		const vk::DeviceQueueCreateInfo queueInfo = vk::DeviceQueueCreateInfo()
														.setQueueFamilyIndex(m_QueueFamily)
														.setQueueCount(1)
														.setPQueuePriorities(&priority);
		const vk::DeviceCreateInfo createInfo =
			vk::DeviceCreateInfo().setPNext(&enabled).setQueueCreateInfos(queueInfo).setPEnabledExtensionNames(
				extensions);
		const vk::Result result = m_PhysicalDevice.createDevice(&createInfo, nullptr, &m_Device, m_Dispatch);
		if (result != vk::Result::eSuccess)
		{
			m_Device = nullptr;
			return DeviceError(
				GraphicsDeviceError::InitializationFailed,
				std::format("cannot create the Vulkan device: {}", vk::to_string(result)));
		}
		m_Dispatch.init(m_Device);
		m_Queue = m_Device.getQueue(m_QueueFamily, 0, m_Dispatch);
		for (const char* name : extensions)
		{
			m_DeviceExtensions.emplace_back(name);
		}
		return {};
	}

	Result<void> VulkanGraphicsDevice::CreateNvrhiDevice()
	{
		std::vector<const char*> instanceExtensions;
		for (const std::string& name : m_InstanceExtensions)
		{
			instanceExtensions.push_back(name.c_str());
		}
		std::vector<const char*> deviceExtensions;
		for (const std::string& name : m_DeviceExtensions)
		{
			deviceExtensions.push_back(name.c_str());
		}

		nvrhi::vulkan::DeviceDesc desc;
		desc.errorCB = &m_MessageCallback;
		desc.instance = static_cast<VkInstance>(m_Instance);
		desc.physicalDevice = static_cast<VkPhysicalDevice>(m_PhysicalDevice);
		desc.device = static_cast<VkDevice>(m_Device);
		desc.graphicsQueue = static_cast<VkQueue>(m_Queue);
		desc.graphicsQueueIndex = static_cast<int>(m_QueueFamily);
		desc.instanceExtensions = instanceExtensions.data();
		desc.numInstanceExtensions = instanceExtensions.size();
		desc.deviceExtensions = deviceExtensions.data();
		desc.numDeviceExtensions = deviceExtensions.size();
		m_VulkanNvrhiDevice = nvrhi::vulkan::createDevice(desc);
		if (!m_VulkanNvrhiDevice)
		{
			return DeviceError(GraphicsDeviceError::InitializationFailed, "NVRHI could not create its Vulkan device");
		}
		m_NvrhiDevice = m_Specification.EnableValidation ? nvrhi::validation::createValidationLayer(m_VulkanNvrhiDevice)
														 : nvrhi::DeviceHandle(m_VulkanNvrhiDevice);
		m_ShaderCache = CreateScope<ShaderCache>(*m_NvrhiDevice);
		return {};
	}

	Result<void> VulkanGraphicsDevice::CreateFrameResources()
	{
		for (uint32_t i = 0; i < m_Specification.FramesInFlight; ++i)
		{
			m_FrameQueries[i] = m_NvrhiDevice->createEventQuery();
		}
		m_PresentCommandList = m_NvrhiDevice->createCommandList();
		if (!m_PresentCommandList || !m_FrameQueries[0])
		{
			return DeviceError(GraphicsDeviceError::InitializationFailed, "NVRHI could not create the frame resources");
		}
		return {};
	}

	void VulkanGraphicsDevice::FillDeviceInfo()
	{
		vk::PhysicalDeviceVulkan12Properties properties12;
		vk::PhysicalDeviceProperties2 properties2;
		properties2.pNext = &properties12;
		m_PhysicalDevice.getProperties2(&properties2, m_Dispatch);
		const vk::PhysicalDeviceProperties& properties = properties2.properties;
		const vk::PhysicalDeviceLimits& limits = properties.limits;

		m_Info.Name = properties.deviceName.data();
		m_Info.Type = ToGpuType(properties.deviceType);
		m_Info.VendorId = properties.vendorID;
		m_Info.DeviceId = properties.deviceID;
		m_Info.ApiVersion = FormatVersion(properties.apiVersion);
		m_Info.DriverName = properties12.driverName.data();
		m_Info.DriverInfo = properties12.driverInfo.data();
		m_Info.ValidationEnabled = m_ValidationLayerEnabled;

		GraphicsDeviceLimits& out = m_Info.Limits;
		out.MaxTextureDimension2D = limits.maxImageDimension2D;
		out.MaxTextureDimensionCube = limits.maxImageDimensionCube;
		out.MaxTextureArrayLayers = limits.maxImageArrayLayers;
		out.MaxColorAttachments = limits.maxColorAttachments;
		out.MaxPushConstantsSize = limits.maxPushConstantsSize;
		out.MaxBoundDescriptorSets = limits.maxBoundDescriptorSets;
		out.MaxComputeWorkGroupInvocations = limits.maxComputeWorkGroupInvocations;
		out.MaxComputeWorkGroupSize = glm::uvec3(
			limits.maxComputeWorkGroupSize[0], limits.maxComputeWorkGroupSize[1], limits.maxComputeWorkGroupSize[2]);
		out.MaxComputeWorkGroupCount = glm::uvec3(
			limits.maxComputeWorkGroupCount[0], limits.maxComputeWorkGroupCount[1], limits.maxComputeWorkGroupCount[2]);
		out.MaxSamplerAnisotropy = limits.maxSamplerAnisotropy;
		out.MinUniformBufferOffsetAlignment = limits.minUniformBufferOffsetAlignment;
		out.MinStorageBufferOffsetAlignment = limits.minStorageBufferOffsetAlignment;
		out.TimestampPeriodNs = limits.timestampPeriod;
	}

	void VulkanGraphicsDevice::OnDebugMessage(
		vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT types,
		const vk::DebugUtilsMessengerCallbackDataEXT& data)
	{
		const char* message = data.pMessage != nullptr ? data.pMessage : "(no message)";
		const bool validation = static_cast<bool>(types & vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
		const char* source = validation ? "Vulkan validation" : "Vulkan";
		if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
		{
			if (validation)
			{
				++m_ValidationErrorCount;
			}
			GB_CORE_ERROR("{}: {}", source, message);
		}
		else
		{
			GB_CORE_WARN("{}: {}", source, message);
		}
	}

	Result<void> VulkanGraphicsDevice::CreateHeadlessBackBuffers(uint32_t width, uint32_t height)
	{
		const nvrhi::FormatSupport support = m_NvrhiDevice->queryFormatSupport(m_Specification.BackBufferFormat);
		const nvrhi::FormatSupport needed = nvrhi::FormatSupport::RenderTarget | nvrhi::FormatSupport::ShaderLoad;
		if ((support & needed) != needed)
		{
			return DeviceError(
				GraphicsDeviceError::InvalidSpecification,
				std::format(
					"the GPU cannot render to {} back buffers",
					nvrhi::utils::FormatToString(m_Specification.BackBufferFormat)));
		}
		std::vector<nvrhi::TextureHandle> textures;
		for (uint32_t i = 0; i < m_Specification.FramesInFlight; ++i)
		{
			// Between frames the back buffers rest in the shader-resource state, ready to be read or copied.
			const nvrhi::TextureDesc desc = nvrhi::TextureDesc()
												.setWidth(width)
												.setHeight(height)
												.setFormat(m_Specification.BackBufferFormat)
												.setIsRenderTarget(true)
												.setInitialState(nvrhi::ResourceStates::ShaderResource)
												.setKeepInitialState(true)
												.setDebugName(std::format("Back buffer {}", i));
			nvrhi::TextureHandle texture = m_NvrhiDevice->createTexture(desc);
			if (!texture)
			{
				return DeviceError(
					GraphicsDeviceError::InitializationFailed, "NVRHI could not create the back buffers");
			}
			textures.push_back(texture);
		}
		return SetBackBuffers(std::move(textures), m_Specification.BackBufferFormat, glm::uvec2(width, height));
	}

	Result<void> VulkanGraphicsDevice::SetBackBuffers(
		std::vector<nvrhi::TextureHandle> textures, nvrhi::Format format, glm::uvec2 size)
	{
		std::vector<nvrhi::FramebufferHandle> framebuffers;
		for (const nvrhi::TextureHandle& texture : textures)
		{
			nvrhi::FramebufferHandle framebuffer =
				m_NvrhiDevice->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture));
			if (!framebuffer)
			{
				return DeviceError(
					GraphicsDeviceError::InitializationFailed, "NVRHI could not create the back-buffer framebuffers");
			}
			framebuffers.push_back(framebuffer);
		}
		m_BackBuffers = std::move(textures);
		m_Framebuffers = std::move(framebuffers);
		m_BackBufferFormat = format;
		m_BackBufferSize = size;
		m_BackBufferIndex = 0;
		return {};
	}

	void VulkanGraphicsDevice::ReleaseBackBuffers()
	{
		m_Framebuffers.clear();
		m_BackBuffers.clear();
		m_BackBufferIndex = 0;
	}

	Result<bool> VulkanGraphicsDevice::BuildSwapchain()
	{
		WaitIdle();
		ReleaseBackBuffers();
		m_SwapchainDirty = false;
		m_BackBufferSize = glm::uvec2(0);

		const bool srgb = nvrhi::getFormatInfo(m_Specification.BackBufferFormat).isSRGB;
		Result<bool> built = false;
		try
		{
			built = m_Swapchain->Build(srgb, m_Specification.VSync, m_FrameCount);
			if (built && *built)
			{
				if (Result<void> wrapped = WrapSwapchainImages(); !wrapped)
				{
					built = wrapped.GetError();
				}
			}
		}
		catch (const vk::SystemError& error)
		{
			built = Error(std::format("cannot create the swap chain: {}", error.what()));
		}
		if (!built)
		{
			// A swap chain without back buffers is of no use: the next BeginFrame starts again from scratch.
			m_Swapchain->DestroySwapchain(m_FrameCount);
			ReleaseBackBuffers();
		}
		return built;
	}

	Result<void> VulkanGraphicsDevice::WrapSwapchainImages()
	{
		const vk::Extent2D extent = m_Swapchain->GetExtent();
		const nvrhi::Format format = [&] {
			switch (m_Swapchain->GetFormat())
			{
				case vk::Format::eB8G8R8A8Srgb:
					return nvrhi::Format::SBGRA8_UNORM;
				case vk::Format::eR8G8B8A8Srgb:
					return nvrhi::Format::SRGBA8_UNORM;
				case vk::Format::eB8G8R8A8Unorm:
					return nvrhi::Format::BGRA8_UNORM;
				default:
					return nvrhi::Format::RGBA8_UNORM;
			}
		}();
		const vk::ImageUsageFlags usage = m_Swapchain->GetImageUsage();
		const std::vector<vk::Image>& images = m_Swapchain->GetImages();
		std::vector<nvrhi::TextureHandle> textures;
		for (size_t i = 0; i < images.size(); ++i)
		{
			// NVRHI transitions swap chain images back to the present state at the end of every command list.
			nvrhi::TextureDesc desc = nvrhi::TextureDesc()
										  .setWidth(extent.width)
										  .setHeight(extent.height)
										  .setFormat(format)
										  .setIsRenderTarget(true)
										  .setInitialState(nvrhi::ResourceStates::Present)
										  .setKeepInitialState(true)
										  .setDebugName(std::format("Swap chain image {}", i));
			desc.isShaderResource = static_cast<bool>(usage & vk::ImageUsageFlagBits::eSampled);
			nvrhi::TextureHandle texture = m_NvrhiDevice->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, nvrhi::Object(static_cast<VkImage>(images[i])), desc);
			if (!texture)
			{
				return Error("NVRHI could not wrap the swap chain images");
			}
			textures.push_back(texture);
		}
		return SetBackBuffers(std::move(textures), format, glm::uvec2(extent.width, extent.height));
	}

	bool VulkanGraphicsDevice::BeginFrame()
	{
		GB_CORE_ASSERT(!m_InFrame, "BeginFrame called twice without Present");
		if (m_DeviceLost)
		{
			return false;
		}

		m_FrameSlot = static_cast<uint32_t>(m_FrameCount % m_Specification.FramesInFlight);
		if (m_FrameQueryPending[m_FrameSlot])
		{
			m_NvrhiDevice->waitEventQuery(m_FrameQueries[m_FrameSlot]);
			m_NvrhiDevice->resetEventQuery(m_FrameQueries[m_FrameSlot]);
			m_FrameQueryPending[m_FrameSlot] = false;
		}

		if (m_Specification.Headless)
		{
			m_BackBufferIndex = m_FrameSlot;
			m_InFrame = true;
			return true;
		}
		m_Swapchain->DestroyRetiredSemaphores(m_FrameCount);

		// Up to three attempts: a swap chain can go out of date again while it is being rebuilt (during a resize).
		for (int attempt = 0; attempt < 3; ++attempt)
		{
			const glm::uvec2 framebufferSize = m_Window->GetFramebufferSize();
			if (framebufferSize.x == 0 || framebufferSize.y == 0)
			{
				return false;
			}
			if (!m_Swapchain->HasSwapchain() || m_SwapchainDirty || framebufferSize != m_Swapchain->GetBuiltForSize()
			    || m_Specification.VSync != m_Swapchain->IsVSync())
			{
				const Result<bool> built = BuildSwapchain();
				if (!built)
				{
					GB_CORE_ERROR("Cannot recreate the swap chain: {}", built.GetError().Message);
					return false;
				}
				if (!*built)
				{
					return false;
				}
			}

			uint32_t imageIndex = 0;
			bool suboptimal = false;
			switch (m_Swapchain->Acquire(m_FrameSlot, imageIndex, suboptimal))
			{
				case VulkanSwapchain::Status::Success:
					m_SwapchainDirty = suboptimal; // rebuilt at the next BeginFrame; this image is still usable
					m_BackBufferIndex = imageIndex;
					m_VulkanNvrhiDevice->queueWaitForSemaphore(
						nvrhi::CommandQueue::Graphics,
						static_cast<VkSemaphore>(m_Swapchain->GetAcquireSemaphore(m_FrameSlot)), 0);
					m_InFrame = true;
					return true;
				case VulkanSwapchain::Status::OutOfDate:
					m_SwapchainDirty = true;
					break;
				case VulkanSwapchain::Status::DeviceLost:
					MarkDeviceLost();
					return false;
				case VulkanSwapchain::Status::Failed:
					return false;
			}
		}
		return false;
	}

	bool VulkanGraphicsDevice::Present()
	{
		GB_CORE_ASSERT(m_InFrame, "Present without a successful BeginFrame");
		if (!m_InFrame)
		{
			return !m_DeviceLost;
		}
		m_InFrame = false;

		if (!m_Specification.Headless)
		{
			m_VulkanNvrhiDevice->queueSignalSemaphore(
				nvrhi::CommandQueue::Graphics,
				static_cast<VkSemaphore>(m_Swapchain->GetPresentSemaphore(m_BackBufferIndex)), 0);
			// This submission carries the semaphore signal, and moves an image that nothing rendered to this frame out
			// of its undefined first-use layout.
			m_PresentCommandList->open();
			m_PresentCommandList->setTextureState(
				m_BackBuffers[m_BackBufferIndex], nvrhi::AllSubresources, nvrhi::ResourceStates::Present);
			m_PresentCommandList->commitBarriers();
			m_PresentCommandList->close();
			m_NvrhiDevice->executeCommandList(m_PresentCommandList);

			bool suboptimal = false;
			switch (m_Swapchain->Present(m_Queue, m_BackBufferIndex, suboptimal))
			{
				case VulkanSwapchain::Status::Success:
					m_SwapchainDirty = m_SwapchainDirty || suboptimal;
					break;
				case VulkanSwapchain::Status::OutOfDate:
					m_SwapchainDirty = true;
					break;
				case VulkanSwapchain::Status::DeviceLost:
					MarkDeviceLost();
					break;
				case VulkanSwapchain::Status::Failed:
					break;
			}
		}

		m_NvrhiDevice->setEventQuery(m_FrameQueries[m_FrameSlot], nvrhi::CommandQueue::Graphics);
		m_FrameQueryPending[m_FrameSlot] = true;
		m_NvrhiDevice->runGarbageCollection();
		++m_FrameCount;
		return !m_DeviceLost;
	}

	nvrhi::ITexture* VulkanGraphicsDevice::GetCurrentBackBuffer() const
	{
		GB_CORE_ASSERT(m_InFrame, "the back buffer is only valid between BeginFrame and Present");
		return m_BackBufferIndex < m_BackBuffers.size() ? m_BackBuffers[m_BackBufferIndex].Get() : nullptr;
	}

	nvrhi::IFramebuffer* VulkanGraphicsDevice::GetCurrentFramebuffer() const
	{
		GB_CORE_ASSERT(m_InFrame, "the framebuffer is only valid between BeginFrame and Present");
		return m_BackBufferIndex < m_Framebuffers.size() ? m_Framebuffers[m_BackBufferIndex].Get() : nullptr;
	}

	Result<void> VulkanGraphicsDevice::ResizeBackBuffers(uint32_t width, uint32_t height)
	{
		if (!m_Specification.Headless)
		{
			return Error("a windowed graphics device takes its back-buffer size from the window");
		}
		if (m_InFrame)
		{
			return Error("the back buffers cannot be resized between BeginFrame and Present");
		}
		if (width == 0 || height == 0 || width > Image::MaxDimension || height > Image::MaxDimension)
		{
			return Error(std::format(
				"invalid back-buffer size {}x{} (1 to {} in each direction)", width, height, Image::MaxDimension));
		}
		if (glm::uvec2(width, height) == m_BackBufferSize)
		{
			return {};
		}
		// The old back buffers stay in place when the new ones cannot be made.
		WaitIdle();
		if (Result<void> created = CreateHeadlessBackBuffers(width, height); !created)
		{
			return Error(created.GetError().Message);
		}
		m_Specification.BackBufferWidth = width;
		m_Specification.BackBufferHeight = height;
		return {};
	}

	void VulkanGraphicsDevice::WaitIdle()
	{
		if (!m_NvrhiDevice->waitForIdle())
		{
			MarkDeviceLost();
		}
		m_NvrhiDevice->runGarbageCollection();
	}

	void VulkanGraphicsDevice::MarkDeviceLost()
	{
		if (!m_DeviceLost)
		{
			GB_CORE_CRITICAL("The graphics device was lost; nothing more can be rendered");
		}
		m_DeviceLost = true;
	}
}
