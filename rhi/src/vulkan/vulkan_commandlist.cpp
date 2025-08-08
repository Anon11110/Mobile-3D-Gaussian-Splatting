#include "vulkan_backend.h"
#include <stdexcept>

namespace RHI {

VulkanCommandList::VulkanCommandList(VkDevice device, VkCommandPool commandPool)
    : device(device), commandBuffer(VK_NULL_HANDLE), currentRenderPass(VK_NULL_HANDLE), 
      currentFramebuffer(VK_NULL_HANDLE), inRenderPass(false) {
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate Vulkan command buffer");
    }
}

VulkanCommandList::~VulkanCommandList() {
    // Command buffers are automatically freed when command pool is destroyed
}

void VulkanCommandList::Begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer");
    }
}

void VulkanCommandList::End() {
    if (inRenderPass) {
        EndRenderPass();
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }
}

void VulkanCommandList::Reset() {
    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset command buffer");
    }
    inRenderPass = false;
    currentRenderPass = VK_NULL_HANDLE;
    currentFramebuffer = VK_NULL_HANDLE;
}

void VulkanCommandList::BeginRenderPass(const RenderPassBeginInfo& info) {
    auto* colorTexture = static_cast<VulkanTexture*>(info.colorTarget);

    // Create render pass on-demand (simplified for triangle example)
    if (currentRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = TextureFormatToVulkan(colorTexture->GetFormat());
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = info.shouldClearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef{};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &colorAttachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &currentRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create render pass");
        }
    }

    // Create framebuffer on-demand
    if (currentFramebuffer == VK_NULL_HANDLE) {
        VkImageView attachments[] = { colorTexture->GetImageView() };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = currentRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = info.width;
        framebufferInfo.height = info.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &currentFramebuffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }

    // Begin render pass
    VkRenderPassBeginInfo renderPassBegin{};
    renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBegin.renderPass = currentRenderPass;
    renderPassBegin.framebuffer = currentFramebuffer;
    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = {info.width, info.height};

    VkClearValue clearColor = {};
    clearColor.color = {{info.clearColor.color[0], info.clearColor.color[1], info.clearColor.color[2], info.clearColor.color[3]}};
    renderPassBegin.clearValueCount = 1;
    renderPassBegin.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
    inRenderPass = true;
}

void VulkanCommandList::EndRenderPass() {
    if (inRenderPass) {
        vkCmdEndRenderPass(commandBuffer);
        inRenderPass = false;
    }
    
    // Clean up framebuffer (recreate each frame)
    if (currentFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, currentFramebuffer, nullptr);
        currentFramebuffer = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::SetPipeline(IRHIPipeline* pipeline) {
    auto* vkPipeline = static_cast<VulkanPipeline*>(pipeline);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetHandle());
}

void VulkanCommandList::SetVertexBuffer(uint32_t binding, IRHIBuffer* buffer, size_t offset) {
    auto* vkBuffer = static_cast<VulkanBuffer*>(buffer);
    VkBuffer vertexBuffers[] = { vkBuffer->GetHandle() };
    VkDeviceSize offsets[] = { static_cast<VkDeviceSize>(offset) };
    vkCmdBindVertexBuffers(commandBuffer, binding, 1, vertexBuffers, offsets);
}

void VulkanCommandList::SetViewport(float x, float y, float width, float height) {
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = width;
    viewport.height = height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
}

void VulkanCommandList::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {width, height};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t firstVertex) {
    vkCmdDraw(commandBuffer, vertexCount, 1, firstVertex, 0);
}

} // namespace RHI