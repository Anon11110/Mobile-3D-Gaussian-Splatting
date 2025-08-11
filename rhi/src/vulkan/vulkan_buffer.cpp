#include <cstring>
#include <stdexcept>

#include "vulkan_backend.h"

namespace RHI
{

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, const BufferDesc &desc) :
    allocator(allocator), buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), size(desc.size), mappedData(nullptr)
{
	// Create buffer info
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size        = desc.size;
	bufferInfo.usage       = BufferUsageToVulkan(desc.usage);
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	switch (desc.resourceUsage)
	{
		case ResourceUsage::Static:
			// Immutable data - always prefer fastest device local memory
			// Initial data upload is handled separately via staging
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;

		case ResourceUsage::DynamicUpload:
			// Frequently updated from CPU - needs host visibility
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			if (desc.hints.persistently_mapped)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			break;

		case ResourceUsage::Readback:
			// GPU writes, CPU reads - needs host visibility
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			if (desc.hints.persistently_mapped)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			break;

		case ResourceUsage::Transient:
			// Per-frame temporary buffers - prefer device local
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			// Transient resources might benefit from aliasing
			allocInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
			break;
	}

	// Apply additional hints
	if (!desc.hints.prefer_device_local && desc.resourceUsage == ResourceUsage::Static)
	{
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	}

	// Let VMA decide on dedicated allocations unless explicitly requested
	// VMA will automatically use dedicated memory for large resources when beneficial
	if (desc.hints.allow_dedicated)
	{
		allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	}

	VmaAllocationInfo allocationInfo;

	VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &allocationInfo);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan buffer with VMA");
	}

	if (allocationInfo.pMappedData != nullptr)
	{
		mappedData = allocationInfo.pMappedData;
	}

	// Note: Initial data upload for Static buffers should be handled at a higher level
	// using staging buffers for optimal performance
}

VulkanBuffer::~VulkanBuffer()
{
	if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE)
	{
		vmaDestroyBuffer(allocator, buffer, allocation);
		buffer     = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
	}
}

void *VulkanBuffer::Map()
{
	if (mappedData != nullptr)
	{
		return mappedData;
	}

	if (vmaMapMemory(allocator, allocation, &mappedData) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to map buffer memory");
	}

	return mappedData;
}

void VulkanBuffer::Unmap()
{
	// Only unmap if not persistently mapped
	// If persistently mapped, keep the pointer valid
	if (mappedData != nullptr && allocation != VK_NULL_HANDLE)
	{
		VmaAllocationInfo allocInfo;
		vmaGetAllocationInfo(allocator, allocation, &allocInfo);

		if (allocInfo.pMappedData == nullptr)
		{
			vmaUnmapMemory(allocator, allocation);
			mappedData = nullptr;
		}
	}
}

size_t VulkanBuffer::GetSize() const
{
	return size;
}

// Utility function implementations
VkBufferUsageFlags BufferUsageToVulkan(BufferUsage usage)
{
	VkBufferUsageFlags result = 0;

	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::VERTEX))
	{
		result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::INDEX))
	{
		result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::UNIFORM))
	{
		result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	}
	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::STORAGE))
	{
		result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}

	// Always add transfer bits for staging operations
	result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

	return result;
}

}        // namespace RHI