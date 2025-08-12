#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "rhi.h"

namespace RHI
{

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
class VulkanDescriptorSetLayout;
class VulkanDescriptorSet;
class VulkanSampler;

// Vulkan Buffer implementation
class VulkanBuffer : public IRHIBuffer
{
  private:
	VmaAllocator  allocator;
	VkBuffer      buffer;
	VmaAllocation allocation;
	size_t        size;
	void         *mappedData;

  public:
	VulkanBuffer(VmaAllocator allocator, const BufferDesc &desc);
	~VulkanBuffer() override;

	void  *Map() override;
	void   Unmap() override;
	size_t GetSize() const override;

	VkBuffer GetHandle() const
	{
		return buffer;
	}
};

// Vulkan Texture implementation
class VulkanTexture : public IRHITexture
{
  private:
	VkDevice      device;
	VmaAllocator  allocator;
	VkImage       image;
	VkImageView   imageView;
	VmaAllocation allocation;
	uint32_t      width;
	uint32_t      height;
	TextureFormat format;
	bool          ownedBySwapchain;

  public:
	VulkanTexture(VkDevice device, VmaAllocator allocator, VkImage image, VkFormat format, uint32_t width,
	              uint32_t height, bool ownedBySwapchain = false);
	VulkanTexture(VkDevice device, VmaAllocator allocator, const TextureDesc &desc);
	~VulkanTexture() override;

	uint32_t GetWidth() const override
	{
		return width;
	}
	uint32_t GetHeight() const override
	{
		return height;
	}
	TextureFormat GetFormat() const override
	{
		return format;
	}

	VkImage GetHandle() const
	{
		return image;
	}
	VkImageView GetImageView() const
	{
		return imageView;
	}
};

// Vulkan Shader implementation
class VulkanShader : public IRHIShader
{
  private:
	VkDevice       device;
	VkShaderModule shaderModule;
	ShaderStage    stage;

  public:
	VulkanShader(VkDevice device, const ShaderDesc &desc);
	~VulkanShader() override;

	ShaderStage GetStage() const override
	{
		return stage;
	}
	VkShaderModule GetHandle() const
	{
		return shaderModule;
	}
};

// Vulkan Pipeline implementation
class VulkanPipeline : public IRHIPipeline
{
  private:
	VkDevice         device;
	VkPipeline       pipeline;
	VkPipelineLayout pipelineLayout;
	VkRenderPass     renderPass;

  public:
	VulkanPipeline(VkDevice device, const GraphicsPipelineDesc &desc);
	~VulkanPipeline() override;

	VkPipeline GetHandle() const
	{
		return pipeline;
	}
	VkPipelineLayout GetLayout() const
	{
		return pipelineLayout;
	}
	VkRenderPass GetRenderPass() const
	{
		return renderPass;
	}
};

// Vulkan Command List implementation
class VulkanCommandList : public IRHICommandList
{
  private:
	VkDevice        device;
	VkCommandBuffer commandBuffer;
	VkRenderPass    currentRenderPass;
	VkFramebuffer   currentFramebuffer;
	VulkanPipeline *currentPipeline;
	bool            inRenderPass;

  public:
	VulkanCommandList(VkDevice device, VkCommandPool commandPool);
	~VulkanCommandList() override;

	void Begin() override;
	void End() override;
	void Reset() override;

	void BeginRenderPass(const RenderPassBeginInfo &info) override;
	void EndRenderPass() override;

	void SetPipeline(IRHIPipeline *pipeline) override;
	void SetVertexBuffer(uint32_t binding, IRHIBuffer *buffer, size_t offset = 0) override;
	void BindIndexBuffer(IRHIBuffer *buffer, size_t offset = 0) override;
	void BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
	                       const uint32_t *dynamicOffsets = nullptr, uint32_t dynamicOffsetCount = 0) override;
	void PushConstants(ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *data) override;
	void SetViewport(float x, float y, float width, float height) override;
	void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

	void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;
	void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) override;
	void DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(DrawIndexedIndirectCommand)) override;

	VkCommandBuffer GetHandle() const
	{
		return commandBuffer;
	}
};

// Vulkan Swapchain implementation
class VulkanSwapchain : public IRHISwapchain
{
  private:
	VkDevice                                    device;
	VkPhysicalDevice                            physicalDevice;
	VmaAllocator                                allocator;
	VkSurfaceKHR                                surface;
	VkQueue                                     graphicsQueue;
	VkSwapchainKHR                              swapchain;
	std::vector<VkImage>                        swapchainImages;
	std::vector<std::unique_ptr<VulkanTexture>> backBuffers;
	std::vector<VkFramebuffer>                  framebuffers;
	VkRenderPass                                renderPass;
	VkFormat                                    swapchainFormat;
	VkExtent2D                                  swapchainExtent;
	VkSurfaceFormatKHR                          chosenSurfaceFormat;
	VkPresentModeKHR                            chosenPresentMode;
	uint32_t                                    requestedBufferCount;

  public:
	VulkanSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkSurfaceKHR surface,
	                VkQueue graphicsQueue, const SwapchainDesc &desc);
	~VulkanSwapchain() override;

	SwapchainStatus AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore = nullptr) override;
	SwapchainStatus Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore = nullptr) override;
	IRHITexture *GetBackBuffer(uint32_t index) override;
	uint32_t     GetImageCount() const override;
	void         Resize(uint32_t width, uint32_t height) override;

	VkFramebuffer GetFramebuffer(uint32_t index, VkRenderPass renderPass);
};

// Vulkan Semaphore implementation
class VulkanSemaphore : public IRHISemaphore
{
  private:
	VkDevice    device;
	VkSemaphore semaphore;

  public:
	VulkanSemaphore(VkDevice device);
	~VulkanSemaphore() override;

	VkSemaphore GetHandle() const
	{
		return semaphore;
	}
};

// Vulkan Fence implementation
class VulkanFence : public IRHIFence
{
  private:
	VkDevice device;
	VkFence  fence;

  public:
	VulkanFence(VkDevice device, bool signaled = false);
	~VulkanFence() override;

	void Wait(uint64_t timeout = UINT64_MAX) override;
	void Reset() override;
	bool IsSignaled() const override;

	VkFence GetHandle() const
	{
		return fence;
	}
};

// Vulkan DescriptorSetLayout implementation
class VulkanDescriptorSetLayout : public IRHIDescriptorSetLayout
{
  private:
	VkDevice                          device;
	VkDescriptorSetLayout             layout;
	std::vector<VkDescriptorPoolSize> poolSizes;

  public:
	VulkanDescriptorSetLayout(VkDevice device, const DescriptorSetLayoutDesc &desc);
	~VulkanDescriptorSetLayout() override;

	VkDescriptorSetLayout GetHandle() const
	{
		return layout;
	}
	const std::vector<VkDescriptorPoolSize> &GetPoolSizes() const
	{
		return poolSizes;
	}
};

// Vulkan DescriptorSet implementation
class VulkanDescriptorSet : public IRHIDescriptorSet
{
  private:
	VkDevice                   device;
	VkDescriptorSet            descriptorSet;
	VkDescriptorPool           sourcePool;
	VulkanDescriptorSetLayout *layout;

  public:
	VulkanDescriptorSet(VkDevice device, VulkanDescriptorSetLayout *layout, VkDescriptorPool pool, VkDescriptorSet set);
	~VulkanDescriptorSet() override;

	void BindBuffer(uint32_t binding, const BufferBinding &bufferBinding) override;
	void BindTexture(uint32_t binding, const TextureBinding &textureBinding) override;

	VkDescriptorSet GetHandle() const
	{
		return descriptorSet;
	}
};

// Vulkan Sampler implementation
class VulkanSampler : public IRHISampler
{
  private:
	VkDevice  device;
	VkSampler sampler;

  public:
	VulkanSampler(VkDevice device);
	~VulkanSampler() override;

	VkSampler GetHandle() const
	{
		return sampler;
	}
};

// Utility functions
VkFormat           TextureFormatToVulkan(TextureFormat format);
TextureFormat      VulkanFormatToTexture(VkFormat format);
VkBufferUsageFlags BufferUsageToVulkan(BufferUsage usage);
VkDescriptorType   DescriptorTypeToVulkan(DescriptorType type);
VkShaderStageFlags ShaderStageFlagsToVulkan(ShaderStageFlags flags);
VkImageLayout      ImageLayoutToVulkan(ImageLayout layout);
uint32_t           FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);

}        // namespace RHI