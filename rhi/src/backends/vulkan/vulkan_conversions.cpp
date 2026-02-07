#include "vulkan_backend.h"
#include <stdexcept>

namespace rhi::vulkan
{

VkFormat TextureFormatToVulkan(TextureFormat format)
{
	switch (format)
	{
		// Standard RGBA formats
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

		// Depth/stencil formats
		case TextureFormat::D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
		case TextureFormat::D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;

		// Single channel formats
		case TextureFormat::R8_UNORM:
			return VK_FORMAT_R8_UNORM;
		case TextureFormat::R16_FLOAT:
			return VK_FORMAT_R16_SFLOAT;
		case TextureFormat::R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;

		// Dual channel formats
		case TextureFormat::RG8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
		case TextureFormat::RG16_FLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
		case TextureFormat::RG32_FLOAT:
			return VK_FORMAT_R32G32_SFLOAT;

		// HDR formats
		case TextureFormat::RGBA16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case TextureFormat::RGBA32_FLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
		case TextureFormat::R11G11B10_FLOAT:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32;

		// Compressed formats
		case TextureFormat::ASTC_4x4_UNORM:
			return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
		case TextureFormat::ASTC_4x4_SRGB:
			return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
		case TextureFormat::ASTC_6x6_UNORM:
			return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
		case TextureFormat::ASTC_6x6_SRGB:
			return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;

		case TextureFormat::ETC2_RGB8_UNORM:
			return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
		case TextureFormat::ETC2_RGB8_SRGB:
			return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
		case TextureFormat::ETC2_RGBA8_UNORM:
			return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
		case TextureFormat::ETC2_RGBA8_SRGB:
			return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;

		case TextureFormat::BC1_RGB_UNORM:
			return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
		case TextureFormat::BC1_RGB_SRGB:
			return VK_FORMAT_BC1_RGB_SRGB_BLOCK;
		case TextureFormat::BC3_RGBA_UNORM:
			return VK_FORMAT_BC3_UNORM_BLOCK;
		case TextureFormat::BC3_RGBA_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
		case TextureFormat::BC5_RG_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case TextureFormat::BC7_RGBA_UNORM:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		case TextureFormat::BC7_RGBA_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;

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
		// Standard RGBA formats
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

		// Depth/stencil formats
		case VK_FORMAT_D32_SFLOAT:
			return TextureFormat::D32_FLOAT;
		case VK_FORMAT_D24_UNORM_S8_UINT:
			return TextureFormat::D24_UNORM_S8_UINT;

		// Single channel formats
		case VK_FORMAT_R8_UNORM:
			return TextureFormat::R8_UNORM;
		case VK_FORMAT_R16_SFLOAT:
			return TextureFormat::R16_FLOAT;
		case VK_FORMAT_R32_SFLOAT:
			return TextureFormat::R32_FLOAT;

		// Dual channel formats
		case VK_FORMAT_R8G8_UNORM:
			return TextureFormat::RG8_UNORM;
		case VK_FORMAT_R16G16_SFLOAT:
			return TextureFormat::RG16_FLOAT;
		case VK_FORMAT_R32G32_SFLOAT:
			return TextureFormat::RG32_FLOAT;

		// HDR formats
		case VK_FORMAT_R16G16B16A16_SFLOAT:
			return TextureFormat::RGBA16_FLOAT;
		case VK_FORMAT_R32G32B32A32_SFLOAT:
			return TextureFormat::RGBA32_FLOAT;
		case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
			return TextureFormat::R11G11B10_FLOAT;

		// Compressed formats
		case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:
			return TextureFormat::ASTC_4x4_UNORM;
		case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:
			return TextureFormat::ASTC_4x4_SRGB;
		case VK_FORMAT_ASTC_6x6_UNORM_BLOCK:
			return TextureFormat::ASTC_6x6_UNORM;
		case VK_FORMAT_ASTC_6x6_SRGB_BLOCK:
			return TextureFormat::ASTC_6x6_SRGB;

		case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:
			return TextureFormat::ETC2_RGB8_UNORM;
		case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
			return TextureFormat::ETC2_RGB8_SRGB;
		case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK:
			return TextureFormat::ETC2_RGBA8_UNORM;
		case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
			return TextureFormat::ETC2_RGBA8_SRGB;

		case VK_FORMAT_BC1_RGB_UNORM_BLOCK:
			return TextureFormat::BC1_RGB_UNORM;
		case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
			return TextureFormat::BC1_RGB_SRGB;
		case VK_FORMAT_BC3_UNORM_BLOCK:
			return TextureFormat::BC3_RGBA_UNORM;
		case VK_FORMAT_BC3_SRGB_BLOCK:
			return TextureFormat::BC3_RGBA_SRGB;
		case VK_FORMAT_BC5_UNORM_BLOCK:
			return TextureFormat::BC5_RG_UNORM;
		case VK_FORMAT_BC7_UNORM_BLOCK:
			return TextureFormat::BC7_RGBA_UNORM;
		case VK_FORMAT_BC7_SRGB_BLOCK:
			return TextureFormat::BC7_RGBA_SRGB;

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
	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::TRANSFER_DST))
	{
		result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if (static_cast<uint32_t>(usage) & static_cast<uint32_t>(BufferUsage::TRANSFER_SRC))
	{
		result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}

	return result;
}

VkDescriptorType DescriptorTypeToVulkan(DescriptorType type)
{
	switch (type)
	{
		// Buffers
		case DescriptorType::UNIFORM_BUFFER:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		case DescriptorType::STORAGE_BUFFER:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

		// Dynamic buffers
		case DescriptorType::UNIFORM_BUFFER_DYNAMIC:
			return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
		case DescriptorType::STORAGE_BUFFER_DYNAMIC:
			return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;

		// Texel buffer views
		case DescriptorType::UNIFORM_TEXEL_BUFFER:
			return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
		case DescriptorType::STORAGE_TEXEL_BUFFER:
			return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;

		// Textures
		case DescriptorType::SAMPLED_TEXTURE:
			return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		case DescriptorType::STORAGE_TEXTURE:
			return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

		// Samplers
		case DescriptorType::SAMPLER:
			return VK_DESCRIPTOR_TYPE_SAMPLER;

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

VkPrimitiveTopology PrimitiveTopologyToVulkan(PrimitiveTopology topology)
{
	switch (topology)
	{
		case PrimitiveTopology::POINT_LIST:
			return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case PrimitiveTopology::LINE_LIST:
			return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case PrimitiveTopology::LINE_STRIP:
			return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
		case PrimitiveTopology::TRIANGLE_LIST:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		case PrimitiveTopology::TRIANGLE_STRIP:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		default:
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

VkPolygonMode PolygonModeToVulkan(PolygonMode mode)
{
	switch (mode)
	{
		case PolygonMode::FILL:
			return VK_POLYGON_MODE_FILL;
		case PolygonMode::LINE:
			return VK_POLYGON_MODE_LINE;
		case PolygonMode::POINT:
			return VK_POLYGON_MODE_POINT;
		default:
			return VK_POLYGON_MODE_FILL;
	}
}

VkCullModeFlags CullModeToVulkan(CullMode mode)
{
	switch (mode)
	{
		case CullMode::NONE:
			return VK_CULL_MODE_NONE;
		case CullMode::FRONT:
			return VK_CULL_MODE_FRONT_BIT;
		case CullMode::BACK:
			return VK_CULL_MODE_BACK_BIT;
		default:
			return VK_CULL_MODE_BACK_BIT;
	}
}

VkFrontFace FrontFaceToVulkan(FrontFace face)
{
	switch (face)
	{
		case FrontFace::COUNTER_CLOCKWISE:
			return VK_FRONT_FACE_COUNTER_CLOCKWISE;
		case FrontFace::CLOCKWISE:
			return VK_FRONT_FACE_CLOCKWISE;
		default:
			return VK_FRONT_FACE_CLOCKWISE;
	}
}

VkCompareOp CompareOpToVulkan(CompareOp op)
{
	switch (op)
	{
		case CompareOp::NEVER:
			return VK_COMPARE_OP_NEVER;
		case CompareOp::LESS:
			return VK_COMPARE_OP_LESS;
		case CompareOp::EQUAL:
			return VK_COMPARE_OP_EQUAL;
		case CompareOp::LESS_OR_EQUAL:
			return VK_COMPARE_OP_LESS_OR_EQUAL;
		case CompareOp::GREATER:
			return VK_COMPARE_OP_GREATER;
		case CompareOp::NOT_EQUAL:
			return VK_COMPARE_OP_NOT_EQUAL;
		case CompareOp::GREATER_OR_EQUAL:
			return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case CompareOp::ALWAYS:
			return VK_COMPARE_OP_ALWAYS;
		default:
			return VK_COMPARE_OP_ALWAYS;
	}
}

VkStencilOp StencilOpToVulkan(StencilOp op)
{
	switch (op)
	{
		case StencilOp::KEEP:
			return VK_STENCIL_OP_KEEP;
		case StencilOp::ZERO:
			return VK_STENCIL_OP_ZERO;
		case StencilOp::REPLACE:
			return VK_STENCIL_OP_REPLACE;
		case StencilOp::INCREMENT_AND_CLAMP:
			return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
		case StencilOp::DECREMENT_AND_CLAMP:
			return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
		case StencilOp::INVERT:
			return VK_STENCIL_OP_INVERT;
		case StencilOp::INCREMENT_AND_WRAP:
			return VK_STENCIL_OP_INCREMENT_AND_WRAP;
		case StencilOp::DECREMENT_AND_WRAP:
			return VK_STENCIL_OP_DECREMENT_AND_WRAP;
		default:
			return VK_STENCIL_OP_KEEP;
	}
}

VkBlendFactor BlendFactorToVulkan(BlendFactor factor)
{
	switch (factor)
	{
		case BlendFactor::ZERO:
			return VK_BLEND_FACTOR_ZERO;
		case BlendFactor::ONE:
			return VK_BLEND_FACTOR_ONE;
		case BlendFactor::SRC_COLOR:
			return VK_BLEND_FACTOR_SRC_COLOR;
		case BlendFactor::ONE_MINUS_SRC_COLOR:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		case BlendFactor::DST_COLOR:
			return VK_BLEND_FACTOR_DST_COLOR;
		case BlendFactor::ONE_MINUS_DST_COLOR:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
		case BlendFactor::SRC_ALPHA:
			return VK_BLEND_FACTOR_SRC_ALPHA;
		case BlendFactor::ONE_MINUS_SRC_ALPHA:
			return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		case BlendFactor::DST_ALPHA:
			return VK_BLEND_FACTOR_DST_ALPHA;
		case BlendFactor::ONE_MINUS_DST_ALPHA:
			return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		case BlendFactor::CONSTANT_COLOR:
			return VK_BLEND_FACTOR_CONSTANT_COLOR;
		case BlendFactor::ONE_MINUS_CONSTANT_COLOR:
			return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
		case BlendFactor::CONSTANT_ALPHA:
			return VK_BLEND_FACTOR_CONSTANT_ALPHA;
		case BlendFactor::ONE_MINUS_CONSTANT_ALPHA:
			return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
		case BlendFactor::SRC_ALPHA_SATURATE:
			return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
		default:
			return VK_BLEND_FACTOR_ONE;
	}
}

VkBlendOp BlendOpToVulkan(BlendOp op)
{
	switch (op)
	{
		case BlendOp::ADD:
			return VK_BLEND_OP_ADD;
		case BlendOp::SUBTRACT:
			return VK_BLEND_OP_SUBTRACT;
		case BlendOp::REVERSE_SUBTRACT:
			return VK_BLEND_OP_REVERSE_SUBTRACT;
		case BlendOp::MIN:
			return VK_BLEND_OP_MIN;
		case BlendOp::MAX:
			return VK_BLEND_OP_MAX;
		default:
			return VK_BLEND_OP_ADD;
	}
}

VkSampleCountFlagBits SampleCountToVulkan(SampleCount count)
{
	switch (count)
	{
		case SampleCount::COUNT_1:
			return VK_SAMPLE_COUNT_1_BIT;
		case SampleCount::COUNT_2:
			return VK_SAMPLE_COUNT_2_BIT;
		case SampleCount::COUNT_4:
			return VK_SAMPLE_COUNT_4_BIT;
		case SampleCount::COUNT_8:
			return VK_SAMPLE_COUNT_8_BIT;
		case SampleCount::COUNT_16:
			return VK_SAMPLE_COUNT_16_BIT;
		case SampleCount::COUNT_32:
			return VK_SAMPLE_COUNT_32_BIT;
		case SampleCount::COUNT_64:
			return VK_SAMPLE_COUNT_64_BIT;
		default:
			return VK_SAMPLE_COUNT_1_BIT;
	}
}

VkAttachmentLoadOp LoadOpToVulkan(LoadOp op)
{
	switch (op)
	{
		case LoadOp::LOAD:
			return VK_ATTACHMENT_LOAD_OP_LOAD;
		case LoadOp::CLEAR:
			return VK_ATTACHMENT_LOAD_OP_CLEAR;
		case LoadOp::DONT_CARE:
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		default:
			return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
}

VkAttachmentStoreOp StoreOpToVulkan(StoreOp op)
{
	switch (op)
	{
		case StoreOp::STORE:
			return VK_ATTACHMENT_STORE_OP_STORE;
		case StoreOp::DONT_CARE:
			return VK_ATTACHMENT_STORE_OP_DONT_CARE;
		default:
			return VK_ATTACHMENT_STORE_OP_STORE;
	}
}

VkPipelineStageFlags PipelineScopeToVulkanStages(PipelineScope scope)
{
	switch (scope)
	{
		case PipelineScope::Graphics:
			return VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
		case PipelineScope::Compute:
			return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		case PipelineScope::Copy:
			return VK_PIPELINE_STAGE_TRANSFER_BIT;
		case PipelineScope::All:
		default:
			return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
}

VkPipelineStageFlags StageMaskToVulkan(StageMask mask)
{
	if (mask == StageMask::Auto)
		return 0;

	VkPipelineStageFlags flags = 0;

	if (static_cast<uint64_t>(mask & StageMask::DrawIndirect))
		flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::VertexInput))
		flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::VertexShader))
		flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::FragmentShader))
		flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::DepthTests))
		flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	if (static_cast<uint64_t>(mask & StageMask::RenderTarget))
		flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::ComputeShader))
		flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::Transfer))
		flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::Host))
		flags |= VK_PIPELINE_STAGE_HOST_BIT;
	if (static_cast<uint64_t>(mask & StageMask::AllGraphics))
		flags |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
	if (static_cast<uint64_t>(mask & StageMask::AllCommands))
		flags |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

	return flags;
}

VkAccessFlags AccessMaskToVulkan(AccessMask mask)
{
	if (mask == AccessMask::Auto)
		return 0;

	VkAccessFlags flags = 0;

	if (static_cast<uint64_t>(mask & AccessMask::IndirectRead))
		flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::IndexRead))
		flags |= VK_ACCESS_INDEX_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::VertexAttributeRead))
		flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::UniformRead))
		flags |= VK_ACCESS_UNIFORM_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::ShaderRead))
		flags |= VK_ACCESS_SHADER_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::ShaderWrite))
		flags |= VK_ACCESS_SHADER_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::RenderTargetRead))
		flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::RenderTargetWrite))
		flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::DepthStencilRead))
		flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::DepthStencilWrite))
		flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::TransferRead))
		flags |= VK_ACCESS_TRANSFER_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::TransferWrite))
		flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::HostRead))
		flags |= VK_ACCESS_HOST_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::HostWrite))
		flags |= VK_ACCESS_HOST_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::MemoryRead))
		flags |= VK_ACCESS_MEMORY_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::MemoryWrite))
		flags |= VK_ACCESS_MEMORY_WRITE_BIT;

	return flags;
}

void GetVulkanStagesAndAccess(ResourceState state, PipelineScope scope,
                              VkPipelineStageFlags &stages, VkAccessFlags &access)
{
	switch (state)
	{
		case ResourceState::Undefined:
			stages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			access = 0;
			break;

		case ResourceState::GeneralRead:
			access = VK_ACCESS_SHADER_READ_BIT;
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}
			else
			{
				// All scope - use all shader stages
				stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			}
			break;

		case ResourceState::CopySource:
			stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
			access = VK_ACCESS_TRANSFER_READ_BIT;
			break;

		case ResourceState::CopyDestination:
			stages = VK_PIPELINE_STAGE_TRANSFER_BIT;
			access = VK_ACCESS_TRANSFER_WRITE_BIT;
			break;

		case ResourceState::ShaderReadWrite:
			stages = (scope == PipelineScope::Compute) ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			access = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			break;

		case ResourceState::ShaderWrite:
			stages = (scope == PipelineScope::Compute) ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			access = VK_ACCESS_SHADER_WRITE_BIT;
			break;

		case ResourceState::RenderTarget:
			stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case ResourceState::DepthStencilRead:
			stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;

		case ResourceState::DepthStencilWrite:
			stages = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			access = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case ResourceState::ResolveSource:
			stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			access = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
			break;

		case ResourceState::ResolveDestination:
			stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case ResourceState::Present:
			stages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			access = 0;
			break;

		case ResourceState::IndirectArgument:
			stages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
			access = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			break;

		case ResourceState::VertexBuffer:
			stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			access = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
			break;

		case ResourceState::IndexBuffer:
			stages = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			access = VK_ACCESS_INDEX_READ_BIT;
			break;

		case ResourceState::UniformBuffer:
			access = VK_ACCESS_UNIFORM_READ_BIT;
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}
			else
			{
				// All scope - use all shader stages
				stages = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
				         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			}
			break;

		default:
			stages = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			access = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			break;
	}
}

VkImageLayout ResourceStateToImageLayout(ResourceState state)
{
	switch (state)
	{
		case ResourceState::Undefined:
			return VK_IMAGE_LAYOUT_UNDEFINED;
		case ResourceState::GeneralRead:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		case ResourceState::CopySource:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ResourceState::CopyDestination:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case ResourceState::ShaderReadWrite:
			return VK_IMAGE_LAYOUT_GENERAL;
		case ResourceState::ShaderWrite:
			return VK_IMAGE_LAYOUT_GENERAL;
		case ResourceState::RenderTarget:
			return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		case ResourceState::DepthStencilRead:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		case ResourceState::DepthStencilWrite:
			return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		case ResourceState::ResolveSource:
			return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		case ResourceState::ResolveDestination:
			return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		case ResourceState::Present:
			return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		default:
			return VK_IMAGE_LAYOUT_GENERAL;
	}
}

VkPipelineStageFlags2 PipelineScopeToVulkanStages2(PipelineScope scope)
{
	switch (scope)
	{
		case PipelineScope::Graphics:
			return VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
		case PipelineScope::Compute:
			return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		case PipelineScope::Copy:
			return VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
		case PipelineScope::All:
		default:
			return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	}
}

VkPipelineStageFlags2 StageMaskToVulkan2(StageMask mask)
{
	if (mask == StageMask::Auto)
		return VK_PIPELINE_STAGE_2_NONE;

	VkPipelineStageFlags2 flags = 0;

	if (static_cast<uint64_t>(mask & StageMask::DrawIndirect))
		flags |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::VertexInput))
		flags |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::VertexShader))
		flags |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::FragmentShader))
		flags |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::DepthTests))
		flags |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
	if (static_cast<uint64_t>(mask & StageMask::RenderTarget))
		flags |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	if (static_cast<uint64_t>(mask & StageMask::ComputeShader))
		flags |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::Transfer))
		flags |= VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
	if (static_cast<uint64_t>(mask & StageMask::Host))
		flags |= VK_PIPELINE_STAGE_2_HOST_BIT;
	if (static_cast<uint64_t>(mask & StageMask::AllGraphics))
		flags |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	if (static_cast<uint64_t>(mask & StageMask::AllCommands))
		flags |= VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	return flags;
}

VkAccessFlags2 AccessMaskToVulkan2(AccessMask mask)
{
	if (mask == AccessMask::Auto)
		return VK_ACCESS_2_NONE;

	VkAccessFlags2 flags = 0;

	if (static_cast<uint64_t>(mask & AccessMask::IndirectRead))
		flags |= VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::IndexRead))
		flags |= VK_ACCESS_2_INDEX_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::VertexAttributeRead))
		flags |= VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::UniformRead))
		flags |= VK_ACCESS_2_UNIFORM_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::ShaderRead))
		flags |= VK_ACCESS_2_SHADER_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::ShaderWrite))
		flags |= VK_ACCESS_2_SHADER_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::RenderTargetRead))
		flags |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::RenderTargetWrite))
		flags |= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::DepthStencilRead))
		flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::DepthStencilWrite))
		flags |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::TransferRead))
		flags |= VK_ACCESS_2_TRANSFER_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::TransferWrite))
		flags |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::HostRead))
		flags |= VK_ACCESS_2_HOST_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::HostWrite))
		flags |= VK_ACCESS_2_HOST_WRITE_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::MemoryRead))
		flags |= VK_ACCESS_2_MEMORY_READ_BIT;
	if (static_cast<uint64_t>(mask & AccessMask::MemoryWrite))
		flags |= VK_ACCESS_2_MEMORY_WRITE_BIT;

	return flags;
}

void GetVulkanStagesAndAccess2(ResourceState state, PipelineScope scope,
                               VkPipelineStageFlags2 &stages, VkAccessFlags2 &access)
{
	switch (state)
	{
		case ResourceState::Undefined:
			stages = VK_PIPELINE_STAGE_2_NONE;
			access = VK_ACCESS_2_NONE;
			break;

		case ResourceState::GeneralRead:
			access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			}
			else
			{
				stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			break;

		case ResourceState::CopySource:
			stages = VK_PIPELINE_STAGE_2_COPY_BIT;
			access = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;

		case ResourceState::CopyDestination:
			stages = VK_PIPELINE_STAGE_2_COPY_BIT;
			access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;

		case ResourceState::ShaderReadWrite:
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			}
			else
			{
				stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			access = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			break;

		case ResourceState::ShaderWrite:
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			}
			else
			{
				stages = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			access = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
			break;

		case ResourceState::RenderTarget:
			stages = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;

		case ResourceState::DepthStencilRead:
			stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
			break;

		case ResourceState::DepthStencilWrite:
			stages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
			access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			break;

		case ResourceState::ResolveSource:
			stages = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
			access = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;

		case ResourceState::ResolveDestination:
			stages = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
			access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;

		case ResourceState::Present:
			stages = VK_PIPELINE_STAGE_2_NONE;
			access = VK_ACCESS_2_NONE;
			break;

		case ResourceState::IndirectArgument:
			stages = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
			access = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
			break;

		case ResourceState::VertexBuffer:
			stages = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
			access = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
			break;

		case ResourceState::IndexBuffer:
			stages = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
			access = VK_ACCESS_2_INDEX_READ_BIT;
			break;

		case ResourceState::UniformBuffer:
			access = VK_ACCESS_2_UNIFORM_READ_BIT;
			if (scope == PipelineScope::Compute)
			{
				stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			else if (scope == PipelineScope::Graphics)
			{
				stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			}
			else
			{
				stages = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
				         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			}
			break;

		default:
			stages = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			access = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
			break;
	}
}

VkFilter FilterModeToVulkan(FilterMode filter)
{
	switch (filter)
	{
		case FilterMode::NEAREST:
			return VK_FILTER_NEAREST;
		case FilterMode::LINEAR:
			return VK_FILTER_LINEAR;
		default:
			return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode MipmapModeToVulkan(MipmapMode mode)
{
	switch (mode)
	{
		case MipmapMode::NEAREST:
			return VK_SAMPLER_MIPMAP_MODE_NEAREST;
		case MipmapMode::LINEAR:
			return VK_SAMPLER_MIPMAP_MODE_LINEAR;
		default:
			return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}

VkSamplerAddressMode SamplerAddressModeToVulkan(SamplerAddressMode mode)
{
	switch (mode)
	{
		case SamplerAddressMode::REPEAT:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
		case SamplerAddressMode::MIRRORED_REPEAT:
			return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
		case SamplerAddressMode::CLAMP_TO_EDGE:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		case SamplerAddressMode::CLAMP_TO_BORDER:
			return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		case SamplerAddressMode::MIRROR_CLAMP_TO_EDGE:
			return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
		default:
			return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VkBorderColor BorderColorToVulkan(BorderColor color)
{
	switch (color)
	{
		case BorderColor::FLOAT_TRANSPARENT_BLACK:
			return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		case BorderColor::INT_TRANSPARENT_BLACK:
			return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;
		case BorderColor::FLOAT_OPAQUE_BLACK:
			return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		case BorderColor::INT_OPAQUE_BLACK:
			return VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		case BorderColor::FLOAT_OPAQUE_WHITE:
			return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		case BorderColor::INT_OPAQUE_WHITE:
			return VK_BORDER_COLOR_INT_OPAQUE_WHITE;
		default:
			return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	}
}

VkImageAspectFlags TextureAspectToVulkan(TextureAspect aspect)
{
	VkImageAspectFlags flags = 0;

	if (static_cast<uint32_t>(aspect & TextureAspect::COLOR))
		flags |= VK_IMAGE_ASPECT_COLOR_BIT;
	if (static_cast<uint32_t>(aspect & TextureAspect::DEPTH))
		flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
	if (static_cast<uint32_t>(aspect & TextureAspect::STENCIL))
		flags |= VK_IMAGE_ASPECT_STENCIL_BIT;

	return flags;
}

}        // namespace rhi::vulkan