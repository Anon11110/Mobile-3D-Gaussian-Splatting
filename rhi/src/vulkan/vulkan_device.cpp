#include <GLFW/glfw3.h>

#include <cstring>
#include <stdexcept>
#include <vector>

#include "vulkan_backend.h"

namespace RHI {

class VulkanDevice : public IRHIDevice {
  private:
    VkInstance instance;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkQueue graphicsQueue;
    uint32_t graphicsQueueFamily;
    VkCommandPool commandPool;

#ifdef DEBUG
    VkDebugUtilsMessengerEXT debugMessenger;
#endif

  public:
    VulkanDevice() {
        if (!glfwInit()) {
            throw std::runtime_error("Failed to initialize GLFW");
        }

        CreateInstance();
        SetupDebugMessenger();
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateCommandPool();
    }

    ~VulkanDevice() override {
        vkDestroyCommandPool(device, commandPool, nullptr);
        vkDestroyDevice(device, nullptr);
#ifdef DEBUG
        DestroyDebugMessenger();
#endif
        vkDestroyInstance(instance, nullptr);
        glfwTerminate();
    }

    // IRHIDevice implementation
    std::unique_ptr<IRHIBuffer> CreateBuffer(const BufferDesc& desc) override {
        return std::make_unique<VulkanBuffer>(device, physicalDevice, desc);
    }

    std::unique_ptr<IRHIShader> CreateShader(const ShaderDesc& desc) override {
        return std::make_unique<VulkanShader>(device, desc);
    }

    std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override {
        return std::make_unique<VulkanPipeline>(device, desc);
    }

    std::unique_ptr<IRHICommandList> CreateCommandList() override {
        return std::make_unique<VulkanCommandList>(device, commandPool);
    }

    std::unique_ptr<IRHISwapchain> CreateSwapchain(const SwapchainDesc& desc) override {
        // Create surface from window handle
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(instance, (GLFWwindow*)desc.windowHandle, nullptr, &surface) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create window surface");
        }
        return std::make_unique<VulkanSwapchain>(device, physicalDevice, surface, graphicsQueue, desc);
    }

    std::unique_ptr<IRHISemaphore> CreateSemaphore() override { return std::make_unique<VulkanSemaphore>(device); }

    std::unique_ptr<IRHIFence> CreateFence(bool signaled = false) override {
        return std::make_unique<VulkanFence>(device, signaled);
    }

    void SubmitCommandLists(IRHICommandList** cmdLists, uint32_t count, IRHISemaphore* waitSemaphore,
                            IRHISemaphore* signalSemaphore, IRHIFence* signalFence) override {
        std::vector<VkCommandBuffer> commandBuffers(count);
        for (uint32_t i = 0; i < count; ++i) {
            auto* vkCmdList = static_cast<VulkanCommandList*>(cmdLists[i]);
            commandBuffers[i] = vkCmdList->GetHandle();
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = count;
        submitInfo.pCommandBuffers = commandBuffers.data();

        VkSemaphore waitSem = VK_NULL_HANDLE;
        if (waitSemaphore) {
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            auto* vkSemaphore = static_cast<VulkanSemaphore*>(waitSemaphore);
            waitSem = vkSemaphore->GetHandle();
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &waitSem;
            submitInfo.pWaitDstStageMask = waitStages;
        }

        VkSemaphore signalSem = VK_NULL_HANDLE;
        if (signalSemaphore) {
            auto* vkSemaphore = static_cast<VulkanSemaphore*>(signalSemaphore);
            signalSem = vkSemaphore->GetHandle();
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &signalSem;
        }

        VkFence fence = VK_NULL_HANDLE;
        if (signalFence) {
            auto* vkFence = static_cast<VulkanFence*>(signalFence);
            fence = vkFence->GetHandle();
        }

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit command buffer");
        }
    }

    void WaitIdle() override { vkDeviceWaitIdle(device); }

  private:
    void CreateInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RHI Triangle";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "RHI";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

// Enable portability enumeration for MoltenVK
#ifdef __APPLE__
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

        // Get required extensions
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        if (glfwExtensions == nullptr) {
            throw std::runtime_error("GLFW failed to get required Vulkan extensions");
        }

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

// Add MoltenVK required extension for macOS
#ifdef __APPLE__
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

        // Debug extension disabled for now

        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        // Validation layers (disabled for now to avoid macOS/MoltenVK issues)
        createInfo.enabledLayerCount = 0;

        if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create Vulkan instance");
        }
    }

    void SetupDebugMessenger() {
#ifdef DEBUG
        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback =
            [](VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType,
               const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) -> VkBool32 {
            fprintf(stderr, "Validation layer: %s\n", pCallbackData->pMessage);
            return VK_FALSE;
        };

        auto func =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, &createInfo, nullptr, &debugMessenger);
        }
#endif
    }

    void DestroyDebugMessenger() {
#ifdef DEBUG
        auto func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, nullptr);
        }
#endif
    }

    void PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

        if (deviceCount == 0) {
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
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsQueueFamily = i;
                break;
            }
        }

        if (graphicsQueueFamily == UINT32_MAX) {
            throw std::runtime_error("Failed to find graphics queue family");
        }
    }

    void CreateLogicalDevice() {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;

        const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = deviceExtensions;

        if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device");
        }

        vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
    }

    void CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily;

        if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create command pool");
        }
    }
};

// Factory function
std::unique_ptr<IRHIDevice> CreateRHIDevice() {
    return std::make_unique<VulkanDevice>();
}

}  // namespace RHI