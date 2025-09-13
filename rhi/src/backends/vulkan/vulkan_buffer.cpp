#include <cstring>
#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanBuffer::VulkanBuffer(VmaAllocator allocator, const BufferDesc &desc) :
    allocator(allocator), buffer(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), size(desc.size), mappedData(nullptr), isPersistentlyMapped(false), indexType(desc.indexType)
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
				isPersistentlyMapped = true;
			}
			break;

		case ResourceUsage::Readback:
			// GPU writes, CPU reads - needs host visibility
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			if (desc.hints.persistently_mapped)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
				isPersistentlyMapped = true;
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
		// Unmap memory if it was manually mapped
		Unmap();

		vmaDestroyBuffer(allocator, buffer, allocation);
		buffer     = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
	}
}

VulkanBuffer::VulkanBuffer(VulkanBuffer &&other) noexcept :
    allocator(other.allocator),
    buffer(other.buffer),
    allocation(other.allocation),
    size(other.size),
    mappedData(other.mappedData),
    isPersistentlyMapped(other.isPersistentlyMapped),
    indexType(other.indexType)
{
	other.allocator            = VK_NULL_HANDLE;
	other.buffer               = VK_NULL_HANDLE;
	other.allocation           = VK_NULL_HANDLE;
	other.size                 = 0;
	other.mappedData           = nullptr;
	other.isPersistentlyMapped = false;
}

VulkanBuffer &VulkanBuffer::operator=(VulkanBuffer &&other) noexcept
{
	if (this != &other)
	{
		if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE)
		{
			vmaDestroyBuffer(allocator, buffer, allocation);
		}

		allocator            = other.allocator;
		buffer               = other.buffer;
		allocation           = other.allocation;
		size                 = other.size;
		mappedData           = other.mappedData;
		isPersistentlyMapped = other.isPersistentlyMapped;
		indexType            = other.indexType;

		other.allocator            = VK_NULL_HANDLE;
		other.buffer               = VK_NULL_HANDLE;
		other.allocation           = VK_NULL_HANDLE;
		other.size                 = 0;
		other.mappedData           = nullptr;
		other.isPersistentlyMapped = false;
	}
	return *this;
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
	// If the buffer has a mapped pointer AND it was NOT persistently mapped
	if (mappedData != nullptr && !isPersistentlyMapped)
	{
		vmaUnmapMemory(allocator, allocation);
		mappedData = nullptr;
	}
}

size_t VulkanBuffer::GetSize() const
{
	return size;
}

bool VulkanBuffer::IsMappable() const
{
	// Check if the buffer was allocated with host-accessible memory
	VmaAllocationInfo allocInfo;
	vmaGetAllocationInfo(allocator, allocation, &allocInfo);

	VkMemoryPropertyFlags memProps;
	vmaGetMemoryTypeProperties(allocator, allocInfo.memoryType, &memProps);

	return (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

}        // namespace rhi::vulkan