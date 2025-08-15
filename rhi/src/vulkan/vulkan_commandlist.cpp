#include <stdexcept>

#include "vulkan_backend.h"

namespace RHI
{

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool commandPool,
                                     PFN_vkCmdBeginRenderingKHR beginFunc,
                                     PFN_vkCmdEndRenderingKHR   endFunc) :
    device(device), commandBuffer(VK_NULL_HANDLE), currentPipeline(nullptr), inRendering(false), vkCmdBeginRenderingKHR(beginFunc), vkCmdEndRenderingKHR(endFunc)
{
	// Allocate command buffer
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool        = commandPool;
	allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to allocate Vulkan command buffer");
	}
}

VulkanCommandList::~VulkanCommandList()
{
	// Command buffers are automatically freed when command pool is destroyed
}

void VulkanCommandList::Begin()
{
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to begin recording command buffer");
	}
}

void VulkanCommandList::End()
{
	if (inRendering)
	{
		EndRendering();
	}

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to record command buffer");
	}
}

void VulkanCommandList::Reset()
{
	if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to reset command buffer");
	}
	inRendering     = false;
	currentPipeline = nullptr;
}

void VulkanCommandList::BeginRendering(const RenderingInfo &info)
{
	// Copy info and auto-fill render area if necessary
	RenderingInfo renderingInfo = info;
	if (renderingInfo.renderAreaWidth == 0 || renderingInfo.renderAreaHeight == 0)
	{
		// Infer from first color attachment, or depth attachment if no color
		if (!renderingInfo.colorAttachments.empty() && renderingInfo.colorAttachments[0].view)
		{
			auto *view                     = renderingInfo.colorAttachments[0].view;
			renderingInfo.renderAreaWidth  = view->GetWidth();
			renderingInfo.renderAreaHeight = view->GetHeight();
		}
		else if (renderingInfo.depthStencilAttachment.view)
		{
			auto *view                     = renderingInfo.depthStencilAttachment.view;
			renderingInfo.renderAreaWidth  = view->GetWidth();
			renderingInfo.renderAreaHeight = view->GetHeight();
		}
	}

	// Setup color attachments
	std::vector<VkRenderingAttachmentInfoKHR> colorAttachments;
	for (const auto &attachment : renderingInfo.colorAttachments)
	{
		VkRenderingAttachmentInfoKHR colorAttachment{};
		colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;

		if (attachment.view)
		{
			auto *vkView                = static_cast<VulkanTextureView *>(attachment.view);
			colorAttachment.imageView   = vkView->GetHandle();
			colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		if (attachment.resolveTarget)
		{
			auto *vkResolve                    = static_cast<VulkanTextureView *>(attachment.resolveTarget);
			colorAttachment.resolveImageView   = vkResolve->GetHandle();
			colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
			if (attachment.resolveMode != ResolveMode::NONE)
			{
				switch (attachment.resolveMode)
				{
					case ResolveMode::SAMPLE_ZERO:
						resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
						break;
					case ResolveMode::AVERAGE:
						resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
						break;
				}
			}
			else
			{
				// Auto-select based on format
				if (attachment.view)
				{
					auto         *vkView = static_cast<VulkanTextureView *>(attachment.view);
					TextureFormat format = vkView->GetFormat();
					if (format == TextureFormat::R8G8B8A8_UNORM || format == TextureFormat::B8G8R8A8_UNORM ||
					    format == TextureFormat::R8G8B8A8_SRGB || format == TextureFormat::B8G8R8A8_SRGB ||
					    format == TextureFormat::R32G32B32_FLOAT)
					{
						resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
					}
					else
					{
						resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
					}
				}
			}
			colorAttachment.resolveMode = resolveMode;
		}

		colorAttachment.loadOp  = LoadOpToVulkan(attachment.loadOp);
		colorAttachment.storeOp = StoreOpToVulkan(attachment.storeOp);

		// Set clear value
		VkClearValue clearValue{};
		memcpy(clearValue.color.float32, attachment.clearValue.color, sizeof(float) * 4);
		colorAttachment.clearValue = clearValue;

		colorAttachments.push_back(colorAttachment);
	}

	// Setup depth-stencil attachment (single attachment for combined depth/stencil)
	VkRenderingAttachmentInfoKHR  depthStencilAttachment{};
	VkRenderingAttachmentInfoKHR *pDepthAttachment   = nullptr;
	VkRenderingAttachmentInfoKHR *pStencilAttachment = nullptr;

	if (renderingInfo.depthStencilAttachment.view)
	{
		auto *vkDepthView = static_cast<VulkanTextureView *>(renderingInfo.depthStencilAttachment.view);

		// Setup combined depth/stencil attachment
		depthStencilAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
		depthStencilAttachment.imageView   = vkDepthView->GetHandle();
		depthStencilAttachment.imageLayout = renderingInfo.depthStencilAttachment.readOnly ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkClearValue depthClearValue{};
		depthClearValue.depthStencil.depth   = renderingInfo.depthStencilAttachment.clearValue.depthStencil.depth;
		depthClearValue.depthStencil.stencil = renderingInfo.depthStencilAttachment.clearValue.depthStencil.stencil;
		depthStencilAttachment.clearValue    = depthClearValue;

		VkFormat format = TextureFormatToVulkan(vkDepthView->GetFormat());
		if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
		{
			// Use stencil ops for combined format
			depthStencilAttachment.loadOp  = LoadOpToVulkan(renderingInfo.depthStencilAttachment.stencilLoadOp);
			depthStencilAttachment.storeOp = StoreOpToVulkan(renderingInfo.depthStencilAttachment.stencilStoreOp);
		}
		else
		{
			// Use depth ops for depth-only format
			depthStencilAttachment.loadOp  = LoadOpToVulkan(renderingInfo.depthStencilAttachment.depthLoadOp);
			depthStencilAttachment.storeOp = StoreOpToVulkan(renderingInfo.depthStencilAttachment.depthStoreOp);
		}

		pDepthAttachment = &depthStencilAttachment;
	}

	// Begin dynamic rendering
	VkRenderingInfoKHR vkRenderingInfo{};
	vkRenderingInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
	vkRenderingInfo.flags                = 0;
	vkRenderingInfo.renderArea.offset    = {static_cast<int32_t>(renderingInfo.renderAreaX), static_cast<int32_t>(renderingInfo.renderAreaY)};
	vkRenderingInfo.renderArea.extent    = {renderingInfo.renderAreaWidth, renderingInfo.renderAreaHeight};
	vkRenderingInfo.layerCount           = renderingInfo.layerCount;
	vkRenderingInfo.viewMask             = 0;
	vkRenderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
	vkRenderingInfo.pColorAttachments    = colorAttachments.data();
	vkRenderingInfo.pDepthAttachment     = pDepthAttachment;
	vkRenderingInfo.pStencilAttachment   = pStencilAttachment;

	if (!vkCmdBeginRenderingKHR)
	{
		throw std::runtime_error("Dynamic rendering not available - vkCmdBeginRenderingKHR is null");
	}

	vkCmdBeginRenderingKHR(commandBuffer, &vkRenderingInfo);
	inRendering = true;
}

void VulkanCommandList::EndRendering()
{
	if (!inRendering)
	{
		return;
	}

	if (!vkCmdEndRenderingKHR)
	{
		throw std::runtime_error("Dynamic rendering not available - vkCmdEndRenderingKHR is null");
	}

	vkCmdEndRenderingKHR(commandBuffer);
	inRendering = false;
}

void VulkanCommandList::SetPipeline(IRHIPipeline *pipeline)
{
	currentPipeline = static_cast<VulkanPipeline *>(pipeline);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->GetHandle());
}

void VulkanCommandList::SetVertexBuffer(uint32_t binding, IRHIBuffer *buffer, size_t offset)
{
	auto        *vkBuffer  = static_cast<VulkanBuffer *>(buffer);
	VkBuffer     buffers[] = {vkBuffer->GetHandle()};
	VkDeviceSize offsets[] = {offset};
	vkCmdBindVertexBuffers(commandBuffer, binding, 1, buffers, offsets);
}

void VulkanCommandList::BindIndexBuffer(IRHIBuffer *buffer, size_t offset)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(buffer);
	vkCmdBindIndexBuffer(commandBuffer, vkBuffer->GetHandle(), offset, VK_INDEX_TYPE_UINT16);
}

void VulkanCommandList::BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
                                          const uint32_t *dynamicOffsets, uint32_t dynamicOffsetCount)
{
	if (!currentPipeline)
	{
		throw std::runtime_error("No pipeline bound when binding descriptor set");
	}

	auto           *vkDescriptorSet = static_cast<VulkanDescriptorSet *>(descriptorSet);
	VkDescriptorSet sets[]          = {vkDescriptorSet->GetHandle()};

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, currentPipeline->GetLayout(), setIndex, 1,
	                        sets, dynamicOffsetCount, dynamicOffsets);
}

void VulkanCommandList::PushConstants(ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *data)
{
	if (!currentPipeline)
	{
		throw std::runtime_error("No pipeline bound when pushing constants");
	}

	VkShaderStageFlags vkStageFlags = ShaderStageFlagsToVulkan(stageFlags);
	vkCmdPushConstants(commandBuffer, currentPipeline->GetLayout(), vkStageFlags, offset, size, data);
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height)
{
	VkViewport viewport{};
	viewport.x        = x;
	viewport.y        = y;
	viewport.width    = width;
	viewport.height   = height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}

void VulkanCommandList::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	VkRect2D scissor{};
	scissor.offset = {x, y};
	scissor.extent = {width, height};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
	vkCmdDraw(commandBuffer, vertexCount, 1, firstVertex, 0);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
{
	vkCmdDrawIndexed(commandBuffer, indexCount, 1, firstIndex, vertexOffset, 0);
}

void VulkanCommandList::DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(buffer);
	vkCmdDrawIndexedIndirect(commandBuffer, vkBuffer->GetHandle(), offset, drawCount, stride);
}

void VulkanCommandList::Barrier(
    PipelineScope                         src_scope,
    PipelineScope                         dst_scope,
    const std::vector<BufferTransition>  &buffer_transitions,
    const std::vector<TextureTransition> &texture_transitions,
    const std::vector<MemoryBarrier>     &memory_barriers)
{
	std::vector<VkBufferMemoryBarrier> bufferBarriers;
	std::vector<VkImageMemoryBarrier>  imageBarriers;
	std::vector<VkMemoryBarrier>       memBarriers;

	VkPipelineStageFlags srcStageFlags = 0;
	VkPipelineStageFlags dstStageFlags = 0;

	// Process buffer transitions
	for (const auto &transition : buffer_transitions)
	{
		if (!transition.buffer)
			continue;

		VkBufferMemoryBarrier barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer              = static_cast<VulkanBuffer *>(transition.buffer)->GetHandle();
		barrier.offset              = transition.offset;
		barrier.size                = (transition.size == ~0ull) ? VK_WHOLE_SIZE : transition.size;

		VkPipelineStageFlags srcStages, dstStages;
		VkAccessFlags        srcAccess, dstAccess;

		if (transition.src_stages != StageMask::Auto)
		{
			srcStages = StageMaskToVulkan(transition.src_stages);
			srcAccess = AccessMaskToVulkan(transition.src_access);
		}
		else
		{
			GetVulkanStagesAndAccess(transition.before, src_scope, srcStages, srcAccess);
		}

		if (transition.dst_stages != StageMask::Auto)
		{
			dstStages = StageMaskToVulkan(transition.dst_stages);
			dstAccess = AccessMaskToVulkan(transition.dst_access);
		}
		else
		{
			GetVulkanStagesAndAccess(transition.after, dst_scope, dstStages, dstAccess);
		}

		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;

		srcStageFlags |= srcStages;
		dstStageFlags |= dstStages;

		bufferBarriers.push_back(barrier);
	}

	// Process texture transitions
	for (const auto &transition : texture_transitions)
	{
		if (!transition.texture)
			continue;

		VkImageMemoryBarrier barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image               = static_cast<VulkanTexture *>(transition.texture)->GetHandle();
		barrier.oldLayout           = ResourceStateToImageLayout(transition.before);
		barrier.newLayout           = ResourceStateToImageLayout(transition.after);

		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		// Check if it's a depth/stencil format
		TextureFormat format = transition.texture->GetFormat();
		if (format == TextureFormat::D32_FLOAT)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		}
		else if (format == TextureFormat::D24_UNORM_S8_UINT)
		{
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		barrier.subresourceRange.baseMipLevel   = transition.baseMipLevel;
		barrier.subresourceRange.levelCount     = (transition.mipLevelCount == ~0u) ? VK_REMAINING_MIP_LEVELS : transition.mipLevelCount;
		barrier.subresourceRange.baseArrayLayer = transition.baseArrayLayer;
		barrier.subresourceRange.layerCount     = (transition.arrayLayerCount == ~0u) ? VK_REMAINING_ARRAY_LAYERS : transition.arrayLayerCount;

		VkPipelineStageFlags srcStages, dstStages;
		VkAccessFlags        srcAccess, dstAccess;

		if (transition.src_stages != StageMask::Auto)
		{
			srcStages = StageMaskToVulkan(transition.src_stages);
			srcAccess = AccessMaskToVulkan(transition.src_access);
		}
		else
		{
			GetVulkanStagesAndAccess(transition.before, src_scope, srcStages, srcAccess);
		}

		if (transition.dst_stages != StageMask::Auto)
		{
			dstStages = StageMaskToVulkan(transition.dst_stages);
			dstAccess = AccessMaskToVulkan(transition.dst_access);
		}
		else
		{
			GetVulkanStagesAndAccess(transition.after, dst_scope, dstStages, dstAccess);
		}

		barrier.srcAccessMask = srcAccess;
		barrier.dstAccessMask = dstAccess;

		srcStageFlags |= srcStages;
		dstStageFlags |= dstStages;

		imageBarriers.push_back(barrier);
	}

	// Process memory barriers
	for (const auto &memBarrier : memory_barriers)
	{
		VkMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;

		if (memBarrier.src_stages != StageMask::Auto)
		{
			srcStageFlags |= StageMaskToVulkan(memBarrier.src_stages);
			barrier.srcAccessMask = AccessMaskToVulkan(memBarrier.src_access);
		}
		else
		{
			srcStageFlags |= PipelineScopeToVulkanStages(src_scope);
			barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
		}

		if (memBarrier.dst_stages != StageMask::Auto)
		{
			dstStageFlags |= StageMaskToVulkan(memBarrier.dst_stages);
			barrier.dstAccessMask = AccessMaskToVulkan(memBarrier.dst_access);
		}
		else
		{
			dstStageFlags |= PipelineScopeToVulkanStages(dst_scope);
			barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
		}

		memBarriers.push_back(barrier);
	}

	// If no specific stages were set, use the scope defaults
	if (srcStageFlags == 0)
		srcStageFlags = PipelineScopeToVulkanStages(src_scope);
	if (dstStageFlags == 0)
		dstStageFlags = PipelineScopeToVulkanStages(dst_scope);

	if (!bufferBarriers.empty() || !imageBarriers.empty() || !memBarriers.empty())
	{
		vkCmdPipelineBarrier(
		    commandBuffer,
		    srcStageFlags,
		    dstStageFlags,
		    0,
		    static_cast<uint32_t>(memBarriers.size()),
		    memBarriers.empty() ? nullptr : memBarriers.data(),
		    static_cast<uint32_t>(bufferBarriers.size()),
		    bufferBarriers.empty() ? nullptr : bufferBarriers.data(),
		    static_cast<uint32_t>(imageBarriers.size()),
		    imageBarriers.empty() ? nullptr : imageBarriers.data());
	}
}

}        // namespace RHI