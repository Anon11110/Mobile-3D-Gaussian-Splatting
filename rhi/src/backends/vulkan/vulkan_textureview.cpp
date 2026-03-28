#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanTextureView::VulkanTextureView(VkDevice device, const TextureViewDesc &desc) :
    device(device), imageView(VK_NULL_HANDLE), texture(static_cast<VulkanTexture *>(desc.texture))
{
	if (!texture)
	{
		throw std::runtime_error("TextureView requires a valid texture");
	}

	// Use texture format if view format is undefined
	format = (desc.format == TextureFormat::UNDEFINED) ? texture->GetFormat() : desc.format;

	baseMipLevel    = desc.baseMipLevel;
	mipLevelCount   = desc.mipLevelCount;
	baseArrayLayer  = desc.baseArrayLayer;
	arrayLayerCount = desc.arrayLayerCount;

	// Convert aspect mask to Vulkan
	VkImageAspectFlags aspectMask = 0;
	if (static_cast<uint32_t>(desc.aspectMask) & static_cast<uint32_t>(TextureAspect::COLOR))
		aspectMask |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (static_cast<uint32_t>(desc.aspectMask) & static_cast<uint32_t>(TextureAspect::DEPTH))
		aspectMask |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (static_cast<uint32_t>(desc.aspectMask) & static_cast<uint32_t>(TextureAspect::STENCIL))
		aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

	// Derive view type from texture type
	VkImageViewType vkViewType;
	switch (texture->GetType())
	{
		case TextureType::TEXTURE_CUBE:
			vkViewType = (desc.arrayLayerCount > 6) ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case TextureType::TEXTURE_2D_ARRAY:
			vkViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case TextureType::TEXTURE_3D:
			vkViewType = VK_IMAGE_VIEW_TYPE_3D;
			break;
		case TextureType::TEXTURE_2D:
		default:
			vkViewType = (desc.arrayLayerCount > 1) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
			break;
	}

	// Create image view
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image                           = texture->GetHandle();
	viewInfo.viewType                        = vkViewType;
	viewInfo.format                          = TextureFormatToVulkan(format);
	viewInfo.subresourceRange.aspectMask     = aspectMask;
	viewInfo.subresourceRange.baseMipLevel   = baseMipLevel;
	viewInfo.subresourceRange.levelCount     = mipLevelCount;
	viewInfo.subresourceRange.baseArrayLayer = baseArrayLayer;
	viewInfo.subresourceRange.layerCount     = arrayLayerCount;

	if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create image view");
	}
}

VulkanTextureView::~VulkanTextureView()
{
	if (imageView != VK_NULL_HANDLE)
	{
		vkDestroyImageView(device, imageView, nullptr);
	}
}

VulkanTextureView::VulkanTextureView(VulkanTextureView &&other) noexcept :
    device(other.device),
    imageView(other.imageView),
    texture(other.texture),
    format(other.format),
    baseMipLevel(other.baseMipLevel),
    mipLevelCount(other.mipLevelCount),
    baseArrayLayer(other.baseArrayLayer),
    arrayLayerCount(other.arrayLayerCount)
{
	other.device          = VK_NULL_HANDLE;
	other.imageView       = VK_NULL_HANDLE;
	other.texture         = nullptr;
	other.format          = TextureFormat::UNDEFINED;
	other.baseMipLevel    = 0;
	other.mipLevelCount   = 0;
	other.baseArrayLayer  = 0;
	other.arrayLayerCount = 0;
}

VulkanTextureView &VulkanTextureView::operator=(VulkanTextureView &&other) noexcept
{
	if (this != &other)
	{
		if (imageView != VK_NULL_HANDLE)
		{
			vkDestroyImageView(device, imageView, nullptr);
		}

		device          = other.device;
		imageView       = other.imageView;
		texture         = other.texture;
		format          = other.format;
		baseMipLevel    = other.baseMipLevel;
		mipLevelCount   = other.mipLevelCount;
		baseArrayLayer  = other.baseArrayLayer;
		arrayLayerCount = other.arrayLayerCount;

		other.device          = VK_NULL_HANDLE;
		other.imageView       = VK_NULL_HANDLE;
		other.texture         = nullptr;
		other.format          = TextureFormat::UNDEFINED;
		other.baseMipLevel    = 0;
		other.mipLevelCount   = 0;
		other.baseArrayLayer  = 0;
		other.arrayLayerCount = 0;
	}
	return *this;
}

uint32_t VulkanTextureView::GetWidth() const
{
	return texture->GetWidth() >> baseMipLevel;
}

uint32_t VulkanTextureView::GetHeight() const
{
	return texture->GetHeight() >> baseMipLevel;
}

}        // namespace rhi::vulkan