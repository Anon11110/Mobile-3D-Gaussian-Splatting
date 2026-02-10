#include <stdexcept>
#include <utility>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanShader::VulkanShader(VkDevice device, const ShaderDesc &desc) :
    device(device), shaderModule(VK_NULL_HANDLE), stage(desc.stage), entryPoint(desc.entryPoint != nullptr ? desc.entryPoint : "main")
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

VulkanShader::VulkanShader(VulkanShader &&other) noexcept :
    device(other.device),
    shaderModule(other.shaderModule),
    stage(other.stage),
    entryPoint(std::move(other.entryPoint))
{
	other.device       = VK_NULL_HANDLE;
	other.shaderModule = VK_NULL_HANDLE;
	other.stage        = ShaderStage::VERTEX;
	other.entryPoint   = "main";
}

VulkanShader &VulkanShader::operator=(VulkanShader &&other) noexcept
{
	if (this != &other)
	{
		if (shaderModule != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(device, shaderModule, nullptr);
		}

		device       = other.device;
		shaderModule = other.shaderModule;
		stage        = other.stage;
		entryPoint   = std::move(other.entryPoint);

		other.device       = VK_NULL_HANDLE;
		other.shaderModule = VK_NULL_HANDLE;
		other.stage        = ShaderStage::VERTEX;
		other.entryPoint   = "main";
	}
	return *this;
}

}        // namespace rhi::vulkan
