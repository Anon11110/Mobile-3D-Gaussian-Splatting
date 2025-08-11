#include <stdexcept>

#include "vulkan_backend.h"

namespace RHI
{

VulkanShader::VulkanShader(VkDevice device, const ShaderDesc &desc) :
    device(device), shaderModule(VK_NULL_HANDLE), stage(desc.stage)
{
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = desc.codeSize;
	createInfo.pCode    = reinterpret_cast<const uint32_t *>(desc.code);

	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan shader module");
	}
}

VulkanShader::~VulkanShader()
{
	if (shaderModule != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(device, shaderModule, nullptr);
	}
}

}        // namespace RHI