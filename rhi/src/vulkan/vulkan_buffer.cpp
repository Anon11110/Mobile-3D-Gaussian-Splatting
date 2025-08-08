#include <cstring>
#include <stdexcept>

#include "vulkan_backend.h"

namespace RHI {

VulkanBuffer::VulkanBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const BufferDesc& desc)
    : device(device), buffer(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), size(desc.size), mappedData(nullptr) {
    // Create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.size;
    bufferInfo.usage = BufferUsageToVulkan(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan buffer");
    }

    // Get memory requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    // Allocate memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;

    // Determine memory properties based on memory type
    VkMemoryPropertyFlags properties = 0;
    switch (desc.memoryType) {
        case MemoryType::GPU_ONLY:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;
        case MemoryType::CPU_TO_GPU:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;
        case MemoryType::GPU_TO_CPU:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;
    }

    allocInfo.memoryTypeIndex = FindMemoryType(physicalDevice, memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(device, buffer, nullptr);
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    // Bind buffer to memory
    if (vkBindBufferMemory(device, buffer, memory, 0) != VK_SUCCESS) {
        vkFreeMemory(device, memory, nullptr);
        vkDestroyBuffer(device, buffer, nullptr);
        throw std::runtime_error("Failed to bind buffer memory");
    }

    // Upload initial data if provided
    if (desc.initialData != nullptr) {
        void* data = Map();
        memcpy(data, desc.initialData, desc.size);
        Unmap();
    }
}

VulkanBuffer::~VulkanBuffer() {
    if (mappedData != nullptr) {
        Unmap();
    }

    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, memory, nullptr);
    }

    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, buffer, nullptr);
    }
}

void* VulkanBuffer::Map() {
    if (mappedData != nullptr) {
        return mappedData;
    }

    if (vkMapMemory(device, memory, 0, size, 0, &mappedData) != VK_SUCCESS) {
        throw std::runtime_error("Failed to map buffer memory");
    }

    return mappedData;
}

void VulkanBuffer::Unmap() {
    if (mappedData != nullptr) {
        vkUnmapMemory(device, memory);
        mappedData = nullptr;
    }
}

size_t VulkanBuffer::GetSize() const {
    return size;
}

// Utility function implementations
VkBufferUsageFlags BufferUsageToVulkan(BufferUsage usage) {
    VkBufferUsageFlags result = 0;

    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::VERTEX)) {
        result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::INDEX)) {
        result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::UNIFORM)) {
        result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::STORAGE)) {
        result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }

    return result;
}

uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

}  // namespace RHI