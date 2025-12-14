#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <vector>

#include "../../../include/rhi/rhi.h"

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
class VulkanBuffer final : public RefCounter<IRHIBuffer>
{
  private:
	VmaAllocator  allocator;
	VkBuffer      buffer;
	VmaAllocation allocation;
	size_t        size;
	void         *mappedData;
	bool          isPersistentlyMapped;
	IndexType     indexType;        // Store index type for index buffers

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
	bool   IsMappable() const;

	[[nodiscard]] VkBuffer GetHandle() const noexcept
	{
		return buffer;
	}

	[[nodiscard]] VmaAllocation GetAllocation() const noexcept
	{
		return allocation;
	}

	[[nodiscard]] IndexType GetIndexType() const noexcept
	{
		return indexType;
	}
};

// Vulkan Texture implementation
class VulkanTexture final : public RefCounter<IRHITexture>
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

	[[nodiscard]] VkImage GetHandle() const noexcept
	{
		return image;
	}
	[[nodiscard]] VkImageView GetImageView() const noexcept
	{
		return imageView;
	}
};

// Vulkan Texture View implementation
class VulkanTextureView final : public RefCounter<IRHITextureView>
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

	[[nodiscard]] VkImageView GetHandle() const
	{
		return imageView;
	}
};

// Vulkan Shader implementation
class VulkanShader final : public RefCounter<IRHIShader>
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
	[[nodiscard]] VkShaderModule GetHandle() const
	{
		return shaderModule;
	}
};

// Vulkan Pipeline implementation
class VulkanPipeline final : public RefCounter<IRHIPipeline>
{
  private:
	VkDevice              device;
	VkPipeline            pipeline;
	VkPipelineLayout      pipelineLayout;
	PipelineType          pipelineType;
	RenderTargetSignature targetSignature;

  public:
	VulkanPipeline(VkDevice device, const GraphicsPipelineDesc &desc);
	VulkanPipeline(VkDevice device, const ComputePipelineDesc &desc);
	~VulkanPipeline() override;

	VulkanPipeline(const VulkanPipeline &)            = delete;
	VulkanPipeline &operator=(const VulkanPipeline &) = delete;
	VulkanPipeline(VulkanPipeline &&other) noexcept;
	VulkanPipeline &operator=(VulkanPipeline &&other) noexcept;

	[[nodiscard]] VkPipeline GetHandle() const
	{
		return pipeline;
	}
	[[nodiscard]] VkPipelineLayout GetLayout() const
	{
		return pipelineLayout;
	}
	PipelineType GetPipelineType() const
	{
		return pipelineType;
	}
	[[nodiscard]] VkPipelineBindPoint GetBindPoint() const
	{
		return pipelineType == PipelineType::GRAPHICS ? VK_PIPELINE_BIND_POINT_GRAPHICS : VK_PIPELINE_BIND_POINT_COMPUTE;
	}
	const RenderTargetSignature &GetTargetSignature() const
	{
		return targetSignature;
	}
};

// Vulkan Command List implementation
class VulkanCommandList final : public RefCounter<IRHICommandList>
{
  private:
	VkDevice        device;
	VkCommandBuffer commandBuffer;
	VulkanPipeline *currentPipeline;
	bool            inRendering;
	QueueType       queueType;
	uint32_t        queueFamily;

	// Queue family mappings for cross-queue transfers
	uint32_t graphicsQueueFamily;
	uint32_t computeQueueFamily;
	uint32_t transferQueueFamily;

	// Cached function pointers
	PFN_vkCmdBeginRenderingKHR vkCmdBeginRenderingKHR;
	PFN_vkCmdEndRenderingKHR   vkCmdEndRenderingKHR;

  public:
	VulkanCommandList(VkDevice device, VkCommandPool commandPool, QueueType queueType, uint32_t queueFamily,
	                  uint32_t graphicsFamily, uint32_t computeFamily, uint32_t transferFamily,
	                  PFN_vkCmdBeginRenderingKHR beginFunc,
	                  PFN_vkCmdEndRenderingKHR   endFunc);
	~VulkanCommandList() override = default;

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
	                       std::span<const uint32_t> dynamicOffsets = {}) override;
	void PushConstants(ShaderStageFlags stageFlags, uint32_t offset, std::span<const std::byte> data) override;
	void SetViewport(float x, float y, float width, float height) override;
	void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

	void Draw(uint32_t vertexCount, uint32_t firstVertex = 0) override;
	void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) override;
	void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;
	void DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(DrawIndexedIndirectCommand)) override;

	void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) override;
	void DispatchIndirect(IRHIBuffer *buffer, size_t offset) override;

	void CopyBuffer(IRHIBuffer *srcBuffer, IRHIBuffer *dstBuffer, std::span<const BufferCopy> regions) override;
	void CopyTexture(IRHITexture *srcTexture, IRHITexture *dstTexture, std::span<const TextureCopy> regions) override;
	void BlitTexture(IRHITexture *srcTexture, IRHITexture *dstTexture, std::span<const TextureBlit> regions, FilterMode filter = FilterMode::LINEAR) override;

	void Barrier(
	    PipelineScope                      src_scope,
	    PipelineScope                      dst_scope,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions,
	    std::span<const MemoryBarrier>     memory_barriers = {}) override;

	void ReleaseToQueue(
	    QueueType                          dstQueue,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions) override;

	void AcquireFromQueue(
	    QueueType                          srcQueue,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions) override;

#ifdef RHI_VULKAN
	void *GetNativeCommandBuffer() override
	{
		return static_cast<void *>(commandBuffer);
	}
#endif

	[[nodiscard]] VkCommandBuffer GetHandle() const
	{
		return commandBuffer;
	}
};

// Vulkan Swapchain implementation
class VulkanSwapchain final : public RefCounter<IRHISwapchain>
{
  private:
	VkInstance                     instance;
	VkDevice                       device;
	VkPhysicalDevice               physicalDevice;
	VmaAllocator                   allocator;
	VkSurfaceKHR                   surface;
	VkQueue                        graphicsQueue;
	VkSwapchainKHR                 swapchain;
	std::vector<VkImage>           swapchainImages;
	std::vector<TextureHandle>     backBuffers;
	std::vector<TextureViewHandle> backBufferViews;
	std::vector<VkFramebuffer>     framebuffers;
	VkRenderPass                   renderPass;
	VkFormat                       swapchainFormat;
	VkExtent2D                     swapchainExtent;
	VkSurfaceFormatKHR             chosenSurfaceFormat;
	VkPresentModeKHR               chosenPresentMode;
	uint32_t                       requestedBufferCount;
	VkSurfaceTransformFlagBitsKHR  currentPreTransform;

  public:
	VulkanSwapchain(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator, VkSurfaceKHR surface,
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
	SurfaceTransform GetPreTransform() const override;

	VkFramebuffer GetFramebuffer(uint32_t index, VkRenderPass renderPass);
};

// Vulkan Semaphore implementation
class VulkanSemaphore final : public RefCounter<IRHISemaphore>
{
  private:
	VkDevice    device;
	VkSemaphore semaphore;

  public:
	explicit VulkanSemaphore(VkDevice device);
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
class VulkanFence final : public RefCounter<IRHIFence>
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
class VulkanDescriptorSetLayout final : public RefCounter<IRHIDescriptorSetLayout>
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
class VulkanDescriptorSet final : public RefCounter<IRHIDescriptorSet>
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
class VulkanSampler final : public RefCounter<IRHISampler>
{
  private:
	VkDevice  device;
	VkSampler sampler;

  public:
	VulkanSampler(VkDevice device, const SamplerDesc &desc);
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

// Vulkan Composite Fence implementation
class VulkanCompositeFence final : public RefCounter<IRHIFence>
{
  private:
	std::vector<FenceHandle> m_fences;

  public:
	explicit VulkanCompositeFence(std::vector<FenceHandle> fences);
	~VulkanCompositeFence() override = default;

	VulkanCompositeFence(const VulkanCompositeFence &)            = delete;
	VulkanCompositeFence &operator=(const VulkanCompositeFence &) = delete;
	VulkanCompositeFence(VulkanCompositeFence &&)                 = delete;
	VulkanCompositeFence &operator=(VulkanCompositeFence &&)      = delete;

	void Wait(uint64_t timeout = UINT64_MAX) override;
	void Reset() override;
	bool IsSignaled() const override;
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
VkFilter              FilterModeToVulkan(FilterMode filter);
VkSamplerMipmapMode   MipmapModeToVulkan(MipmapMode mode);
VkSamplerAddressMode  SamplerAddressModeToVulkan(SamplerAddressMode mode);
VkBorderColor         BorderColorToVulkan(BorderColor color);
VkImageAspectFlags    TextureAspectToVulkan(TextureAspect aspect);
uint32_t              FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
VkPipelineStageFlags  PipelineScopeToVulkanStages(PipelineScope scope);
VkPipelineStageFlags  StageMaskToVulkan(StageMask mask);
VkAccessFlags         AccessMaskToVulkan(AccessMask mask);
void                  GetVulkanStagesAndAccess(ResourceState state, PipelineScope scope,
                                               VkPipelineStageFlags &stages, VkAccessFlags &access);
VkImageLayout         ResourceStateToImageLayout(ResourceState state);

}        // namespace rhi::vulkan