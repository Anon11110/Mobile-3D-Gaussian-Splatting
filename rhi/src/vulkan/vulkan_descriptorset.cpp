#include <stdexcept>
#include <unordered_map>

#include "vulkan_backend.h"

namespace RHI {

// Utility function implementations
VkDescriptorType DescriptorTypeToVulkan(DescriptorType type) {
    switch (type) {
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

VkShaderStageFlags ShaderStageFlagsToVulkan(ShaderStageFlags flags) {
    VkShaderStageFlags vkFlags = 0;
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::VERTEX)) {
        vkFlags |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::FRAGMENT)) {
        vkFlags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    if (static_cast<uint32_t>(flags) & static_cast<uint32_t>(ShaderStageFlags::COMPUTE)) {
        vkFlags |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    return vkFlags;
}

VkImageLayout ImageLayoutToVulkan(ImageLayout layout) {
    switch (layout) {
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

// VulkanDescriptorSetLayout implementation
VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VkDevice device, const DescriptorSetLayoutDesc& desc)
    : device(device) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::unordered_map<VkDescriptorType, uint32_t> poolSizeMap;

    for (const auto& binding : desc.bindings) {
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding.binding;
        layoutBinding.descriptorType = DescriptorTypeToVulkan(binding.type);
        layoutBinding.descriptorCount = binding.count;
        layoutBinding.stageFlags = ShaderStageFlagsToVulkan(binding.stageFlags);
        layoutBinding.pImmutableSamplers = nullptr;

        bindings.push_back(layoutBinding);

        // Track pool sizes needed
        poolSizeMap[layoutBinding.descriptorType] += layoutBinding.descriptorCount;
    }

    // Convert pool size map to vector
    for (const auto& pair : poolSizeMap) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = pair.first;
        poolSize.descriptorCount = pair.second;
        poolSizes.push_back(poolSize);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout");
    }
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
    vkDestroyDescriptorSetLayout(device, layout, nullptr);
}

// VulkanDescriptorSet implementation
VulkanDescriptorSet::VulkanDescriptorSet(VkDevice device, VulkanDescriptorSetLayout* layout, VkDescriptorPool pool,
                                         VkDescriptorSet set)
    : device(device), descriptorSet(set), sourcePool(pool), layout(layout) {}

VulkanDescriptorSet::~VulkanDescriptorSet() {
    // Descriptor sets are automatically freed when pool is reset or destroyed
    // No explicit cleanup needed
}

void VulkanDescriptorSet::BindBuffer(uint32_t binding, const BufferBinding& bufferBinding) {
    auto* vkBuffer = static_cast<VulkanBuffer*>(bufferBinding.buffer);

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer->GetHandle();
    bufferInfo.offset = bufferBinding.offset;
    bufferInfo.range = (bufferBinding.range == 0) ? VK_WHOLE_SIZE : bufferBinding.range;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = DescriptorTypeToVulkan(bufferBinding.type);
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanDescriptorSet::BindTexture(uint32_t binding, const TextureBinding& textureBinding) {
    auto* vkTexture = static_cast<VulkanTexture*>(textureBinding.texture);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = ImageLayoutToVulkan(textureBinding.layout);
    imageInfo.imageView = vkTexture->GetImageView();

    // Use provided sampler or VK_NULL_HANDLE for separate texture bindings
    if (textureBinding.sampler) {
        auto* vkSampler = static_cast<const VulkanSampler*>(textureBinding.sampler);
        imageInfo.sampler = vkSampler->GetHandle();
    } else {
        imageInfo.sampler = VK_NULL_HANDLE;
    }

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = binding;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = DescriptorTypeToVulkan(textureBinding.type);
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

// VulkanSampler implementation
VulkanSampler::VulkanSampler(VkDevice device) : device(device) {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler");
    }
}

VulkanSampler::~VulkanSampler() {
    vkDestroySampler(device, sampler, nullptr);
}

}  // namespace RHI