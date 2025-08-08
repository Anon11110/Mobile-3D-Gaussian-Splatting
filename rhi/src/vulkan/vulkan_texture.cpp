#include <stdexcept>

#include "vulkan_backend.h"

namespace RHI {

VulkanTexture::VulkanTexture(VkDevice device, VkImage image, VkFormat format, uint32_t width, uint32_t height,
                             bool ownedBySwapchain)
    : device(device), image(image), imageView(VK_NULL_HANDLE), memory(VK_NULL_HANDLE), width(width), height(height),
      format(VulkanFormatToTexture(format)), ownedBySwapchain(ownedBySwapchain) {
    // Create image view
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;

    // Default component mapping
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

    // Subresource range
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &createInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan image view");
    }
}

VulkanTexture::~VulkanTexture() {
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    // Only destroy image and memory if not owned by swapchain
    if (!ownedBySwapchain) {
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
        if (image != VK_NULL_HANDLE) {
            vkDestroyImage(device, image, nullptr);
        }
    }
}

// Utility function implementations
VkFormat TextureFormatToVulkan(TextureFormat format) {
    switch (format) {
        case TextureFormat::R8G8B8A8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case TextureFormat::R8G8B8A8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::B8G8R8A8_UNORM:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case TextureFormat::B8G8R8A8_SRGB:
            return VK_FORMAT_B8G8R8A8_SRGB;
        case TextureFormat::R32G32B32_FLOAT:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case TextureFormat::D32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case TextureFormat::D24_UNORM_S8_UINT:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

TextureFormat VulkanFormatToTexture(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
            return TextureFormat::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB:
            return TextureFormat::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM:
            return TextureFormat::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB:
            return TextureFormat::B8G8R8A8_SRGB;
        case VK_FORMAT_R32G32B32_SFLOAT:
            return TextureFormat::R32G32B32_FLOAT;
        case VK_FORMAT_D32_SFLOAT:
            return TextureFormat::D32_FLOAT;
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return TextureFormat::D24_UNORM_S8_UINT;
        default:
            return TextureFormat::UNDEFINED;
    }
}

}  // namespace RHI