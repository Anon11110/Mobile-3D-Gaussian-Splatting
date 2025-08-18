#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

#include "vulkan_backend.h"

#include <GLFW/glfw3.h>

namespace rhi::vulkan
{

class VulkanDevice : public IRHIDevice
{
  private:
	VkInstance       instance;
	VkPhysicalDevice physicalDevice;
	VkDevice         device;

	// Queue families and queues
	VkQueue  graphicsQueue;
	VkQueue  computeQueue;
	VkQueue  transferQueue;
	uint32_t graphicsQueueFamily;
	uint32_t computeQueueFamily;
	uint32_t transferQueueFamily;

	// Command pools for different queue families
	VkCommandPool graphicsCommandPool;
	VkCommandPool computeCommandPool;
	VkCommandPool transferCommandPool;
	VmaAllocator  allocator;

	VkDescriptorPool graphicsDescriptorPool;
	VkDescriptorPool computeDescriptorPool;
	VkDescriptorPool transferDescriptorPool;

	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
	PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR;

	// Device feature flags
	bool hasDynamicRendering;

#if defined(DEBUG) || defined(_DEBUG)
	VkDebugUtilsMessengerEXT debugMessenger;
#endif

  public:
	VulkanDevice()
	{
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		CreateInstance();
		SetupDebugMessenger();
		PickPhysicalDevice();
		CreateLogicalDevice();
		CreateCommandPools();
		CreateVmaAllocator();
		CreateDescriptorPools();
		CacheDynamicRenderingFunctions();
	}

	~VulkanDevice() override
	{
		// Wait for all operations to complete before cleanup
		if (device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(device);
		}

		// Destroy descriptor pools first
		if (graphicsDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, graphicsDescriptorPool, nullptr);
		}
		if (computeDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, computeDescriptorPool, nullptr);
		}
		if (transferDescriptorPool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(device, transferDescriptorPool, nullptr);
		}

		// Destroy command pools
		if (graphicsCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, graphicsCommandPool, nullptr);
		}
		if (computeCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, computeCommandPool, nullptr);
		}
		if (transferCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, transferCommandPool, nullptr);
		}

		// IMPORTANT: VMA allocator must be destroyed AFTER all buffers/textures are destroyed
		// but BEFORE the Vulkan device is destroyed
		if (allocator != VK_NULL_HANDLE)
		{
			vmaDestroyAllocator(allocator);
		}

		// Destroy device
		if (device != VK_NULL_HANDLE)
		{
			vkDestroyDevice(device, nullptr);
		}

#if defined(DEBUG) || defined(_DEBUG)
		DestroyDebugMessenger();
#endif

		if (instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(instance, nullptr);
		}

		glfwTerminate();
	}

	// IRHIDevice implementation
	std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc &desc) override
	{
		auto buffer = std::make_unique<VulkanBuffer>(allocator, desc);

		if (desc.initialData != nullptr && desc.resourceUsage == ResourceUsage::Static)
		{
			// Handle initial data upload for static buffers using staging
			UploadToStaticBuffer(buffer.get(), desc.initialData, desc.size);
		}
		else if (desc.initialData != nullptr && desc.resourceUsage != ResourceUsage::Static)
		{
			// For non-static buffers, upload directly (they're host-visible)
			void *mapped = buffer->Map();
			memcpy(mapped, desc.initialData, desc.size);
			buffer->Unmap();
		}

		return buffer;
	}

	std::unique_ptr<IRHITexture> CreateTexture(const TextureDesc &desc) override
	{
		auto texture = std::make_unique<VulkanTexture>(device, allocator, desc);

		if (desc.initialData != nullptr && desc.resourceUsage == ResourceUsage::Static)
		{
			// Handle initial data upload for static textures using staging
			UploadToStaticTexture(texture.get(), desc);
		}
		else if (desc.initialData != nullptr && desc.resourceUsage != ResourceUsage::Static)
		{
			// For non-static textures, they should be mappable
			// But this is rarely used in practice, so we'll skip it for now
			throw std::runtime_error("Initial data upload for non-static textures not yet implemented");
		}

		return texture;
	}

	std::unique_ptr<IRHITextureView> CreateTextureView(const TextureViewDesc &desc) override
	{
		return std::make_unique<VulkanTextureView>(device, desc);
	}

	std::unique_ptr<IRHIShader> CreateShader(const ShaderDesc &desc) override
	{
		return std::make_unique<VulkanShader>(device, desc);
	}

	std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override
	{
		return std::make_unique<VulkanPipeline>(device, desc);
	}

	std::unique_ptr<IRHIPipeline> CreateComputePipeline(const ComputePipelineDesc &desc) override
	{
		return std::make_unique<VulkanPipeline>(device, desc);
	}

	std::unique_ptr<IRHICommandList> CreateCommandList(QueueType queueType = QueueType::GRAPHICS) override
	{
		VkCommandPool commandPool = GetCommandPool(queueType);
		return std::make_unique<VulkanCommandList>(device, commandPool, queueType, vkCmdBeginRenderingKHR, vkCmdEndRenderingKHR);
	}

	std::unique_ptr<IRHISwapchain> CreateSwapchain(const SwapchainDesc &desc) override
	{
		// Create surface from window handle
		VkSurfaceKHR surface;
		if (glfwCreateWindowSurface(instance, (GLFWwindow *) desc.windowHandle, nullptr, &surface) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create window surface");
		}
		return std::make_unique<VulkanSwapchain>(device, physicalDevice, allocator, surface, graphicsQueue, desc);
	}

	std::unique_ptr<IRHISemaphore> CreateSemaphore() override
	{
		return std::make_unique<VulkanSemaphore>(device);
	}

	std::unique_ptr<IRHIFence> CreateFence(bool signaled = false) override
	{
		return std::make_unique<VulkanFence>(device, signaled);
	}

	std::unique_ptr<IRHIDescriptorSetLayout> CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) override
	{
		return std::make_unique<VulkanDescriptorSetLayout>(device, desc);
	}

	std::unique_ptr<IRHIDescriptorSet> CreateDescriptorSet(IRHIDescriptorSetLayout *layout,
	                                                       QueueType                queueType) override
	{
		auto            *vkLayout = static_cast<VulkanDescriptorSetLayout *>(layout);
		VkDescriptorPool pool     = GetDescriptorPool(queueType);

		// Allocate descriptor set from appropriate pool
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool     = pool;
		allocInfo.descriptorSetCount = 1;
		auto layoutHandle            = vkLayout->GetHandle();
		allocInfo.pSetLayouts        = &layoutHandle;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate descriptor set");
		}

		return std::make_unique<VulkanDescriptorSet>(device, vkLayout, pool, descriptorSet);
	}

	void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists,
	                        QueueType                         queueType       = QueueType::GRAPHICS,
	                        IRHISemaphore                    *waitSemaphore   = nullptr,
	                        IRHISemaphore                    *signalSemaphore = nullptr,
	                        IRHIFence                        *signalFence     = nullptr) override
	{
		SubmitInfo submitInfo{};
		if (waitSemaphore)
		{
			static SemaphoreWaitInfo waitInfo{waitSemaphore, StageMask::RenderTarget};
			submitInfo.waitSemaphores = std::span<const SemaphoreWaitInfo>(&waitInfo, 1);
		}
		if (signalSemaphore)
		{
			submitInfo.signalSemaphores = std::span<IRHISemaphore *const>(&signalSemaphore, 1);
		}
		submitInfo.signalFence = signalFence;

		SubmitCommandLists(cmdLists, queueType, submitInfo);
	}

	void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists,
	                        QueueType                         queueType,
	                        const SubmitInfo                 &submitInfo) override
	{
		std::vector<VkCommandBuffer> commandBuffers(cmdLists.size());
		for (size_t i = 0; i < cmdLists.size(); i++)
		{
			auto *vkCmdList   = static_cast<VulkanCommandList *>(cmdLists[i]);
			commandBuffers[i] = vkCmdList->GetHandle();
		}

		VkSubmitInfo vkSubmitInfo{};
		vkSubmitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		vkSubmitInfo.commandBufferCount = static_cast<uint32_t>(cmdLists.size());
		vkSubmitInfo.pCommandBuffers    = commandBuffers.data();

		std::vector<VkSemaphore>          waitSemaphores;
		std::vector<VkPipelineStageFlags> waitStages;
		if (!submitInfo.waitSemaphores.empty())
		{
			waitSemaphores.reserve(submitInfo.waitSemaphores.size());
			waitStages.reserve(submitInfo.waitSemaphores.size());

			for (const auto &waitInfo : submitInfo.waitSemaphores)
			{
				auto *vkSemaphore = static_cast<VulkanSemaphore *>(waitInfo.semaphore);
				waitSemaphores.push_back(vkSemaphore->GetHandle());
				waitStages.push_back(StageMaskToVulkan(waitInfo.waitStage));
			}

			vkSubmitInfo.waitSemaphoreCount = static_cast<uint32_t>(submitInfo.waitSemaphores.size());
			vkSubmitInfo.pWaitSemaphores    = waitSemaphores.data();
			vkSubmitInfo.pWaitDstStageMask  = waitStages.data();
		}

		std::vector<VkSemaphore> signalSemaphores;
		if (!submitInfo.signalSemaphores.empty())
		{
			signalSemaphores.reserve(submitInfo.signalSemaphores.size());

			for (auto *semaphore : submitInfo.signalSemaphores)
			{
				auto *vkSemaphore = static_cast<VulkanSemaphore *>(semaphore);
				signalSemaphores.push_back(vkSemaphore->GetHandle());
			}

			vkSubmitInfo.signalSemaphoreCount = static_cast<uint32_t>(submitInfo.signalSemaphores.size());
			vkSubmitInfo.pSignalSemaphores    = signalSemaphores.data();
		}

		VkFence fence = VK_NULL_HANDLE;
		if (submitInfo.signalFence)
		{
			auto *vkFence = static_cast<VulkanFence *>(submitInfo.signalFence);
			fence         = vkFence->GetHandle();
		}

		VkQueue queue = GetQueue(queueType);
		if (vkQueueSubmit(queue, 1, &vkSubmitInfo, fence) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit command buffer");
		}
	}

	void WaitQueueIdle(QueueType queueType) override
	{
		VkQueue queue = GetQueue(queueType);
		vkQueueWaitIdle(queue);
	}

	void WaitIdle() override
	{
		vkDeviceWaitIdle(device);
	}

  private:
	VkDescriptorPool GetDescriptorPool(QueueType queueType)
	{
		switch (queueType)
		{
			case QueueType::GRAPHICS:
				return graphicsDescriptorPool;
			case QueueType::COMPUTE:
				return computeDescriptorPool;
			case QueueType::TRANSFER:
				return transferDescriptorPool;
			default:
				return graphicsDescriptorPool;
		}
	}

	VkQueue GetQueue(QueueType queueType)
	{
		switch (queueType)
		{
			case QueueType::GRAPHICS:
				return graphicsQueue;
			case QueueType::COMPUTE:
				return computeQueue;
			case QueueType::TRANSFER:
				return transferQueue;
			default:
				return graphicsQueue;
		}
	}

	VkCommandPool GetCommandPool(QueueType queueType)
	{
		switch (queueType)
		{
			case QueueType::GRAPHICS:
				return graphicsCommandPool;
			case QueueType::COMPUTE:
				return computeCommandPool;
			case QueueType::TRANSFER:
				return transferCommandPool;
			default:
				return graphicsCommandPool;
		}
	}

	void CreateInstance()
	{
		VkApplicationInfo appInfo{};
		appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName   = "RHI Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName        = "RHI";
		appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion         = VK_API_VERSION_1_3;        // Request 1.3 for dynamic rendering core support

		VkInstanceCreateInfo createInfo{};
		createInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

// Enable portability enumeration for MoltenVK
#ifdef __APPLE__
		createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

		// Get required extensions
		uint32_t     glfwExtensionCount = 0;
		const char **glfwExtensions     = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		if (glfwExtensions == nullptr)
		{
			throw std::runtime_error("GLFW failed to get required Vulkan extensions");
		}

		std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

// Add MoltenVK required extension for macOS
#ifdef __APPLE__
		extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

#if defined(DEBUG) || defined(_DEBUG)
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

		createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

#if defined(DEBUG) || defined(_DEBUG)
		const std::vector<const char *> validationLayers = {
		    "VK_LAYER_KHRONOS_validation"};

		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		bool layersAvailable = true;
		for (const char *layerName : validationLayers)
		{
			bool layerFound = false;
			for (const auto &layerProperties : availableLayers)
			{
				if (strcmp(layerName, layerProperties.layerName) == 0)
				{
					layerFound = true;
					break;
				}
			}
			if (!layerFound)
			{
				fprintf(stderr, "Warning: Validation layer %s not available\n", layerName);
				layersAvailable = false;
			}
		}

		if (layersAvailable)
		{
			createInfo.enabledLayerCount   = static_cast<uint32_t>(validationLayers.size());
			createInfo.ppEnabledLayerNames = validationLayers.data();
			fprintf(stdout, "Vulkan validation layers enabled\n");
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			fprintf(stderr, "Warning: Validation layers requested but not available\n");
		}
#else
		createInfo.enabledLayerCount = 0;
#endif

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create Vulkan instance");
		}
	}

	void SetupDebugMessenger()
	{
#if defined(DEBUG) || defined(_DEBUG)
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		// Enable all severity levels except verbose (too noisy)
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback =
		    [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
		       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) -> VkBool32 {
			const char *severityStr = "";
			const char *typeStr     = "";

			switch (messageSeverity)
			{
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
					severityStr = "[VERBOSE]";
					break;
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
					severityStr = "[INFO]";
					break;
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
					severityStr = "[WARNING]";
					break;
				case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
					severityStr = "[ERROR]";
					break;
				default:
					severityStr = "[UNKNOWN]";
			}

			if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
				typeStr = "General";
			else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
				typeStr = "Validation";
			else if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
				typeStr = "Performance";

			fprintf(stderr, "\n%s Vulkan %s:\n", severityStr, typeStr);

			if (pCallbackData->pMessageIdName)
			{
				fprintf(stderr, "  Message ID: %s (0x%x)\n",
				        pCallbackData->pMessageIdName, pCallbackData->messageIdNumber);
			}

			fprintf(stderr, "  Message: %s\n", pCallbackData->pMessage);

			if (pCallbackData->objectCount > 0)
			{
				fprintf(stderr, "  Objects involved:\n");
				for (uint32_t i = 0; i < pCallbackData->objectCount; i++)
				{
					const VkDebugUtilsObjectNameInfoEXT &obj = pCallbackData->pObjects[i];
					fprintf(stderr, "    - Type: %d, Handle: 0x%llx",
					        obj.objectType, (unsigned long long) obj.objectHandle);
					if (obj.pObjectName)
					{
						fprintf(stderr, ", Name: %s", obj.pObjectName);
					}
					fprintf(stderr, "\n");
				}
			}

			if (pCallbackData->queueLabelCount > 0)
			{
				fprintf(stderr, "  Queue Labels:\n");
				for (uint32_t i = 0; i < pCallbackData->queueLabelCount; i++)
				{
					fprintf(stderr, "    - %s\n", pCallbackData->pQueueLabels[i].pLabelName);
				}
			}

			if (pCallbackData->cmdBufLabelCount > 0)
			{
				fprintf(stderr, "  Command Buffer Labels:\n");
				for (uint32_t i = 0; i < pCallbackData->cmdBufLabelCount; i++)
				{
					fprintf(stderr, "    - %s\n", pCallbackData->pCmdBufLabels[i].pLabelName);
				}
			}

			fprintf(stderr, "\n");

			if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
#	ifdef _WIN32
				__debugbreak();
#	endif
			}

			return VK_FALSE;
		};

		auto func =
		    (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
		if (func != nullptr)
		{
			func(instance, &createInfo, nullptr, &debugMessenger);
		}
#endif
	}

	void DestroyDebugMessenger()
	{
#if defined(DEBUG) || defined(_DEBUG)
		auto func =
		    (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != nullptr)
		{
			func(instance, debugMessenger, nullptr);
		}
#endif
	}

	void PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

		if (deviceCount == 0)
		{
			throw std::runtime_error("No GPUs with Vulkan support");
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

		// Pick first suitable device (can be improved)
		physicalDevice = devices[0];

		// Find queue families
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		graphicsQueueFamily = UINT32_MAX;
		computeQueueFamily  = UINT32_MAX;
		transferQueueFamily = UINT32_MAX;

		// Find dedicated compute queue (without graphics)
		for (uint32_t i = 0; i < queueFamilyCount; i++)
		{
			if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			    !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			{
				computeQueueFamily = i;
				break;
			}
		}

		// Find dedicated transfer queue (without graphics and compute)
		for (uint32_t i = 0; i < queueFamilyCount; i++)
		{
			if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			    !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			    !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
			{
				transferQueueFamily = i;
				break;
			}
		}

		// Find graphics queue (supports graphics, compute, and transfer)
		for (uint32_t i = 0; i < queueFamilyCount; i++)
		{
			if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				graphicsQueueFamily = i;
				// If we didn't find dedicated queues, use graphics queue for all
				if (computeQueueFamily == UINT32_MAX)
					computeQueueFamily = i;
				if (transferQueueFamily == UINT32_MAX)
					transferQueueFamily = i;
				break;
			}
		}

		if (graphicsQueueFamily == UINT32_MAX)
		{
			throw std::runtime_error("Failed to find graphics queue family");
		}
	}

	void CreateLogicalDevice()
	{
		std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily, computeQueueFamily, transferQueueFamily};

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		float                                queuePriority = 1.0f;

		for (uint32_t queueFamily : uniqueQueueFamilies)
		{
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount       = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back(queueCreateInfo);
		}

		VkPhysicalDeviceFeatures deviceFeatures{};

		// Check if Vulkan 1.3 is supported (dynamic rendering is core in 1.3)
		VkPhysicalDeviceProperties deviceProperties;
		vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
		uint32_t apiVersion = deviceProperties.apiVersion;

		// Enable dynamic rendering if available
		VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{};
		dynamicRenderingFeatures.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
		dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		createInfo.pQueueCreateInfos    = queueCreateInfos.data();
		createInfo.pEnabledFeatures     = &deviceFeatures;

		// Check available device extensions
		uint32_t deviceExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount,
		                                     availableExtensions.data());

		std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

		hasDynamicRendering = false;

		auto isExtensionAvailable = [&availableExtensions](const char *extensionName) {
			for (const auto &ext : availableExtensions)
			{
				if (strcmp(ext.extensionName, extensionName) == 0)
					return true;
			}
			return false;
		};

		// Add VMA-related extensions
		if (isExtensionAvailable(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME))
		{
			deviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
		}
		if (isExtensionAvailable(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME))
		{
			deviceExtensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
		}
		if (isExtensionAvailable(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME))
		{
			deviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
		}

		// Check for dynamic rendering support
		// For Vulkan 1.3+, dynamic rendering is core and doesn't need the extension
		// For Vulkan 1.2 and below, we need the extension
		if (apiVersion >= VK_API_VERSION_1_3)
		{
			// Dynamic rendering is core in Vulkan 1.3, no extension needed
			hasDynamicRendering = true;
			fprintf(stdout, "Dynamic rendering supported via Vulkan 1.3 core\n");
		}
		else if (isExtensionAvailable(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
		{
			// For older Vulkan versions, check if we can use the extension
			deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

			if (isExtensionAvailable(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME);
			}
			if (isExtensionAvailable(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
			}
			if (isExtensionAvailable(VK_KHR_MULTIVIEW_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_KHR_MULTIVIEW_EXTENSION_NAME);
			}
			if (isExtensionAvailable(VK_KHR_MAINTENANCE2_EXTENSION_NAME))
			{
				deviceExtensions.push_back(VK_KHR_MAINTENANCE2_EXTENSION_NAME);
			}

			hasDynamicRendering = true;
			fprintf(stdout, "Dynamic rendering enabled via VK_KHR_dynamic_rendering extension\n");
		}
		else
		{
			hasDynamicRendering = false;
			fprintf(stderr, "Warning: Dynamic rendering not available, falling back to render passes\n");
		}

#ifdef __APPLE__
		if (isExtensionAvailable("VK_KHR_portability_subset"))
		{
			deviceExtensions.push_back("VK_KHR_portability_subset");
		}
#endif

		createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if (hasDynamicRendering)
		{
			createInfo.pNext = &dynamicRenderingFeatures;
		}

		if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create logical device");
		}

		vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
		vkGetDeviceQueue(device, computeQueueFamily, 0, &computeQueue);
		vkGetDeviceQueue(device, transferQueueFamily, 0, &transferQueue);
	}

	void CreateCommandPools()
	{
		// Create graphics command pool
		VkCommandPoolCreateInfo graphicsPoolInfo{};
		graphicsPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		graphicsPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		graphicsPoolInfo.queueFamilyIndex = graphicsQueueFamily;

		if (vkCreateCommandPool(device, &graphicsPoolInfo, nullptr, &graphicsCommandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create graphics command pool");
		}

		// Create compute command pool
		VkCommandPoolCreateInfo computePoolInfo{};
		computePoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		computePoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		computePoolInfo.queueFamilyIndex = computeQueueFamily;

		if (vkCreateCommandPool(device, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create compute command pool");
		}

		// Create transfer command pool
		VkCommandPoolCreateInfo transferPoolInfo{};
		transferPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		transferPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		transferPoolInfo.queueFamilyIndex = transferQueueFamily;

		if (vkCreateCommandPool(device, &transferPoolInfo, nullptr, &transferCommandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create transfer command pool");
		}
	}

	void CreateVmaAllocator()
	{
		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
		allocatorInfo.physicalDevice   = physicalDevice;
		allocatorInfo.device           = device;
		allocatorInfo.instance         = instance;

		// Check if extensions are available before enabling them
		uint32_t deviceExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
		std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data());

		bool hasGetMemoryRequirements2 = false;
		bool hasBindMemory2            = false;
		bool hasDedicatedAllocation    = false;

		for (const auto &ext : deviceExtensions)
		{
			if (strcmp(ext.extensionName, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
			{
				hasGetMemoryRequirements2 = true;
			}
			else if (strcmp(ext.extensionName, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 0)
			{
				hasBindMemory2 = true;
			}
			else if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
			{
				hasDedicatedAllocation = true;
			}
		}

		// Only enable extensions that are actually available
		allocatorInfo.flags = 0;
		if (hasDedicatedAllocation && hasGetMemoryRequirements2)
		{
			allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
		}
		if (hasBindMemory2)
		{
			allocatorInfo.flags |= VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT;
		}

		if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create VMA allocator");
		}
	}

	void UploadToStaticBuffer(VulkanBuffer *dstBuffer, const void *data, size_t size)
	{
		// Create staging buffer in host-visible memory
		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size        = size;
		stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocInfo{};
		stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer          stagingBuffer;
		VmaAllocation     stagingAllocation;
		VmaAllocationInfo stagingAllocResult;

		if (vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation,
		                    &stagingAllocResult) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create staging buffer");
		}

		// Copy data to staging buffer
		memcpy(stagingAllocResult.pMappedData, data, size);
		vmaFlushAllocation(allocator, stagingAllocation, 0, VK_WHOLE_SIZE);

		// Create and record copy command
		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAllocInfo.commandPool        = transferCommandPool;
		cmdAllocInfo.commandBufferCount = 1;

		VkCommandBuffer copyCmd;
		vkAllocateCommandBuffers(device, &cmdAllocInfo, &copyCmd);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(copyCmd, &beginInfo);

		VkBufferCopy copyRegion{};
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = 0;
		copyRegion.size      = size;
		vkCmdCopyBuffer(copyCmd, stagingBuffer, dstBuffer->GetHandle(), 1, &copyRegion);

		vkEndCommandBuffer(copyCmd);

		// Submit and wait
		VkSubmitInfo submitInfo{};
		submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers    = &copyCmd;

		// TODO: Async transfer? Because this is a one-time setup cost that happens when the
		// user is already waiting for buffer creation, using vkQueueWaitIdle may be acceptable
		vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(transferQueue);

		vkFreeCommandBuffers(device, transferCommandPool, 1, &copyCmd);
		vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
	}

	void UploadToStaticTexture(VulkanTexture *dstTexture, const TextureDesc &desc)
	{
		// Calculate staging buffer size based on texture format and dimensions
		size_t pixelSize   = GetPixelSize(desc.format);
		size_t stagingSize = desc.width * desc.height * desc.depth * pixelSize;

		// Create staging buffer in host-visible memory
		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size        = stagingSize;
		stagingInfo.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocInfo{};
		stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer          stagingBuffer;
		VmaAllocation     stagingAllocation;
		VmaAllocationInfo stagingAllocResult;

		if (vmaCreateBuffer(allocator, &stagingInfo, &stagingAllocInfo, &stagingBuffer, &stagingAllocation,
		                    &stagingAllocResult) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create staging buffer for texture");
		}

		// Copy data to staging buffer
		memcpy(stagingAllocResult.pMappedData, desc.initialData, std::min(stagingSize, desc.initialDataSize));
		vmaFlushAllocation(allocator, stagingAllocation, 0, VK_WHOLE_SIZE);

		VkCommandBufferAllocateInfo cmdAllocInfo{};
		cmdAllocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdAllocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdAllocInfo.commandPool        = transferCommandPool;
		cmdAllocInfo.commandBufferCount = 1;

		VkCommandBuffer copyCmd;
		vkAllocateCommandBuffers(device, &cmdAllocInfo, &copyCmd);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		vkBeginCommandBuffer(copyCmd, &beginInfo);

		// Transition image from UNDEFINED to TRANSFER_DST_OPTIMAL
		VkImageMemoryBarrier barrier{};
		barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout                       = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
		barrier.image                           = dstTexture->GetHandle();
		barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel   = 0;
		barrier.subresourceRange.levelCount     = desc.mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount     = 1;
		barrier.srcAccessMask                   = 0;
		barrier.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
		                     0, nullptr, 1, &barrier);

		// Copy buffer to image
		VkBufferImageCopy region{};
		region.bufferOffset                    = 0;
		region.bufferRowLength                 = 0;
		region.bufferImageHeight               = 0;
		region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel       = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount     = 1;
		region.imageOffset                     = {0, 0, 0};
		region.imageExtent                     = {desc.width, desc.height, desc.depth};

		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, dstTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
		                       &region);

		// Transition image from TRANSFER_DST_OPTIMAL to SHADER_READ_ONLY_OPTIMAL
		barrier.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(copyCmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &barrier);

		vkEndCommandBuffer(copyCmd);

		// Submit and wait
		VkSubmitInfo submitInfo{};
		submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers    = &copyCmd;

		// TODO: Async transfer? Same as buffer, because this is a one-time setup cost that happens when
		// the user is already waiting for buffer creation, using vkQueueWaitIdle may be acceptable
		vkQueueSubmit(transferQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(transferQueue);

		vkFreeCommandBuffers(device, transferCommandPool, 1, &copyCmd);
		vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
	}

	size_t GetPixelSize(TextureFormat format) const
	{
		switch (format)
		{
			case TextureFormat::R8G8B8A8_UNORM:
			case TextureFormat::R8G8B8A8_SRGB:
			case TextureFormat::B8G8R8A8_UNORM:
			case TextureFormat::B8G8R8A8_SRGB:
				return 4;
			case TextureFormat::R32G32B32_FLOAT:
				return 12;
			case TextureFormat::D32_FLOAT:
				return 4;
			case TextureFormat::D24_UNORM_S8_UINT:
				return 4;
			default:
				return 4;
		}
	}

	void CreateDescriptorPools()
	{
		// Create pools with generous sizes for different queue types
		std::vector<VkDescriptorPoolSize> graphicsPoolSizes = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
		                                                       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
		                                                       {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 500},
		                                                       {VK_DESCRIPTOR_TYPE_SAMPLER, 500},
		                                                       {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 200}};

		std::vector<VkDescriptorPoolSize> computePoolSizes = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2000},
		                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500},
		                                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 500},
		                                                      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 200}};

		std::vector<VkDescriptorPoolSize> transferPoolSizes = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 100},
		                                                       {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100}};

		// Create graphics descriptor pool
		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets       = 1000;
		poolInfo.poolSizeCount = static_cast<uint32_t>(graphicsPoolSizes.size());
		poolInfo.pPoolSizes    = graphicsPoolSizes.data();

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &graphicsDescriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create graphics descriptor pool");
		}

		// Create compute descriptor pool
		poolInfo.maxSets       = 500;
		poolInfo.poolSizeCount = static_cast<uint32_t>(computePoolSizes.size());
		poolInfo.pPoolSizes    = computePoolSizes.data();

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &computeDescriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create compute descriptor pool");
		}

		// Create transfer descriptor pool
		poolInfo.maxSets       = 100;
		poolInfo.poolSizeCount = static_cast<uint32_t>(transferPoolSizes.size());
		poolInfo.pPoolSizes    = transferPoolSizes.data();

		if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &transferDescriptorPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create transfer descriptor pool");
		}
	}

	void CacheDynamicRenderingFunctions()
	{
		if (hasDynamicRendering)
		{
			// Try Vulkan 1.3 core functions first
			vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR) vkGetDeviceProcAddr(device, "vkCmdBeginRendering");
			vkCmdEndRenderingKHR   = (PFN_vkCmdEndRenderingKHR) vkGetDeviceProcAddr(device, "vkCmdEndRendering");

			// Fall back to KHR extension functions
			if (!vkCmdBeginRenderingKHR)
			{
				vkCmdBeginRenderingKHR = (PFN_vkCmdBeginRenderingKHR) vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR");
			}
			if (!vkCmdEndRenderingKHR)
			{
				vkCmdEndRenderingKHR = (PFN_vkCmdEndRenderingKHR) vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR");
			}
		}
		else
		{
			vkCmdBeginRenderingKHR = nullptr;
			vkCmdEndRenderingKHR   = nullptr;
		}
	}
};

}        // namespace rhi::vulkan

namespace rhi
{

// Factory function
std::unique_ptr<IRHIDevice> CreateRHIDevice()
{
	return std::make_unique<vulkan::VulkanDevice>();
}

}        // namespace rhi