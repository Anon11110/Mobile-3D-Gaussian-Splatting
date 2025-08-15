#include <stdexcept>
#include <vector>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanPipeline::VulkanPipeline(VkDevice device, const GraphicsPipelineDesc &desc) :
    device(device), pipeline(VK_NULL_HANDLE), pipelineLayout(VK_NULL_HANDLE)
{
	auto *vertexShader   = static_cast<VulkanShader *>(desc.vertexShader);
	auto *fragmentShader = static_cast<VulkanShader *>(desc.fragmentShader);

	targetSignature = desc.targetSignature;

	// Shader stages
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage  = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertexShader->GetHandle();
	vertShaderStageInfo.pName  = "main";
	shaderStages.push_back(vertShaderStageInfo);

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragmentShader->GetHandle();
	fragShaderStageInfo.pName  = "main";
	shaderStages.push_back(fragShaderStageInfo);

	// Vertex input
	std::vector<VkVertexInputBindingDescription>   bindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

	for (const auto &binding : desc.vertexLayout.bindings)
	{
		VkVertexInputBindingDescription bindingDesc{};
		bindingDesc.binding   = binding.binding;
		bindingDesc.stride    = binding.stride;
		bindingDesc.inputRate = binding.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
		bindingDescriptions.push_back(bindingDesc);
	}

	for (const auto &attribute : desc.vertexLayout.attributes)
	{
		VkVertexInputAttributeDescription attributeDesc{};
		attributeDesc.binding  = attribute.binding;
		attributeDesc.location = attribute.location;
		attributeDesc.format   = VertexFormatToVulkan(attribute.format);
		attributeDesc.offset   = attribute.offset;
		attributeDescriptions.push_back(attributeDesc);
	}

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindingDescriptions.size());
	vertexInputInfo.pVertexBindingDescriptions      = bindingDescriptions.data();
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions    = attributeDescriptions.data();

	// Input assembly
	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology               = PrimitiveTopologyToVulkan(desc.topology);
	inputAssembly.primitiveRestartEnable = desc.primitiveRestartEnable ? VK_TRUE : VK_FALSE;

	// Viewport state (dynamic)
	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount  = 1;

	// Rasterizer
	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable        = desc.rasterizationState.depthClampEnable ? VK_TRUE : VK_FALSE;
	rasterizer.rasterizerDiscardEnable = desc.rasterizationState.rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;
	rasterizer.polygonMode             = PolygonModeToVulkan(desc.rasterizationState.polygonMode);
	rasterizer.lineWidth               = 1.0f;
	rasterizer.cullMode                = CullModeToVulkan(desc.rasterizationState.cullMode);
	rasterizer.frontFace               = FrontFaceToVulkan(desc.rasterizationState.frontFace);
	rasterizer.depthBiasEnable         = desc.rasterizationState.depthBiasEnable ? VK_TRUE : VK_FALSE;
	rasterizer.depthBiasConstantFactor = desc.rasterizationState.depthBiasConstantFactor;
	rasterizer.depthBiasClamp          = desc.rasterizationState.depthBiasClamp;
	rasterizer.depthBiasSlopeFactor    = desc.rasterizationState.depthBiasSlopeFactor;

	// Depth-stencil state
	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable       = desc.depthStencilState.depthTestEnable ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable      = desc.depthStencilState.depthWriteEnable ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp        = CompareOpToVulkan(desc.depthStencilState.depthCompareOp);
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable     = desc.depthStencilState.stencilTestEnable ? VK_TRUE : VK_FALSE;

	// Front stencil state
	depthStencil.front.failOp      = StencilOpToVulkan(desc.depthStencilState.front.failOp);
	depthStencil.front.passOp      = StencilOpToVulkan(desc.depthStencilState.front.passOp);
	depthStencil.front.depthFailOp = StencilOpToVulkan(desc.depthStencilState.front.depthFailOp);
	depthStencil.front.compareOp   = CompareOpToVulkan(desc.depthStencilState.front.compareOp);
	depthStencil.front.compareMask = desc.depthStencilState.front.compareMask;
	depthStencil.front.writeMask   = desc.depthStencilState.front.writeMask;
	depthStencil.front.reference   = desc.depthStencilState.front.reference;

	// Back stencil state
	depthStencil.back.failOp      = StencilOpToVulkan(desc.depthStencilState.back.failOp);
	depthStencil.back.passOp      = StencilOpToVulkan(desc.depthStencilState.back.passOp);
	depthStencil.back.depthFailOp = StencilOpToVulkan(desc.depthStencilState.back.depthFailOp);
	depthStencil.back.compareOp   = CompareOpToVulkan(desc.depthStencilState.back.compareOp);
	depthStencil.back.compareMask = desc.depthStencilState.back.compareMask;
	depthStencil.back.writeMask   = desc.depthStencilState.back.writeMask;
	depthStencil.back.reference   = desc.depthStencilState.back.reference;

	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;

	// Multisampling
	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable   = desc.multisampleState.sampleShadingEnable ? VK_TRUE : VK_FALSE;
	multisampling.rasterizationSamples  = SampleCountToVulkan(desc.multisampleState.rasterizationSamples);
	multisampling.minSampleShading      = desc.multisampleState.minSampleShading;
	multisampling.pSampleMask           = &desc.multisampleState.sampleMask;
	multisampling.alphaToCoverageEnable = desc.multisampleState.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
	multisampling.alphaToOneEnable      = VK_FALSE;

	// Color blending - support multiple attachments
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
	for (const auto &attachment : desc.colorBlendAttachments)
	{
		VkPipelineColorBlendAttachmentState vkAttachment{};
		vkAttachment.blendEnable         = attachment.blendEnable ? VK_TRUE : VK_FALSE;
		vkAttachment.srcColorBlendFactor = BlendFactorToVulkan(attachment.srcColorBlendFactor);
		vkAttachment.dstColorBlendFactor = BlendFactorToVulkan(attachment.dstColorBlendFactor);
		vkAttachment.colorBlendOp        = BlendOpToVulkan(attachment.colorBlendOp);
		vkAttachment.srcAlphaBlendFactor = BlendFactorToVulkan(attachment.srcAlphaBlendFactor);
		vkAttachment.dstAlphaBlendFactor = BlendFactorToVulkan(attachment.dstAlphaBlendFactor);
		vkAttachment.alphaBlendOp        = BlendOpToVulkan(attachment.alphaBlendOp);
		vkAttachment.colorWriteMask      = attachment.colorWriteMask;
		colorBlendAttachments.push_back(vkAttachment);
	}

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable   = VK_FALSE;
	colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	colorBlending.pAttachments    = colorBlendAttachments.data();
	memcpy(colorBlending.blendConstants, desc.blendConstants, sizeof(desc.blendConstants));

	// Dynamic state
	VkDynamicState                   dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates    = dynamicStates;

	// Collect descriptor set layouts
	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	for (auto *layout : desc.descriptorSetLayouts)
	{
		auto *vkLayout = static_cast<VulkanDescriptorSetLayout *>(layout);
		descriptorSetLayouts.push_back(vkLayout->GetHandle());
	}

	// Collect push constant ranges
	std::vector<VkPushConstantRange> pushConstantRanges;
	for (const auto &range : desc.pushConstantRanges)
	{
		VkPushConstantRange vkRange{};
		vkRange.stageFlags = ShaderStageFlagsToVulkan(range.stageFlags);
		vkRange.offset     = range.offset;
		vkRange.size       = range.size;
		pushConstantRanges.push_back(vkRange);
	}

	// Pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount         = static_cast<uint32_t>(descriptorSetLayouts.size());
	pipelineLayoutInfo.pSetLayouts            = descriptorSetLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
	pipelineLayoutInfo.pPushConstantRanges    = pushConstantRanges.data();

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create pipeline layout");
	}

	// Setup dynamic rendering info for pipeline creation
	std::vector<VkFormat> colorFormats;
	for (const auto &format : targetSignature.colorFormats)
	{
		colorFormats.push_back(TextureFormatToVulkan(format));
	}

	VkPipelineRenderingCreateInfoKHR renderingInfo{};
	renderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
	renderingInfo.colorAttachmentCount    = static_cast<uint32_t>(colorFormats.size());
	renderingInfo.pColorAttachmentFormats = colorFormats.data();

	VkFormat depthFormat   = VK_FORMAT_UNDEFINED;
	VkFormat stencilFormat = VK_FORMAT_UNDEFINED;
	if (targetSignature.depthFormat != TextureFormat::UNDEFINED)
	{
		VkFormat vkDepthFormat = TextureFormatToVulkan(targetSignature.depthFormat);
		depthFormat            = vkDepthFormat;
		// Check if format has stencil component
		if (vkDepthFormat == VK_FORMAT_D24_UNORM_S8_UINT || vkDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
		{
			stencilFormat = vkDepthFormat;
		}
	}
	renderingInfo.depthAttachmentFormat   = depthFormat;
	renderingInfo.stencilAttachmentFormat = stencilFormat;

	// Graphics pipeline with dynamic rendering
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext               = &renderingInfo;
	pipelineInfo.stageCount          = static_cast<uint32_t>(shaderStages.size());
	pipelineInfo.pStages             = shaderStages.data();
	pipelineInfo.pVertexInputState   = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState      = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState   = &multisampling;
	pipelineInfo.pDepthStencilState  = &depthStencil;
	pipelineInfo.pColorBlendState    = &colorBlending;
	pipelineInfo.pDynamicState       = &dynamicState;
	pipelineInfo.layout              = pipelineLayout;
	pipelineInfo.renderPass          = VK_NULL_HANDLE;
	pipelineInfo.subpass             = 0;

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create graphics pipeline");
	}
}

VulkanPipeline::~VulkanPipeline()
{
	if (pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(device, pipeline, nullptr);
	}

	if (pipelineLayout != VK_NULL_HANDLE)
	{
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
	}
}

VulkanPipeline::VulkanPipeline(VulkanPipeline &&other) noexcept :
    device(other.device),
    pipeline(other.pipeline),
    pipelineLayout(other.pipelineLayout),
    targetSignature(std::move(other.targetSignature))
{
	other.device         = VK_NULL_HANDLE;
	other.pipeline       = VK_NULL_HANDLE;
	other.pipelineLayout = VK_NULL_HANDLE;
}

VulkanPipeline &VulkanPipeline::operator=(VulkanPipeline &&other) noexcept
{
	if (this != &other)
	{
		if (pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, pipeline, nullptr);
		}
		if (pipelineLayout != VK_NULL_HANDLE)
		{
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		}

		device          = other.device;
		pipeline        = other.pipeline;
		pipelineLayout  = other.pipelineLayout;
		targetSignature = std::move(other.targetSignature);

		other.device         = VK_NULL_HANDLE;
		other.pipeline       = VK_NULL_HANDLE;
		other.pipelineLayout = VK_NULL_HANDLE;
	}
	return *this;
}

}        // namespace rhi::vulkan
