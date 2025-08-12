#include "vulkan_backend.h"
#include <stdexcept>

namespace RHI
{

VkFormat TextureFormatToVulkan(TextureFormat format)
{
	switch (format)
	{
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

VkFormat VertexFormatToVulkan(VertexFormat format)
{
	switch (format)
	{
		case VertexFormat::R32_SFLOAT:
			return VK_FORMAT_R32_SFLOAT;
		case VertexFormat::R32G32_SFLOAT:
			return VK_FORMAT_R32G32_SFLOAT;
		case VertexFormat::R32G32B32_SFLOAT:
			return VK_FORMAT_R32G32B32_SFLOAT;
		case VertexFormat::R32G32B32A32_SFLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case VertexFormat::R16_SFLOAT:
			return VK_FORMAT_R16_SFLOAT;
		case VertexFormat::R16G16_SFLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
		case VertexFormat::R16G16B16_SFLOAT:
			return VK_FORMAT_R16G16B16_SFLOAT;
		case VertexFormat::R16G16B16A16_SFLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case VertexFormat::R8_UNORM:
			return VK_FORMAT_R8_UNORM;
		case VertexFormat::R8G8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
		case VertexFormat::R8G8B8_UNORM:
			return VK_FORMAT_R8G8B8_UNORM;
		case VertexFormat::R8G8B8A8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case VertexFormat::R8_UINT:
			return VK_FORMAT_R8_UINT;
		case VertexFormat::R8G8_UINT:
			return VK_FORMAT_R8G8_UINT;
		case VertexFormat::R8G8B8_UINT:
			return VK_FORMAT_R8G8B8_UINT;
		case VertexFormat::R8G8B8A32_UINT:
			return VK_FORMAT_R8G8B8A8_UINT;
		case VertexFormat::R16_UINT:
			return VK_FORMAT_R16_UINT;
		case VertexFormat::R16G16_UINT:
			return VK_FORMAT_R16G16_UINT;
		case VertexFormat::R16G16B16_UINT:
			return VK_FORMAT_R16G16B16_UINT;
		case VertexFormat::R16G16B16A16_UINT:
			return VK_FORMAT_R16G16B16A16_UINT;
		case VertexFormat::R32_UINT:
			return VK_FORMAT_R32_UINT;
		case VertexFormat::R32G32_UINT:
			return VK_FORMAT_R32G32_UINT;
		case VertexFormat::R32G32B32_UINT:
			return VK_FORMAT_R32G32B32_UINT;
		case VertexFormat::R32G32B32A32_UINT:
			return VK_FORMAT_R32G32B32A32_UINT;
		default:
			return VK_FORMAT_UNDEFINED;
	}
}

TextureFormat VulkanFormatToTexture(VkFormat format)
{
	switch (format)
	{
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

VkDescriptorType DescriptorTypeToVulkan(DescriptorType type)
{
	switch (type)
	{
		case DescriptorType::UNIFORM_BUFFER:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DescriptorType::STORAGE_BUFFER:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		case DescriptorType::SAMPLER:
			return VK_DESCRIPTOR_TYPE_SAMPLER;
		case DescriptorType::TEXTURE:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DescriptorType::COMBINED_IMAGE_SAMPLER:
			return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		default:
			throw std::runtime_error("Unknown descriptor type");
	}
}

VkShaderStageFlags ShaderStageFlagsToVulkan(ShaderStageFlags flags)
{
	VkShaderStageFlags vkFlags = 0;
	if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::VERTEX))
	{
		vkFlags |= VK_SHADER_STAGE_VERTEX_BIT;
	}
	if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::FRAGMENT))
	{
		vkFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::COMPUTE))
	{
		vkFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
	}
	return vkFlags;
}

VkImageLayout ImageLayoutToVulkan(ImageLayout layout)
{
	switch (layout)
	{
		case ImageLayout::UNDEFINED:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		case ImageLayout::GENERAL:
			return VK_IMAGE_LAYOUT_GENERAL;
		case ImageLayout::COLOR_ATTACHMENT:
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case ImageLayout::DEPTH_STENCIL_ATTACHMENT:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case ImageLayout::DEPTH_STENCIL_READ_ONLY:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		case ImageLayout::SHADER_READ_ONLY:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case ImageLayout::TRANSFER_SRC:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ImageLayout::TRANSFER_DST:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case ImageLayout::PRESENT_SRC:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		default:
			throw std::runtime_error("Unknown image layout");
	}
}

}        // namespace RHI