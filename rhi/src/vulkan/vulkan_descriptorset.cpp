#include <stdexcept>
#include <unordered_map>

#include "vulkan_backend.h"

namespace RHI
{

// VulkanDescriptorSetLayout implementation
VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, const DescriptorSetLayoutDesc &desc) :
    device(device)
{
	std::vector<VkDescriptorSetLayoutBinding>      bindings;
	std::unordered_map<VkDescriptorType, uint32_t> poolSizeMap;

	for (const auto &binding : desc.bindings)
	{
		VkDescriptorSetLayoutBinding layoutBinding{};
		layoutBinding.binding            = binding.binding;
		layoutBinding.descriptorType     = DescriptorTypeToVulkan(binding.type);
		layoutBinding.descriptorCount    = binding.count;
		layoutBinding.stageFlags         = ShaderStageFlagsToVulkan(binding.stageFlags);
		layoutBinding.pImmutableSamplers = nullptr;

		bindings.push_back(layoutBinding);

		// Track pool sizes needed
		poolSizeMap[layoutBinding.descriptorType] += layoutBinding.descriptorCount;
	}

	// Convert pool size map to vector
	for (const auto &pair : poolSizeMap)
	{
		VkDescriptorPoolSize poolSize{};
		poolSize.type            = pair.first;
		poolSize.descriptorCount = pair.second;
		poolSizes.push_back(poolSize);
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings    = bindings.data();

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create descriptor set layout");
	}
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout()
{
	vkDestroyDescriptorSetLayout(device, layout, nullptr);
}

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanDescriptorSetLayout &&other) noexcept :
    device(other.device),
    layout(other.layout),
    poolSizes(std::move(other.poolSizes))
{
	other.device = VK_NULL_HANDLE;
	other.layout = VK_NULL_HANDLE;
}

VulkanDescriptorSetLayout &VulkanDescriptorSetLayout::operator=(VulkanDescriptorSetLayout &&other) noexcept
{
	if (this != &other)
	{
		if (layout != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}

		device    = other.device;
		layout    = other.layout;
		poolSizes = std::move(other.poolSizes);

		other.device = VK_NULL_HANDLE;
		other.layout = VK_NULL_HANDLE;
	}
	return *this;
}

// VulkanDescriptorSet implementation
VulkanDescriptorSet::VulkanDescriptorSet(VkDevice device, VulkanDescriptorSetLayout *layout, VkDescriptorPool pool,
                                         VkDescriptorSet set) :
    device(device), descriptorSet(set), sourcePool(pool), layout(layout)
{}

VulkanDescriptorSet::~VulkanDescriptorSet()
{
	// Descriptor sets are automatically freed when pool is reset or destroyed
	// No explicit cleanup needed
}

VulkanDescriptorSet::VulkanDescriptorSet(VulkanDescriptorSet &&other) noexcept :
    device(other.device),
    descriptorSet(other.descriptorSet),
    sourcePool(other.sourcePool),
    layout(other.layout)
{
	other.device        = VK_NULL_HANDLE;
	other.descriptorSet = VK_NULL_HANDLE;
	other.sourcePool    = VK_NULL_HANDLE;
	other.layout        = nullptr;
}

VulkanDescriptorSet &VulkanDescriptorSet::operator=(VulkanDescriptorSet &&other) noexcept
{
	if (this != &other)
	{
		// No explicit cleanup needed for descriptor sets

		// Move from other
		device        = other.device;
		descriptorSet = other.descriptorSet;
		sourcePool    = other.sourcePool;
		layout        = other.layout;

		// Reset other
		other.device        = VK_NULL_HANDLE;
		other.descriptorSet = VK_NULL_HANDLE;
		other.sourcePool    = VK_NULL_HANDLE;
		other.layout        = nullptr;
	}
	return *this;
}

void VulkanDescriptorSet::BindBuffer(uint32_t binding, const BufferBinding &bufferBinding)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(bufferBinding.buffer);

	VkDescriptorBufferInfo bufferInfo{};
	bufferInfo.buffer = vkBuffer->GetHandle();
	bufferInfo.offset = bufferBinding.offset;
	bufferInfo.range  = (bufferBinding.range == 0) ? VK_WHOLE_SIZE : bufferBinding.range;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet          = descriptorSet;
	descriptorWrite.dstBinding      = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType  = DescriptorTypeToVulkan(bufferBinding.type);
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo     = &bufferInfo;

	vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanDescriptorSet::BindTexture(uint32_t binding, const TextureBinding &textureBinding)
{
	auto *vkTexture = static_cast<VulkanTexture *>(textureBinding.texture);

	VkDescriptorImageInfo imageInfo{};
	imageInfo.imageLayout = ImageLayoutToVulkan(textureBinding.layout);
	imageInfo.imageView   = vkTexture->GetImageView();

	// Use provided sampler or VK_NULL_HANDLE for separate texture bindings
	if (textureBinding.sampler)
	{
		auto *vkSampler   = static_cast<const VulkanSampler *>(textureBinding.sampler);
		imageInfo.sampler = vkSampler->GetHandle();
	}
	else
	{
		imageInfo.sampler = VK_NULL_HANDLE;
	}

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet          = descriptorSet;
	descriptorWrite.dstBinding      = binding;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType  = DescriptorTypeToVulkan(textureBinding.type);
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pImageInfo      = &imageInfo;

	vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

// VulkanSampler implementation
VulkanSampler::VulkanSampler(VkDevice device) :
    device(device)
{
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter               = VK_FILTER_LINEAR;
	samplerInfo.minFilter               = VK_FILTER_LINEAR;
	samplerInfo.addressModeU            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW            = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable        = VK_FALSE;
	samplerInfo.maxAnisotropy           = 1.0f;
	samplerInfo.borderColor             = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable           = VK_FALSE;
	samplerInfo.compareOp               = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode              = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create texture sampler");
	}
}

VulkanSampler::~VulkanSampler()
{
	vkDestroySampler(device, sampler, nullptr);
}

VulkanSampler::VulkanSampler(VulkanSampler &&other) noexcept :
    device(other.device),
    sampler(other.sampler)
{
	other.device  = VK_NULL_HANDLE;
	other.sampler = VK_NULL_HANDLE;
}

VulkanSampler &VulkanSampler::operator=(VulkanSampler &&other) noexcept
{
	if (this != &other)
	{
		if (sampler != VK_NULL_HANDLE)
		{
			vkDestroySampler(device, sampler, nullptr);
		}

		device  = other.device;
		sampler = other.sampler;

		other.device  = VK_NULL_HANDLE;
		other.sampler = VK_NULL_HANDLE;
	}
	return *this;
}

}        // namespace RHI