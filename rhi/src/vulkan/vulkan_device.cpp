#include <cstring>
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
	VkQueue          graphicsQueue;
	uint32_t         graphicsQueueFamily;
	VkCommandPool    commandPool;
	VmaAllocator     allocator;

	VkDescriptorPool graphicsDescriptorPool;
	VkDescriptorPool computeDescriptorPool;
	VkDescriptorPool transferDescriptorPool;

	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
	PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR;

	// Device feature flags
	bool hasDynamicRendering;

#ifdef DEBUG
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
		CreateCommandPool();
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

		// Destroy command pool
		if (commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(device, commandPool, nullptr);
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

#ifdef DEBUG
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

	std::unique_ptr<IRHICommandList> CreateCommandList() override
	{
		return std::make_unique<VulkanCommandList>(device, commandPool, vkCmdBeginRenderingKHR, vkCmdEndRenderingKHR);
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

	void SubmitCommandLists(IRHICommandList **cmdLists, uint32_t count, IRHISemaphore *waitSemaphore,
	                        IRHISemaphore *signalSemaphore, IRHIFence *signalFence) override
	{
		std::vector<VkCommandBuffer> commandBuffers(count);
		for (uint32_t i = 0; i < count; ++i)
		{
			auto *vkCmdList   = static_cast<VulkanCommandList *>(cmdLists[i]);
			commandBuffers[i] = vkCmdList->GetHandle();
		}

		VkSubmitInfo submitInfo{};
		submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = count;
		submitInfo.pCommandBuffers    = commandBuffers.data();

		VkSemaphore          waitSem      = VK_NULL_HANDLE;
		VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
		if (waitSemaphore)
		{
			auto *vkSemaphore             = static_cast<VulkanSemaphore *>(waitSemaphore);
			waitSem                       = vkSemaphore->GetHandle();
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores    = &waitSem;
			submitInfo.pWaitDstStageMask  = waitStages;
		}

		VkSemaphore signalSem = VK_NULL_HANDLE;
		if (signalSemaphore)
		{
			auto *vkSemaphore               = static_cast<VulkanSemaphore *>(signalSemaphore);
			signalSem                       = vkSemaphore->GetHandle();
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores    = &signalSem;
		}

		VkFence fence = VK_NULL_HANDLE;
		if (signalFence)
		{
			auto *vkFence = static_cast<VulkanFence *>(signalFence);
			fence         = vkFence->GetHandle();
		}

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to submit command buffer");
		}
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

	void CreateInstance()
	{
		VkApplicationInfo appInfo{};
		appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName   = "RHI Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName        = "RHI";
		appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion         = VK_API_VERSION_1_0;

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

		// Debug extension disabled for now

		createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
		createInfo.ppEnabledExtensionNames = extensions.data();

		// Validation layers (disabled for now to avoid macOS/MoltenVK issues)
		createInfo.enabledLayerCount = 0;

		if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create Vulkan instance");
		}
	}

	void SetupDebugMessenger()
	{
#ifdef DEBUG
		VkDebugUtilsMessengerCreateInfoEXT createInfo{};
		createInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback =
		    [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
		       const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData, void *pUserData) -> VkBool32 {
			fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
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
#ifdef DEBUG
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

		// Find graphics queue family
		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

		graphicsQueueFamily = UINT32_MAX;
		for (uint32_t i = 0; i < queueFamilyCount; i++)
		{
			if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				graphicsQueueFamily = i;
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
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
		queueCreateInfo.queueCount       = 1;
		float queuePriority              = 1.0f;
		queueCreateInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceFeatures deviceFeatures{};

		// Enable dynamic rendering if available
		VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{};
		dynamicRenderingFeatures.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
		dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

		VkDeviceCreateInfo createInfo{};
		createInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos    = &queueCreateInfo;
		createInfo.pEnabledFeatures     = &deviceFeatures;

		// Check available device extensions
		uint32_t deviceExtensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(deviceExtensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &deviceExtensionCount,
		                                     availableExtensions.data());

		std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

		hasDynamicRendering = false;
		for (const auto &ext : availableExtensions)
		{
			if (strcmp(ext.extensionName, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME) == 0)
			{
				deviceExtensions.push_back(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
			}
			else if (strcmp(ext.extensionName, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME) == 0)
			{
				deviceExtensions.push_back(VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
			}
			else if (strcmp(ext.extensionName, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME) == 0)
			{
				deviceExtensions.push_back(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);
			}
			else if (strcmp(ext.extensionName, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) == 0)
			{
				deviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
				hasDynamicRendering = true;
			}
#ifdef __APPLE__
			else if (strcmp(ext.extensionName, "VK_KHR_portability_subset") == 0)
			{
				deviceExtensions.push_back("VK_KHR_portability_subset");
			}
#endif
		}

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
	}

	void CreateCommandPool()
	{
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = graphicsQueueFamily;

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create command pool");
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
		cmdAllocInfo.commandPool        = commandPool;
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
		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);
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
		cmdAllocInfo.commandPool        = commandPool;
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
		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers(device, commandPool, 1, &copyCmd);
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