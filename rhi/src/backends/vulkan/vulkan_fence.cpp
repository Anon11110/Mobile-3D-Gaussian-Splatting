#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanFence::VulkanFence(VkDevice device, bool signaled) :
    device(device), fence(VK_NULL_HANDLE)
{
	VkFenceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	if (signaled)
	{
		createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	}

	if (vkCreateFence(device, &createInfo, nullptr, &fence) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan fence");
	}
}

VulkanFence::~VulkanFence()
{
	if (fence != VK_NULL_HANDLE)
	{
		vkDestroyFence(device, fence, nullptr);
	}
}

VulkanFence::VulkanFence(VulkanFence &&other) noexcept :
    device(other.device),
    fence(other.fence)
{
	other.device = VK_NULL_HANDLE;
	other.fence  = VK_NULL_HANDLE;
}

VulkanFence &VulkanFence::operator=(VulkanFence &&other) noexcept
{
	if (this != &other)
	{
		if (fence != VK_NULL_HANDLE)
		{
			vkDestroyFence(device, fence, nullptr);
		}

		device = other.device;
		fence  = other.fence;

		other.device = VK_NULL_HANDLE;
		other.fence  = VK_NULL_HANDLE;
	}
	return *this;
}

void VulkanFence::Wait(uint64_t timeout)
{
	VkResult result = vkWaitForFences(device, 1, &fence, VK_TRUE, timeout);
	if (result == VK_TIMEOUT)
	{
		throw std::runtime_error("Fence wait timeout");
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to wait for fence");
	}
}

void VulkanFence::Reset()
{
	if (vkResetFences(device, 1, &fence) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to reset fence");
	}
}

bool VulkanFence::IsSignaled() const
{
	VkResult result = vkGetFenceStatus(device, fence);
	return result == VK_SUCCESS;
}

// VulkanCompositeFence implementation
VulkanCompositeFence::VulkanCompositeFence(std::vector<FenceHandle> fences) :
    m_fences(std::move(fences))
{
}

void VulkanCompositeFence::Wait(uint64_t timeout)
{
	for (const auto &fence : m_fences)
	{
		if (fence)
		{
			fence->Wait(timeout);
		}
	}
}

void VulkanCompositeFence::Reset()
{
	for (const auto &fence : m_fences)
	{
		if (fence)
		{
			fence->Reset();
		}
	}
}

bool VulkanCompositeFence::IsSignaled() const
{
	for (const auto &fence : m_fences)
	{
		if (fence && !fence->IsSignaled())
		{
			return false;
		}
	}
	return true;
}

}        // namespace rhi::vulkan