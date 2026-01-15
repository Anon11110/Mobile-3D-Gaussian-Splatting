#include <stdexcept>

#include "vulkan_backend.h"

namespace rhi::vulkan
{

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool commandPool, QueueType queueType, uint32_t queueFamily,
                                     uint32_t graphicsFamily, uint32_t computeFamily, uint32_t transferFamily,
                                     PFN_vkCmdBeginRenderingKHR beginFunc,
                                     PFN_vkCmdEndRenderingKHR   endFunc,
                                     PFN_vkCmdPipelineBarrier2  barrier2Func,
                                     PFN_vkCmdWriteTimestamp2   timestamp2Func) :
    device(device), commandBuffer(VK_NULL_HANDLE), currentPipeline(nullptr), inRendering(false), queueType(queueType), queueFamily(queueFamily), graphicsQueueFamily(graphicsFamily), computeQueueFamily(computeFamily), transferQueueFamily(transferFamily), vkCmdBeginRenderingKHR(beginFunc), vkCmdEndRenderingKHR(endFunc), vkCmdPipelineBarrier2(barrier2Func), vkCmdWriteTimestamp2(timestamp2Func)
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
					case ResolveMode::NONE:
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
	vkCmdBindPipeline(commandBuffer, currentPipeline->GetBindPoint(), currentPipeline->GetHandle());
	// Track pipeline resource
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

	// Map RHI IndexType to Vulkan index type
	VkIndexType vkIndexType;
	switch (vkBuffer->GetIndexType())
	{
		case IndexType::UINT16:
			vkIndexType = VK_INDEX_TYPE_UINT16;
			break;
		case IndexType::UINT32:
			vkIndexType = VK_INDEX_TYPE_UINT32;
			break;
		default:
			vkIndexType = VK_INDEX_TYPE_UINT32;        // Safe default
			break;
	}

	vkCmdBindIndexBuffer(commandBuffer, vkBuffer->GetHandle(), offset, vkIndexType);
}

void VulkanCommandList::BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
                                          std::span<const uint32_t> dynamicOffsets)
{
	if (!currentPipeline)
	{
		throw std::runtime_error("No pipeline bound when binding descriptor set");
	}

	auto           *vkDescriptorSet = static_cast<VulkanDescriptorSet *>(descriptorSet);
	VkDescriptorSet sets[]          = {vkDescriptorSet->GetHandle()};

	vkCmdBindDescriptorSets(commandBuffer, currentPipeline->GetBindPoint(), currentPipeline->GetLayout(), setIndex, 1,
	                        sets, static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
}

void VulkanCommandList::PushConstants(ShaderStageFlags stageFlags, uint32_t offset, std::span<const std::byte> data)
{
	if (!currentPipeline)
	{
		throw std::runtime_error("No pipeline bound when pushing constants");
	}

	VkShaderStageFlags vkStageFlags = ShaderStageFlagsToVulkan(stageFlags);
	vkCmdPushConstants(commandBuffer, currentPipeline->GetLayout(), vkStageFlags, offset,
	                   static_cast<uint32_t>(data.size()), data.data());
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

void VulkanCommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                             int32_t vertexOffset, uint32_t firstInstance)
{
	vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(buffer);
	vkCmdDrawIndexedIndirect(commandBuffer, vkBuffer->GetHandle(), offset, drawCount, stride);
}

void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::DispatchIndirect(IRHIBuffer *buffer, size_t offset)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(buffer);
	vkCmdDispatchIndirect(commandBuffer, vkBuffer->GetHandle(), offset);
}

void VulkanCommandList::CopyBuffer(IRHIBuffer *srcBuffer, IRHIBuffer *dstBuffer, std::span<const BufferCopy> regions)
{
	if (regions.empty())
		return;

	static_assert(sizeof(BufferCopy) == sizeof(VkBufferCopy));
	static_assert(offsetof(BufferCopy, srcOffset) == offsetof(VkBufferCopy, srcOffset));
	static_assert(offsetof(BufferCopy, dstOffset) == offsetof(VkBufferCopy, dstOffset));
	static_assert(offsetof(BufferCopy, size) == offsetof(VkBufferCopy, size));

	auto *vkSrcBuffer = static_cast<VulkanBuffer *>(srcBuffer);
	auto *vkDstBuffer = static_cast<VulkanBuffer *>(dstBuffer);

	vkCmdCopyBuffer(commandBuffer, vkSrcBuffer->GetHandle(), vkDstBuffer->GetHandle(),
	                static_cast<uint32_t>(regions.size()),
	                reinterpret_cast<const VkBufferCopy *>(regions.data()));
}

void VulkanCommandList::FillBuffer(IRHIBuffer *buffer, size_t offset, size_t size, uint32_t value)
{
	auto *vkBuffer = static_cast<VulkanBuffer *>(buffer);

	// size == 0 means fill the entire buffer from offset to the end
	VkDeviceSize fillSize = (size == 0) ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(size);

	vkCmdFillBuffer(commandBuffer, vkBuffer->GetHandle(),
	                static_cast<VkDeviceSize>(offset), fillSize, value);
}

void VulkanCommandList::CopyTexture(IRHITexture *srcTexture, IRHITexture *dstTexture, std::span<const TextureCopy> regions)
{
	if (regions.empty())
		return;

	auto *vkSrcTexture = static_cast<VulkanTexture *>(srcTexture);
	auto *vkDstTexture = static_cast<VulkanTexture *>(dstTexture);

	std::vector<VkImageCopy> vkRegions;
	vkRegions.reserve(regions.size());

	for (const auto &region : regions)
	{
		VkImageCopy vkRegion{};

		// Source subresource
		vkRegion.srcSubresource.aspectMask     = TextureAspectToVulkan(region.aspectMask);
		vkRegion.srcSubresource.mipLevel       = region.srcMipLevel;
		vkRegion.srcSubresource.baseArrayLayer = region.srcArrayLayer;
		vkRegion.srcSubresource.layerCount     = region.layerCount;

		// Source offset
		vkRegion.srcOffset.x = static_cast<int32_t>(region.srcX);
		vkRegion.srcOffset.y = static_cast<int32_t>(region.srcY);
		vkRegion.srcOffset.z = static_cast<int32_t>(region.srcZ);

		// Destination subresource
		vkRegion.dstSubresource.aspectMask     = TextureAspectToVulkan(region.aspectMask);
		vkRegion.dstSubresource.mipLevel       = region.dstMipLevel;
		vkRegion.dstSubresource.baseArrayLayer = region.dstArrayLayer;
		vkRegion.dstSubresource.layerCount     = region.layerCount;

		// Destination offset
		vkRegion.dstOffset.x = static_cast<int32_t>(region.dstX);
		vkRegion.dstOffset.y = static_cast<int32_t>(region.dstY);
		vkRegion.dstOffset.z = static_cast<int32_t>(region.dstZ);

		// Extent
		vkRegion.extent.width  = region.width;
		vkRegion.extent.height = region.height;
		vkRegion.extent.depth  = region.depth;

		vkRegions.push_back(vkRegion);
	}

	vkCmdCopyImage(commandBuffer,
	               vkSrcTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	               vkDstTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	               static_cast<uint32_t>(vkRegions.size()), vkRegions.data());
}

void VulkanCommandList::BlitTexture(IRHITexture *srcTexture, IRHITexture *dstTexture, std::span<const TextureBlit> regions, FilterMode filter)
{
	if (regions.empty())
		return;

	auto *vkSrcTexture = static_cast<VulkanTexture *>(srcTexture);
	auto *vkDstTexture = static_cast<VulkanTexture *>(dstTexture);

	std::vector<VkImageBlit> vkRegions;
	vkRegions.reserve(regions.size());

	for (const auto &region : regions)
	{
		VkImageBlit vkRegion{};

		// Source subresource
		vkRegion.srcSubresource.aspectMask     = TextureAspectToVulkan(region.aspectMask);
		vkRegion.srcSubresource.mipLevel       = region.srcMipLevel;
		vkRegion.srcSubresource.baseArrayLayer = region.srcArrayLayer;
		vkRegion.srcSubresource.layerCount     = region.layerCount;

		// Source offsets (blit region)
		vkRegion.srcOffsets[0].x = static_cast<int32_t>(region.srcX0);
		vkRegion.srcOffsets[0].y = static_cast<int32_t>(region.srcY0);
		vkRegion.srcOffsets[0].z = static_cast<int32_t>(region.srcZ0);
		vkRegion.srcOffsets[1].x = static_cast<int32_t>(region.srcX1);
		vkRegion.srcOffsets[1].y = static_cast<int32_t>(region.srcY1);
		vkRegion.srcOffsets[1].z = static_cast<int32_t>(region.srcZ1);

		// Destination subresource
		vkRegion.dstSubresource.aspectMask     = TextureAspectToVulkan(region.aspectMask);
		vkRegion.dstSubresource.mipLevel       = region.dstMipLevel;
		vkRegion.dstSubresource.baseArrayLayer = region.dstArrayLayer;
		vkRegion.dstSubresource.layerCount     = region.layerCount;

		// Destination offsets (blit region)
		vkRegion.dstOffsets[0].x = static_cast<int32_t>(region.dstX0);
		vkRegion.dstOffsets[0].y = static_cast<int32_t>(region.dstY0);
		vkRegion.dstOffsets[0].z = static_cast<int32_t>(region.dstZ0);
		vkRegion.dstOffsets[1].x = static_cast<int32_t>(region.dstX1);
		vkRegion.dstOffsets[1].y = static_cast<int32_t>(region.dstY1);
		vkRegion.dstOffsets[1].z = static_cast<int32_t>(region.dstZ1);

		vkRegions.push_back(vkRegion);
	}

	vkCmdBlitImage(commandBuffer,
	               vkSrcTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
	               vkDstTexture->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	               static_cast<uint32_t>(vkRegions.size()), vkRegions.data(),
	               FilterModeToVulkan(filter));
}

void VulkanCommandList::Barrier(
    PipelineScope                      src_scope,
    PipelineScope                      dst_scope,
    std::span<const BufferTransition>  buffer_transitions,
    std::span<const TextureTransition> texture_transitions,
    std::span<const MemoryBarrier>     memory_barriers)
{
	std::vector<VkBufferMemoryBarrier2> bufferBarriers;
	std::vector<VkImageMemoryBarrier2>  imageBarriers;
	std::vector<VkMemoryBarrier2>       memBarriers;

	// Process buffer transitions
	for (const auto &transition : buffer_transitions)
	{
		if (!transition.buffer)
			continue;

		VkBufferMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.buffer              = static_cast<VulkanBuffer *>(transition.buffer)->GetHandle();
		barrier.offset              = transition.offset;
		barrier.size                = (transition.size == ~0ull) ? VK_WHOLE_SIZE : transition.size;

		VkPipelineStageFlags2 srcStages, dstStages;
		VkAccessFlags2        srcAccess, dstAccess;

		if (transition.src_stages != StageMask::Auto)
		{
			srcStages = StageMaskToVulkan2(transition.src_stages);
			srcAccess = AccessMaskToVulkan2(transition.src_access);

			// If user provided stages but not access, infer access from the state
			if (srcAccess == VK_ACCESS_2_NONE && transition.src_access == AccessMask::Auto)
			{
				VkPipelineStageFlags2 tempStages;
				GetVulkanStagesAndAccess2(transition.before, src_scope, tempStages, srcAccess);
			}
		}
		else
		{
			GetVulkanStagesAndAccess2(transition.before, src_scope, srcStages, srcAccess);
		}

		if (transition.dst_stages != StageMask::Auto)
		{
			dstStages = StageMaskToVulkan2(transition.dst_stages);
			dstAccess = AccessMaskToVulkan2(transition.dst_access);

			// If user provided stages but not access, infer access from the state
			if (dstAccess == VK_ACCESS_2_NONE && transition.dst_access == AccessMask::Auto)
			{
				VkPipelineStageFlags2 tempStages;
				GetVulkanStagesAndAccess2(transition.after, dst_scope, tempStages, dstAccess);
			}
		}
		else
		{
			GetVulkanStagesAndAccess2(transition.after, dst_scope, dstStages, dstAccess);
		}

		barrier.srcStageMask  = srcStages;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask  = dstStages;
		barrier.dstAccessMask = dstAccess;

		bufferBarriers.push_back(barrier);
	}

	// Process texture transitions
	for (const auto &transition : texture_transitions)
	{
		if (!transition.texture)
			continue;

		VkImageMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
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

		VkPipelineStageFlags2 srcStages, dstStages;
		VkAccessFlags2        srcAccess, dstAccess;

		if (transition.src_stages != StageMask::Auto)
		{
			srcStages = StageMaskToVulkan2(transition.src_stages);
			srcAccess = AccessMaskToVulkan2(transition.src_access);

			// If user provided stages but not access, infer access from the state
			if (srcAccess == VK_ACCESS_2_NONE && transition.src_access == AccessMask::Auto)
			{
				VkPipelineStageFlags2 tempStages;
				GetVulkanStagesAndAccess2(transition.before, src_scope, tempStages, srcAccess);
			}
		}
		else
		{
			GetVulkanStagesAndAccess2(transition.before, src_scope, srcStages, srcAccess);
		}

		if (transition.dst_stages != StageMask::Auto)
		{
			dstStages = StageMaskToVulkan2(transition.dst_stages);
			dstAccess = AccessMaskToVulkan2(transition.dst_access);

			// If user provided stages but not access, infer access from the state
			if (dstAccess == VK_ACCESS_2_NONE && transition.dst_access == AccessMask::Auto)
			{
				VkPipelineStageFlags2 tempStages;
				GetVulkanStagesAndAccess2(transition.after, dst_scope, tempStages, dstAccess);
			}
		}
		else
		{
			GetVulkanStagesAndAccess2(transition.after, dst_scope, dstStages, dstAccess);
		}

		barrier.srcStageMask  = srcStages;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask  = dstStages;
		barrier.dstAccessMask = dstAccess;

		imageBarriers.push_back(barrier);
	}

	// Process memory barriers
	for (const auto &memBarrier : memory_barriers)
	{
		VkMemoryBarrier2 barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;

		if (memBarrier.src_stages != StageMask::Auto)
		{
			barrier.srcStageMask  = StageMaskToVulkan2(memBarrier.src_stages);
			barrier.srcAccessMask = AccessMaskToVulkan2(memBarrier.src_access);
		}
		else
		{
			barrier.srcStageMask  = PipelineScopeToVulkanStages2(src_scope);
			barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		}

		if (memBarrier.dst_stages != StageMask::Auto)
		{
			barrier.dstStageMask  = StageMaskToVulkan2(memBarrier.dst_stages);
			barrier.dstAccessMask = AccessMaskToVulkan2(memBarrier.dst_access);
		}
		else
		{
			barrier.dstStageMask  = PipelineScopeToVulkanStages2(dst_scope);
			barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		}

		memBarriers.push_back(barrier);
	}

	if (!bufferBarriers.empty() || !imageBarriers.empty() || !memBarriers.empty())
	{
		VkDependencyInfo depInfo{};
		depInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.memoryBarrierCount       = static_cast<uint32_t>(memBarriers.size());
		depInfo.pMemoryBarriers          = memBarriers.empty() ? nullptr : memBarriers.data();
		depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
		depInfo.pBufferMemoryBarriers    = bufferBarriers.empty() ? nullptr : bufferBarriers.data();
		depInfo.imageMemoryBarrierCount  = static_cast<uint32_t>(imageBarriers.size());
		depInfo.pImageMemoryBarriers     = imageBarriers.empty() ? nullptr : imageBarriers.data();

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}
}

void VulkanCommandList::ReleaseToQueue(
    QueueType                          dstQueue,
    std::span<const BufferTransition>  buffer_transitions,
    std::span<const TextureTransition> texture_transitions)
{
	uint32_t dstQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	switch (dstQueue)
	{
		case QueueType::GRAPHICS:
			dstQueueFamily = graphicsQueueFamily;
			break;
		case QueueType::COMPUTE:
			dstQueueFamily = computeQueueFamily;
			break;
		case QueueType::TRANSFER:
			dstQueueFamily = transferQueueFamily;
			break;
	}

	PipelineScope currentScope = PipelineScope::All;
	switch (queueType)
	{
		case QueueType::GRAPHICS:
			currentScope = PipelineScope::Graphics;
			break;
		case QueueType::COMPUTE:
			currentScope = PipelineScope::Compute;
			break;
		case QueueType::TRANSFER:
			currentScope = PipelineScope::Copy;
			break;
	}

	std::vector<VkBufferMemoryBarrier2> bufferBarriers;
	std::vector<VkImageMemoryBarrier2>  imageBarriers;

	// Process buffer transitions for release
	for (const auto &transition : buffer_transitions)
	{
		if (!transition.buffer)
			continue;

		VkBufferMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		barrier.srcQueueFamilyIndex = queueFamily;
		barrier.dstQueueFamilyIndex = dstQueueFamily;
		barrier.buffer              = static_cast<VulkanBuffer *>(transition.buffer)->GetHandle();
		barrier.offset              = transition.offset;
		barrier.size                = (transition.size == ~0ull) ? VK_WHOLE_SIZE : transition.size;

		// For release, we specify the source access based on the 'before' state
		VkPipelineStageFlags2 srcStages;
		VkAccessFlags2        srcAccess;
		GetVulkanStagesAndAccess2(transition.before, currentScope, srcStages, srcAccess);

		barrier.srcStageMask  = srcStages;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask  = VK_PIPELINE_STAGE_2_NONE;
		barrier.dstAccessMask = VK_ACCESS_2_NONE;

		bufferBarriers.push_back(barrier);
	}

	// Process texture transitions for release
	for (const auto &transition : texture_transitions)
	{
		if (!transition.texture)
			continue;

		VkImageMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcQueueFamilyIndex = queueFamily;
		barrier.dstQueueFamilyIndex = dstQueueFamily;
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

		// For release, we specify the source access based on the 'before' state
		VkPipelineStageFlags2 srcStages;
		VkAccessFlags2        srcAccess;
		GetVulkanStagesAndAccess2(transition.before, currentScope, srcStages, srcAccess);

		barrier.srcStageMask  = srcStages;
		barrier.srcAccessMask = srcAccess;
		barrier.dstStageMask  = VK_PIPELINE_STAGE_2_NONE;
		barrier.dstAccessMask = VK_ACCESS_2_NONE;

		imageBarriers.push_back(barrier);
	}

	// Issue the pipeline barrier for release
	if (!bufferBarriers.empty() || !imageBarriers.empty())
	{
		VkDependencyInfo depInfo{};
		depInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
		depInfo.pBufferMemoryBarriers    = bufferBarriers.empty() ? nullptr : bufferBarriers.data();
		depInfo.imageMemoryBarrierCount  = static_cast<uint32_t>(imageBarriers.size());
		depInfo.pImageMemoryBarriers     = imageBarriers.empty() ? nullptr : imageBarriers.data();

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}
}

void VulkanCommandList::AcquireFromQueue(
    QueueType                          srcQueue,
    std::span<const BufferTransition>  buffer_transitions,
    std::span<const TextureTransition> texture_transitions)
{
	uint32_t srcQueueFamily = VK_QUEUE_FAMILY_IGNORED;
	switch (srcQueue)
	{
		case QueueType::GRAPHICS:
			srcQueueFamily = graphicsQueueFamily;
			break;
		case QueueType::COMPUTE:
			srcQueueFamily = computeQueueFamily;
			break;
		case QueueType::TRANSFER:
			srcQueueFamily = transferQueueFamily;
			break;
	}

	PipelineScope currentScope = PipelineScope::All;
	switch (queueType)
	{
		case QueueType::GRAPHICS:
			currentScope = PipelineScope::Graphics;
			break;
		case QueueType::COMPUTE:
			currentScope = PipelineScope::Compute;
			break;
		case QueueType::TRANSFER:
			currentScope = PipelineScope::Copy;
			break;
	}

	std::vector<VkBufferMemoryBarrier2> bufferBarriers;
	std::vector<VkImageMemoryBarrier2>  imageBarriers;

	// Process buffer transitions for acquire
	for (const auto &transition : buffer_transitions)
	{
		if (!transition.buffer)
			continue;

		VkBufferMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		barrier.srcQueueFamilyIndex = srcQueueFamily;
		barrier.dstQueueFamilyIndex = queueFamily;
		barrier.buffer              = static_cast<VulkanBuffer *>(transition.buffer)->GetHandle();
		barrier.offset              = transition.offset;
		barrier.size                = (transition.size == ~0ull) ? VK_WHOLE_SIZE : transition.size;

		// For acquire, we specify the destination access based on the 'after' state
		VkPipelineStageFlags2 dstStages;
		VkAccessFlags2        dstAccess;
		GetVulkanStagesAndAccess2(transition.after, currentScope, dstStages, dstAccess);

		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
		barrier.srcAccessMask = VK_ACCESS_2_NONE;
		barrier.dstStageMask  = dstStages;
		barrier.dstAccessMask = dstAccess;

		bufferBarriers.push_back(barrier);
	}

	// Process texture transitions for acquire
	for (const auto &transition : texture_transitions)
	{
		if (!transition.texture)
			continue;

		VkImageMemoryBarrier2 barrier{};
		barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcQueueFamilyIndex = srcQueueFamily;
		barrier.dstQueueFamilyIndex = queueFamily;
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

		// For acquire, we specify the destination access based on the 'after' state
		VkPipelineStageFlags2 dstStages;
		VkAccessFlags2        dstAccess;
		GetVulkanStagesAndAccess2(transition.after, currentScope, dstStages, dstAccess);

		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_NONE;
		barrier.srcAccessMask = VK_ACCESS_2_NONE;
		barrier.dstStageMask  = dstStages;
		barrier.dstAccessMask = dstAccess;

		imageBarriers.push_back(barrier);
	}

	// Issue the pipeline barrier for acquire
	if (!bufferBarriers.empty() || !imageBarriers.empty())
	{
		VkDependencyInfo depInfo{};
		depInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
		depInfo.pBufferMemoryBarriers    = bufferBarriers.empty() ? nullptr : bufferBarriers.data();
		depInfo.imageMemoryBarrierCount  = static_cast<uint32_t>(imageBarriers.size());
		depInfo.pImageMemoryBarriers     = imageBarriers.empty() ? nullptr : imageBarriers.data();

		vkCmdPipelineBarrier2(commandBuffer, &depInfo);
	}
}

void VulkanCommandList::ResetQueryPool(IRHIQueryPool *queryPool, uint32_t firstQuery, uint32_t queryCount)
{
	auto *vkQueryPool = static_cast<VulkanQueryPool *>(queryPool);
	vkCmdResetQueryPool(commandBuffer, vkQueryPool->GetHandle(), firstQuery, queryCount);
}

void VulkanCommandList::WriteTimestamp(IRHIQueryPool *queryPool, uint32_t query, StageMask stage)
{
	auto *vkQueryPool = static_cast<VulkanQueryPool *>(queryPool);

	VkPipelineStageFlags2 vkStage = StageMaskToVulkan2(stage);
	vkCmdWriteTimestamp2(commandBuffer, vkStage, vkQueryPool->GetHandle(), query);
}

void VulkanCommandList::BeginQuery(IRHIQueryPool *queryPool, uint32_t query)
{
	auto *vkQueryPool = static_cast<VulkanQueryPool *>(queryPool);
	vkCmdBeginQuery(commandBuffer, vkQueryPool->GetHandle(), query, 0);
}

void VulkanCommandList::EndQuery(IRHIQueryPool *queryPool, uint32_t query)
{
	auto *vkQueryPool = static_cast<VulkanQueryPool *>(queryPool);
	vkCmdEndQuery(commandBuffer, vkQueryPool->GetHandle(), query);
}

void VulkanCommandList::CopyQueryPoolResults(
    IRHIQueryPool   *queryPool,
    uint32_t         firstQuery,
    uint32_t         queryCount,
    IRHIBuffer      *dstBuffer,
    size_t           dstOffset,
    size_t           stride,
    QueryResultFlags flags)
{
	auto              *vkQueryPool = static_cast<VulkanQueryPool *>(queryPool);
	auto              *vkBuffer    = static_cast<VulkanBuffer *>(dstBuffer);
	VkQueryResultFlags vkFlags     = QueryResultFlagsToVulkan(flags) | VK_QUERY_RESULT_64_BIT;

	vkCmdCopyQueryPoolResults(
	    commandBuffer,
	    vkQueryPool->GetHandle(),
	    firstQuery,
	    queryCount,
	    vkBuffer->GetHandle(),
	    dstOffset,
	    stride,
	    vkFlags);
}

}        // namespace rhi::vulkan