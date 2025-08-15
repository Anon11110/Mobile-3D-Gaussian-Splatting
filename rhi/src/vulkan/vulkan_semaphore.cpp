#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanSemaphore::VulkanSemaphore(VkDevice device) :
    device(device), semaphore(VK_NULL_HANDLE)
{
	VkSemaphoreCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	if (vkCreateSemaphore(device, &createInfo, nullptr, &semaphore) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan semaphore");
	}
}

VulkanSemaphore::~VulkanSemaphore()
{
	if (semaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(device, semaphore, nullptr);
	}
}

VulkanSemaphore::VulkanSemaphore(VulkanSemaphore &&other) noexcept :
    device(other.device),
    semaphore(other.semaphore)
{
	other.device    = VK_NULL_HANDLE;
	other.semaphore = VK_NULL_HANDLE;
}

VulkanSemaphore &VulkanSemaphore::operator=(VulkanSemaphore &&other) noexcept
{
	if (this != &other)
	{
		if (semaphore != VK_NULL_HANDLE)
		{
			vkDestroySemaphore(device, semaphore, nullptr);
		}

		device    = other.device;
		semaphore = other.semaphore;

		other.device    = VK_NULL_HANDLE;
		other.semaphore = VK_NULL_HANDLE;
	}
	return *this;
}

}        // namespace rhi::vulkan
