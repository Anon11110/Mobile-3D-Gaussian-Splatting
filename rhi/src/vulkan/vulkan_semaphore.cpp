#include "vulkan_backend.h"
#include <stdexcept>

namespace RHI {

VulkanSemaphore::VulkanSemaphore(VkDevice device) : device(device), semaphore(VK_NULL_HANDLE) {
    VkSemaphoreCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    if (vkCreateSemaphore(device, &createInfo, nullptr, &semaphore) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan semaphore");
    }
}

VulkanSemaphore::~VulkanSemaphore() {
    if (semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
}

} // namespace RHI