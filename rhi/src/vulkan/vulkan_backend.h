#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "rhi.h"

namespace RHI {

// Forward declarations of Vulkan classes
class VulkanDevice;
class VulkanBuffer;
class VulkanTexture;
class VulkanShader;
class VulkanPipeline;
class VulkanCommandList;
class VulkanSwapchain;
class VulkanSemaphore;
class VulkanFence;

// Vulkan Buffer implementation
class VulkanBuffer : public IRHIBuffer {
  private:
    VkDevice device;
    VkBuffer buffer;
    VkDeviceMemory memory;
    size_t size;
    void* mappedData;

  public:
    VulkanBuffer(VkDevice device, VkPhysicalDevice physicalDevice, const BufferDesc& desc);
    ~VulkanBuffer() override;

    void* Map() override;
    void Unmap() override;
    size_t GetSize() const override;

    VkBuffer GetHandle() const { return buffer; }
};

// Vulkan Texture implementation
class VulkanTexture : public IRHITexture {
  private:
    VkDevice device;
    VkImage image;
    VkImageView imageView;
    VkDeviceMemory memory;
    uint32_t width;
    uint32_t height;
    TextureFormat format;
    bool ownedBySwapchain;

  public:
    VulkanTexture(VkDevice device, VkImage image, VkFormat format, uint32_t width, uint32_t height,
                  bool ownedBySwapchain = false);
    ~VulkanTexture() override;

    uint32_t GetWidth() const override { return width; }
    uint32_t GetHeight() const override { return height; }
    TextureFormat GetFormat() const override { return format; }

    VkImage GetHandle() const { return image; }
    VkImageView GetImageView() const { return imageView; }
};

// Vulkan Shader implementation
class VulkanShader : public IRHIShader {
  private:
    VkDevice device;
    VkShaderModule shaderModule;
    ShaderStage stage;

  public:
    VulkanShader(VkDevice device, const ShaderDesc& desc);
    ~VulkanShader() override;

    ShaderStage GetStage() const override { return stage; }
    VkShaderModule GetHandle() const { return shaderModule; }
};

// Vulkan Pipeline implementation
class VulkanPipeline : public IRHIPipeline {
  private:
    VkDevice device;
    VkPipeline pipeline;
    VkPipelineLayout pipelineLayout;
    VkRenderPass renderPass;

  public:
    VulkanPipeline(VkDevice device, const GraphicsPipelineDesc& desc);
    ~VulkanPipeline() override;

    VkPipeline GetHandle() const { return pipeline; }
    VkPipelineLayout GetLayout() const { return pipelineLayout; }
    VkRenderPass GetRenderPass() const { return renderPass; }
};

// Vulkan Command List implementation
class VulkanCommandList : public IRHICommandList {
  private:
    VkDevice device;
    VkCommandBuffer commandBuffer;
    VkRenderPass currentRenderPass;
    VkFramebuffer currentFramebuffer;
    VulkanPipeline* currentPipeline;
    bool inRenderPass;

  public:
    VulkanCommandList(VkDevice device, VkCommandPool commandPool);
    ~VulkanCommandList() override;

    void Begin() override;
    void End() override;
    void Reset() override;

    void BeginRenderPass(const RenderPassBeginInfo& info) override;
    void EndRenderPass() override;

    void SetPipeline(IRHIPipeline* pipeline) override;
    void SetVertexBuffer(uint32_t binding, IRHIBuffer* buffer, size_t offset = 0) override;
    void SetViewport(float x, float y, float width, float height) override;
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

    void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;

    VkCommandBuffer GetHandle() const { return commandBuffer; }
};

// Vulkan Swapchain implementation
class VulkanSwapchain : public IRHISwapchain {
  private:
    VkDevice device;
    VkPhysicalDevice physicalDevice;
    VkSurfaceKHR surface;
    VkQueue graphicsQueue;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<std::unique_ptr<VulkanTexture>> backBuffers;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;

  public:
    VulkanSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkQueue graphicsQueue,
                    const SwapchainDesc& desc);
    ~VulkanSwapchain() override;

    uint32_t AcquireNextImage(IRHISemaphore* signalSemaphore = nullptr) override;
    void Present(uint32_t imageIndex, IRHISemaphore* waitSemaphore = nullptr) override;
    IRHITexture* GetBackBuffer(uint32_t index) override;
    uint32_t GetImageCount() const override;
    void Resize(uint32_t width, uint32_t height) override;
    
    VkFramebuffer GetFramebuffer(uint32_t index, VkRenderPass renderPass);
};

// Vulkan Semaphore implementation
class VulkanSemaphore : public IRHISemaphore {
  private:
    VkDevice device;
    VkSemaphore semaphore;

  public:
    VulkanSemaphore(VkDevice device);
    ~VulkanSemaphore() override;

    VkSemaphore GetHandle() const { return semaphore; }
};

// Vulkan Fence implementation
class VulkanFence : public IRHIFence {
  private:
    VkDevice device;
    VkFence fence;

  public:
    VulkanFence(VkDevice device, bool signaled = false);
    ~VulkanFence() override;

    void Wait(uint64_t timeout = UINT64_MAX) override;
    void Reset() override;
    bool IsSignaled() const override;

    VkFence GetHandle() const { return fence; }
};

// Utility functions
VkFormat TextureFormatToVulkan(TextureFormat format);
TextureFormat VulkanFormatToTexture(VkFormat format);
VkBufferUsageFlags BufferUsageToVulkan(BufferUsage usage);
uint32_t FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

}  // namespace RHI