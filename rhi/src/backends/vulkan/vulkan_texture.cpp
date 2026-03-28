#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

namespace
{
VkImageType GetVkImageType(TextureType type)
{
	switch (type)
	{
		case TextureType::TEXTURE_3D:
			return VK_IMAGE_TYPE_3D;
		default:
			return VK_IMAGE_TYPE_2D;
	}
}

VkImageViewType GetVkImageViewType(TextureType type, uint32_t arrayLayers, bool isCubeMap)
{
	if (isCubeMap || type == TextureType::TEXTURE_CUBE)
	{
		return (arrayLayers > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
	}

	switch (type)
	{
		case TextureType::TEXTURE_2D_ARRAY:
			return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		case TextureType::TEXTURE_3D:
			return VK_IMAGE_VIEW_TYPE_3D;
		case TextureType::TEXTURE_2D:
		default:
			return (arrayLayers > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
	}
}

VkImageCreateFlags GetImageCreateFlags(TextureType type, bool isCubeMap)
{
	if (isCubeMap || type == TextureType::TEXTURE_CUBE)
	{
		return VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}
	return 0;
}
}        // namespace

VulkanTexture::VulkanTexture(VkDevice device, VmaAllocator allocator, VkImage image, VkFormat format, uint32_t width,
                             uint32_t height, bool ownedBySwapchain) :
    device(device), allocator(allocator), image(image), imageView(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), width(width), height(height), depth(1), mipLevels(1), arrayLayers(1), format(VulkanFormatToTexture(format)), type(TextureType::TEXTURE_2D), ownedBySwapchain(ownedBySwapchain)
{
	// Create image view
	VkImageViewCreateInfo createInfo{};
	createInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createInfo.image    = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format   = format;

	// Default component mapping
	createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	// Subresource range
	createInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
	createInfo.subresourceRange.baseMipLevel   = 0;
	createInfo.subresourceRange.levelCount     = 1;
	createInfo.subresourceRange.baseArrayLayer = 0;
	createInfo.subresourceRange.layerCount     = 1;

	if (vkCreateImageView(device, &createInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan image view");
	}
}

VulkanTexture::VulkanTexture(VkDevice device, VmaAllocator allocator, const TextureDesc &desc) :
    device(device), allocator(allocator), image(VK_NULL_HANDLE), imageView(VK_NULL_HANDLE), allocation(VK_NULL_HANDLE), width(desc.width), height(desc.height), depth(desc.depth), mipLevels(desc.mipLevels), arrayLayers(desc.arrayLayers), format(desc.format), type(desc.type), ownedBySwapchain(false)
{
	// Create image info
	VkImageCreateInfo imageInfo{};
	imageInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType     = GetVkImageType(desc.type);
	imageInfo.flags         = GetImageCreateFlags(desc.type, desc.isCubeMap);
	imageInfo.extent.width  = desc.width;
	imageInfo.extent.height = desc.height;
	imageInfo.extent.depth  = desc.depth;
	imageInfo.mipLevels     = desc.mipLevels;
	imageInfo.arrayLayers   = desc.arrayLayers;
	imageInfo.format        = TextureFormatToVulkan(desc.format);
	imageInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.samples       = VK_SAMPLE_COUNT_1_BIT;

	// Set usage flags
	imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	if (desc.isRenderTarget)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if (desc.isDepthStencil)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if (desc.isStorageImage)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (desc.isInputAttachment)
	{
		imageInfo.usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	VmaAllocationCreateInfo allocInfo{};
	switch (desc.resourceUsage)
	{
		case ResourceUsage::Static:
			// Static textures - prefer device local memory
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;

		case ResourceUsage::DynamicUpload:
			// Streaming textures - need host visibility
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			if (desc.hints.persistently_mapped)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			break;

		case ResourceUsage::Readback:
			// Readback textures - need host visibility
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			if (desc.hints.persistently_mapped)
			{
				allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
			}
			break;

		case ResourceUsage::Transient:
			// Transient render targets - prefer device local, allow aliasing
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			allocInfo.flags = VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
			if (desc.isRenderTarget || desc.isDepthStencil)
			{
				// Transient attachments can use lazily allocated memory on some GPUs
				allocInfo.preferredFlags = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;
			}
			break;
	}

	// Apply additional hints
	if (!desc.hints.prefer_device_local && desc.resourceUsage == ResourceUsage::Static)
	{
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
	}

	VmaAllocationInfo allocationInfo;
	VkResult          result = vmaCreateImage(allocator, &imageInfo, &allocInfo, &image, &allocation, &allocationInfo);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan image with VMA");
	}

	// Create image view
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType        = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image        = image;
	viewInfo.viewType     = GetVkImageViewType(desc.type, desc.arrayLayers, desc.isCubeMap);
	viewInfo.format       = TextureFormatToVulkan(desc.format);
	viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

	// Set aspect mask based on format
	if (desc.isDepthStencil)
	{
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (desc.format == TextureFormat::D24_UNORM_S8_UINT || desc.format == TextureFormat::D32_SFLOAT_S8_UINT)
		{
			viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else
	{
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	}

	viewInfo.subresourceRange.baseMipLevel   = 0;
	viewInfo.subresourceRange.levelCount     = desc.mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount     = desc.arrayLayers;

	if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		vmaDestroyImage(allocator, image, allocation);
		throw std::runtime_error("Failed to create Vulkan image view");
	}

	// Note: Initial data upload for Static textures should be handled at a higher level
	// using staging buffers and proper image layout transitions for optimal performance
}

VulkanTexture::~VulkanTexture()
{
	if (imageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, imageView, nullptr);
		imageView = VK_NULL_HANDLE;
	}

	// Only destroy image and allocation if not owned by swapchain
	if (!ownedBySwapchain)
	{
		if (image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE)
		{
			vmaDestroyImage(allocator, image, allocation);
			image      = VK_NULL_HANDLE;
			allocation = VK_NULL_HANDLE;
		}
	}
}

VulkanTexture::VulkanTexture(VulkanTexture &&other) noexcept :
    device(other.device),
    allocator(other.allocator),
    image(other.image),
    imageView(other.imageView),
    allocation(other.allocation),
    width(other.width),
    height(other.height),
    depth(other.depth),
    mipLevels(other.mipLevels),
    arrayLayers(other.arrayLayers),
    format(other.format),
    type(other.type),
    ownedBySwapchain(other.ownedBySwapchain)
{
	other.device           = VK_NULL_HANDLE;
	other.allocator        = VK_NULL_HANDLE;
	other.image            = VK_NULL_HANDLE;
	other.imageView        = VK_NULL_HANDLE;
	other.allocation       = VK_NULL_HANDLE;
	other.width            = 0;
	other.height           = 0;
	other.depth            = 0;
	other.mipLevels        = 0;
	other.arrayLayers      = 0;
	other.format           = TextureFormat::UNDEFINED;
	other.type             = TextureType::TEXTURE_2D;
	other.ownedBySwapchain = false;
}

VulkanTexture &VulkanTexture::operator=(VulkanTexture &&other) noexcept
{
	if (this != &other)
	{
		if (imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, imageView, nullptr);
		}
		if (!ownedBySwapchain && image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE && allocator != VK_NULL_HANDLE)
		{
			vmaDestroyImage(allocator, image, allocation);
		}

		device           = other.device;
		allocator        = other.allocator;
		image            = other.image;
		imageView        = other.imageView;
		allocation       = other.allocation;
		width            = other.width;
		height           = other.height;
		depth            = other.depth;
		mipLevels        = other.mipLevels;
		arrayLayers      = other.arrayLayers;
		format           = other.format;
		type             = other.type;
		ownedBySwapchain = other.ownedBySwapchain;

		other.device           = VK_NULL_HANDLE;
		other.allocator        = VK_NULL_HANDLE;
		other.image            = VK_NULL_HANDLE;
		other.imageView        = VK_NULL_HANDLE;
		other.allocation       = VK_NULL_HANDLE;
		other.width            = 0;
		other.height           = 0;
		other.depth            = 0;
		other.mipLevels        = 0;
		other.arrayLayers      = 0;
		other.format           = TextureFormat::UNDEFINED;
		other.type             = TextureType::TEXTURE_2D;
		other.ownedBySwapchain = false;
	}
	return *this;
}

}        // namespace rhi::vulkan
