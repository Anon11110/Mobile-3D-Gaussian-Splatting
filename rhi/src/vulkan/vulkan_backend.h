#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <memory>
#include <vector>

#include "rhi.h"

namespace rhi::vulkan
{

// Forward declarations of Vulkan classes
class VulkanDevice;
class VulkanBuffer;
class VulkanTexture;
class VulkanTextureView;
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

	VulkanBuffer(const VulkanBuffer &)            = delete;
	VulkanBuffer &operator=(const VulkanBuffer &) = delete;
	VulkanBuffer(VulkanBuffer &&other) noexcept;
	VulkanBuffer &operator=(VulkanBuffer &&other) noexcept;

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
	uint32_t      depth;
	uint32_t      mipLevels;
	uint32_t      arrayLayers;
	TextureFormat format;
	bool          ownedBySwapchain;

  public:
	VulkanTexture(VkDevice device, VmaAllocator allocator, VkImage image, VkFormat format, uint32_t width,
	              uint32_t height, bool ownedBySwapchain = false);
	VulkanTexture(VkDevice device, VmaAllocator allocator, const TextureDesc &desc);
	~VulkanTexture() override;

	VulkanTexture(const VulkanTexture &)            = delete;
	VulkanTexture &operator=(const VulkanTexture &) = delete;
	VulkanTexture(VulkanTexture &&other) noexcept;
	VulkanTexture &operator=(VulkanTexture &&other) noexcept;

	uint32_t GetWidth() const override
	{
		return width;
	}
	uint32_t GetHeight() const override
	{
		return height;
	}
	uint32_t GetDepth() const override
	{
		return depth;
	}
	uint32_t GetMipLevels() const override
	{
		return mipLevels;
	}
	uint32_t GetArrayLayers() const override
	{
		return arrayLayers;
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

// Vulkan Texture View implementation
class VulkanTextureView : public IRHITextureView
{
  private:
	VkDevice       device;
	VkImageView    imageView;
	VulkanTexture *texture;
	TextureFormat  format;
	uint32_t       baseMipLevel;
	uint32_t       mipLevelCount;
	uint32_t       baseArrayLayer;
	uint32_t       arrayLayerCount;

  public:
	VulkanTextureView(VkDevice device, const TextureViewDesc &desc);
	~VulkanTextureView() override;

	VulkanTextureView(const VulkanTextureView &)            = delete;
	VulkanTextureView &operator=(const VulkanTextureView &) = delete;
	VulkanTextureView(VulkanTextureView &&other) noexcept;
	VulkanTextureView &operator=(VulkanTextureView &&other) noexcept;

	IRHITexture *GetTexture() override
	{
		return texture;
	}
	TextureFormat GetFormat() const override
	{
		return format;
	}
	uint32_t GetWidth() const override;
	uint32_t GetHeight() const override;
	uint32_t GetBaseMipLevel() const override
	{
		return baseMipLevel;
	}
	uint32_t GetMipLevelCount() const override
	{
		return mipLevelCount;
	}
	uint32_t GetBaseArrayLayer() const override
	{
		return baseArrayLayer;
	}
	uint32_t GetArrayLayerCount() const override
	{
		return arrayLayerCount;
	}

	VkImageView GetHandle() const
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

	VulkanShader(const VulkanShader &)            = delete;
	VulkanShader &operator=(const VulkanShader &) = delete;
	VulkanShader(VulkanShader &&other) noexcept;
	VulkanShader &operator=(VulkanShader &&other) noexcept;

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
	VkDevice              device;
	VkPipeline            pipeline;
	VkPipelineLayout      pipelineLayout;
	RenderTargetSignature targetSignature;

  public:
	VulkanPipeline(VkDevice device, const GraphicsPipelineDesc &desc);
	~VulkanPipeline() override;

	VulkanPipeline(const VulkanPipeline &)            = delete;
	VulkanPipeline &operator=(const VulkanPipeline &) = delete;
	VulkanPipeline(VulkanPipeline &&other) noexcept;
	VulkanPipeline &operator=(VulkanPipeline &&other) noexcept;

	VkPipeline GetHandle() const
	{
		return pipeline;
	}
	VkPipelineLayout GetLayout() const
	{
		return pipelineLayout;
	}
	const RenderTargetSignature &GetTargetSignature() const
	{
		return targetSignature;
	}
};

// Vulkan Command List implementation
class VulkanCommandList : public IRHICommandList
{
  private:
	VkDevice        device;
	VkCommandBuffer commandBuffer;
	VulkanPipeline *currentPipeline;
	bool            inRendering;

	// Cached function pointers
	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
	PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR;

  public:
	VulkanCommandList(VkDevice device, VkCommandPool commandPool,
	                  PFN_vkCmdBeginRenderingKHR beginFunc,
	                  PFN_vkCmdEndRenderingKHR   endFunc);
	~VulkanCommandList() override;

	VulkanCommandList(const VulkanCommandList &)            = delete;
	VulkanCommandList &operator=(const VulkanCommandList &) = delete;
	VulkanCommandList(VulkanCommandList &&)                 = delete;
	VulkanCommandList &operator=(VulkanCommandList &&)      = delete;

	void Begin() override;
	void End() override;
	void Reset() override;

	void BeginRendering(const RenderingInfo &info) override;
	void EndRendering() override;

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

	void Barrier(
	    PipelineScope                         src_scope,
	    PipelineScope                         dst_scope,
	    const std::vector<BufferTransition>  &buffer_transitions,
	    const std::vector<TextureTransition> &texture_transitions,
	    const std::vector<MemoryBarrier>     &memory_barriers = {}) override;

	VkCommandBuffer GetHandle() const
	{
		return commandBuffer;
	}
};

// Vulkan Swapchain implementation
class VulkanSwapchain : public IRHISwapchain
{
  private:
	VkDevice                                        device;
	VkPhysicalDevice                                physicalDevice;
	VmaAllocator                                    allocator;
	VkSurfaceKHR                                    surface;
	VkQueue                                         graphicsQueue;
	VkSwapchainKHR                                  swapchain;
	std::vector<VkImage>                            swapchainImages;
	std::vector<std::unique_ptr<VulkanTexture>>     backBuffers;
	std::vector<std::unique_ptr<VulkanTextureView>> backBufferViews;
	std::vector<VkFramebuffer>                      framebuffers;
	VkRenderPass                                    renderPass;
	VkFormat                                        swapchainFormat;
	VkExtent2D                                      swapchainExtent;
	VkSurfaceFormatKHR                              chosenSurfaceFormat;
	VkPresentModeKHR                                chosenPresentMode;
	uint32_t                                        requestedBufferCount;

  public:
	VulkanSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkSurfaceKHR surface,
	                VkQueue graphicsQueue, const SwapchainDesc &desc);
	~VulkanSwapchain() override;

	VulkanSwapchain(const VulkanSwapchain &)            = delete;
	VulkanSwapchain &operator=(const VulkanSwapchain &) = delete;
	VulkanSwapchain(VulkanSwapchain &&)                 = delete;
	VulkanSwapchain &operator=(VulkanSwapchain &&)      = delete;

	SwapchainStatus  AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore = nullptr) override;
	SwapchainStatus  Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore = nullptr) override;
	IRHITexture     *GetBackBuffer(uint32_t index) override;
	IRHITextureView *GetBackBufferView(uint32_t index) override;
	uint32_t         GetImageCount() const override;
	void             Resize(uint32_t width, uint32_t height) override;

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

	VulkanSemaphore(const VulkanSemaphore &)            = delete;
	VulkanSemaphore &operator=(const VulkanSemaphore &) = delete;
	VulkanSemaphore(VulkanSemaphore &&other) noexcept;
	VulkanSemaphore &operator=(VulkanSemaphore &&other) noexcept;

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

	VulkanFence(const VulkanFence &)            = delete;
	VulkanFence &operator=(const VulkanFence &) = delete;
	VulkanFence(VulkanFence &&other) noexcept;
	VulkanFence &operator=(VulkanFence &&other) noexcept;

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

	VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout &)            = delete;
	VulkanDescriptorSetLayout &operator=(const VulkanDescriptorSetLayout &) = delete;
	VulkanDescriptorSetLayout(VulkanDescriptorSetLayout &&other) noexcept;
	VulkanDescriptorSetLayout &operator=(VulkanDescriptorSetLayout &&other) noexcept;

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

	VulkanDescriptorSet(const VulkanDescriptorSet &)            = delete;
	VulkanDescriptorSet &operator=(const VulkanDescriptorSet &) = delete;
	VulkanDescriptorSet(VulkanDescriptorSet &&other) noexcept;
	VulkanDescriptorSet &operator=(VulkanDescriptorSet &&other) noexcept;

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

	VulkanSampler(const VulkanSampler &)            = delete;
	VulkanSampler &operator=(const VulkanSampler &) = delete;
	VulkanSampler(VulkanSampler &&other) noexcept;
	VulkanSampler &operator=(VulkanSampler &&other) noexcept;

	VkSampler GetHandle() const
	{
		return sampler;
	}
};

// Utility functions
VkFormat              TextureFormatToVulkan(TextureFormat format);
VkFormat              VertexFormatToVulkan(VertexFormat format);
TextureFormat         VulkanFormatToTexture(VkFormat format);
VkBufferUsageFlags    BufferUsageToVulkan(BufferUsage usage);
VkDescriptorType      DescriptorTypeToVulkan(DescriptorType type);
VkShaderStageFlags    ShaderStageFlagsToVulkan(ShaderStageFlags flags);
VkImageLayout         ImageLayoutToVulkan(ImageLayout layout);
VkPrimitiveTopology   PrimitiveTopologyToVulkan(PrimitiveTopology topology);
VkPolygonMode         PolygonModeToVulkan(PolygonMode mode);
VkCullModeFlags       CullModeToVulkan(CullMode mode);
VkFrontFace           FrontFaceToVulkan(FrontFace face);
VkAttachmentLoadOp    LoadOpToVulkan(LoadOp op);
VkAttachmentStoreOp   StoreOpToVulkan(StoreOp op);
VkCompareOp           CompareOpToVulkan(CompareOp op);
VkStencilOp           StencilOpToVulkan(StencilOp op);
VkBlendFactor         BlendFactorToVulkan(BlendFactor factor);
VkBlendOp             BlendOpToVulkan(BlendOp op);
VkSampleCountFlagBits SampleCountToVulkan(SampleCount count);
uint32_t              FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
VkPipelineStageFlags  PipelineScopeToVulkanStages(PipelineScope scope);
VkPipelineStageFlags  StageMaskToVulkan(StageMask mask);
VkAccessFlags         AccessMaskToVulkan(AccessMask mask);
void                  GetVulkanStagesAndAccess(ResourceState state, PipelineScope scope,
                                               VkPipelineStageFlags &stages, VkAccessFlags &access);
VkImageLayout         ResourceStateToImageLayout(ResourceState state);

}        // namespace rhi::vulkan