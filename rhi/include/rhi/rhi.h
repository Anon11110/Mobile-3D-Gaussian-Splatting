#pragma once
#include "common/ref_count.h"
#include "rhi_types.h"

namespace rhi
{

// Forward declarations of interfaces
class IRHIDevice;
class IRHIBuffer;
class IRHITexture;
class IRHITextureView;
class IRHISampler;
class IRHIShader;
class IRHIPipeline;
class IRHICommandList;
class IRHISwapchain;
class IRHISemaphore;
class IRHIFence;
class IRHIDescriptorSetLayout;
class IRHIDescriptorSet;

// Handle typedefs (forward declarations)
typedef RefCntPtr<IRHIDevice>              DeviceHandle;
typedef RefCntPtr<IRHIBuffer>              BufferHandle;
typedef RefCntPtr<IRHITexture>             TextureHandle;
typedef RefCntPtr<IRHITextureView>         TextureViewHandle;
typedef RefCntPtr<IRHISampler>             SamplerHandle;
typedef RefCntPtr<IRHIShader>              ShaderHandle;
typedef RefCntPtr<IRHIPipeline>            PipelineHandle;
typedef RefCntPtr<IRHICommandList>         CommandListHandle;
typedef RefCntPtr<IRHISwapchain>           SwapchainHandle;
typedef RefCntPtr<IRHISemaphore>           SemaphoreHandle;
typedef RefCntPtr<IRHIFence>               FenceHandle;
typedef RefCntPtr<IRHIDescriptorSetLayout> DescriptorSetLayoutHandle;
typedef RefCntPtr<IRHIDescriptorSet>       DescriptorSetHandle;

// Main device interface
class IRHIDevice : public IRefCounted
{
  public:
	virtual ~IRHIDevice() = default;

	// Resource creation
	virtual BufferHandle              CreateBuffer(const BufferDesc &desc)                           = 0;
	virtual TextureHandle             CreateTexture(const TextureDesc &desc)                         = 0;
	virtual TextureViewHandle         CreateTextureView(const TextureViewDesc &desc)                 = 0;
	virtual SamplerHandle             CreateSampler(const SamplerDesc &desc)                         = 0;
	virtual ShaderHandle              CreateShader(const ShaderDesc &desc)                           = 0;
	virtual PipelineHandle            CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)       = 0;
	virtual PipelineHandle            CreateComputePipeline(const ComputePipelineDesc &desc)         = 0;
	virtual CommandListHandle         CreateCommandList(QueueType queueType = QueueType::GRAPHICS)   = 0;
	virtual SwapchainHandle           CreateSwapchain(const SwapchainDesc &desc)                     = 0;
	virtual SemaphoreHandle           CreateSemaphore()                                              = 0;
	virtual FenceHandle               CreateFence(bool signaled = false)                             = 0;
	virtual FenceHandle               CreateCompositeFence(const std::vector<FenceHandle> &fences)   = 0;
	virtual DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) = 0;
	virtual DescriptorSetHandle       CreateDescriptorSet(IRHIDescriptorSetLayout *layout,
	                                                      QueueType                queueType = QueueType::GRAPHICS) = 0;

	// Buffer operations
	virtual void UpdateBuffer(IRHIBuffer *buffer, const void *RHI_RESTRICT data, size_t size, size_t offset = 0) = 0;

	// Async buffer upload
	virtual FenceHandle UploadBufferAsync(
	    IRHIBuffer *dstBuffer,
	    const void *data,
	    size_t      size,
	    size_t      offset = 0) = 0;

	virtual FenceHandle UploadBufferAsync(
	    const BufferHandle &dstBuffer,
	    const void         *data,
	    size_t              size,
	    size_t              offset = 0) = 0;

	// Queue operations
	virtual void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists,
	                                QueueType                         queueType       = QueueType::GRAPHICS,
	                                IRHISemaphore                    *waitSemaphore   = nullptr,
	                                IRHISemaphore                    *signalSemaphore = nullptr,
	                                IRHIFence                        *signalFence     = nullptr) = 0;

	virtual void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists,
	                                QueueType                         queueType,
	                                const SubmitInfo                 &submitInfo) = 0;

	virtual void WaitQueueIdle(QueueType queueType) = 0;
	virtual void WaitIdle()                         = 0;

	// Frame retirement for GPU resource lifetime management
	virtual void RetireCompletedFrame() = 0;
};

typedef RefCntPtr<IRHIDevice> DeviceHandle;

// Buffer interface
class IRHIBuffer : public IRefCounted
{
  public:
	virtual ~IRHIBuffer()                        = default;
	[[nodiscard]] virtual void  *Map()           = 0;
	virtual void                 Unmap()         = 0;
	[[nodiscard]] virtual size_t GetSize() const = 0;
};

typedef RefCntPtr<IRHIBuffer> BufferHandle;

// Texture interface
class IRHITexture : public IRefCounted
{
  public:
	virtual ~IRHITexture()                                     = default;
	[[nodiscard]] virtual uint32_t      GetWidth() const       = 0;
	[[nodiscard]] virtual uint32_t      GetHeight() const      = 0;
	[[nodiscard]] virtual uint32_t      GetDepth() const       = 0;
	[[nodiscard]] virtual uint32_t      GetMipLevels() const   = 0;
	[[nodiscard]] virtual uint32_t      GetArrayLayers() const = 0;
	[[nodiscard]] virtual TextureFormat GetFormat() const      = 0;
};

typedef RefCntPtr<IRHITexture> TextureHandle;

// Texture view interface
class IRHITextureView : public IRefCounted
{
  public:
	virtual ~IRHITextureView()                       = default;
	virtual IRHITexture  *GetTexture()               = 0;
	virtual TextureFormat GetFormat() const          = 0;
	virtual uint32_t      GetWidth() const           = 0;
	virtual uint32_t      GetHeight() const          = 0;
	virtual uint32_t      GetBaseMipLevel() const    = 0;
	virtual uint32_t      GetMipLevelCount() const   = 0;
	virtual uint32_t      GetBaseArrayLayer() const  = 0;
	virtual uint32_t      GetArrayLayerCount() const = 0;
};

typedef RefCntPtr<IRHITextureView> TextureViewHandle;

// Shader interface
class IRHIShader : public IRefCounted
{
  public:
	virtual ~IRHIShader()                = default;
	virtual ShaderStage GetStage() const = 0;
};

typedef RefCntPtr<IRHIShader> ShaderHandle;

// Pipeline interface
class IRHIPipeline : public IRefCounted
{
  public:
	virtual ~IRHIPipeline() = default;
};

typedef RefCntPtr<IRHIPipeline> PipelineHandle;

// Command list interface
class IRHICommandList : public IRefCounted
{
  public:
	virtual ~IRHICommandList() = default;

	virtual void Begin() = 0;
	virtual void End()   = 0;
	virtual void Reset() = 0;

	virtual void BeginRendering(const RenderingInfo &info) = 0;
	virtual void EndRendering()                            = 0;

	virtual void SetPipeline(IRHIPipeline *pipeline)                                                          = 0;
	virtual void SetVertexBuffer(uint32_t binding, IRHIBuffer *buffer, size_t offset = 0)                     = 0;
	virtual void BindIndexBuffer(IRHIBuffer *buffer, size_t offset = 0)                                       = 0;
	virtual void BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
	                               std::span<const uint32_t> dynamicOffsets = {})                             = 0;
	virtual void PushConstants(ShaderStageFlags stageFlags, uint32_t offset, std::span<const std::byte> data) = 0;
	virtual void SetViewport(float x, float y, float width, float height)                                     = 0;
	virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)                            = 0;

	virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0)                                                                                             = 0;
	virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0)                                                              = 0;
	virtual void DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(DrawIndexedIndirectCommand))                 = 0;
	virtual void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

	virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) = 0;
	virtual void DispatchIndirect(IRHIBuffer *buffer, size_t offset)                                = 0;

	virtual void CopyBuffer(IRHIBuffer *RHI_RESTRICT srcBuffer, IRHIBuffer *RHI_RESTRICT dstBuffer, std::span<const BufferCopy> regions) = 0;

	virtual void CopyTexture(IRHITexture *RHI_RESTRICT srcTexture, IRHITexture *RHI_RESTRICT dstTexture, std::span<const TextureCopy> regions)                                         = 0;
	virtual void BlitTexture(IRHITexture *RHI_RESTRICT srcTexture, IRHITexture *RHI_RESTRICT dstTexture, std::span<const TextureBlit> regions, FilterMode filter = FilterMode::LINEAR) = 0;

	virtual void Barrier(
	    PipelineScope                      src_scope,
	    PipelineScope                      dst_scope,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions,
	    std::span<const MemoryBarrier>     memory_barriers = {}) = 0;

	virtual void ReleaseToQueue(
	    QueueType                          dstQueue,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions) = 0;

	virtual void AcquireFromQueue(
	    QueueType                          srcQueue,
	    std::span<const BufferTransition>  buffer_transitions,
	    std::span<const TextureTransition> texture_transitions) = 0;
};

typedef RefCntPtr<IRHICommandList> CommandListHandle;

// Swapchain interface
class IRHISwapchain : public IRefCounted
{
  public:
	virtual ~IRHISwapchain() = default;

	virtual SwapchainStatus  AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore = nullptr) = 0;
	virtual SwapchainStatus  Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore = nullptr)             = 0;
	virtual IRHITexture     *GetBackBuffer(uint32_t index)                                                    = 0;
	virtual IRHITextureView *GetBackBufferView(uint32_t index)                                                = 0;
	virtual uint32_t         GetImageCount() const                                                            = 0;
	virtual void             Resize(uint32_t width, uint32_t height)                                          = 0;
};

typedef RefCntPtr<IRHISwapchain> SwapchainHandle;

// Synchronization primitives
class IRHISemaphore : public IRefCounted
{
  public:
	virtual ~IRHISemaphore() = default;
};

typedef RefCntPtr<IRHISemaphore> SemaphoreHandle;

class IRHIFence : public IRefCounted
{
  public:
	virtual ~IRHIFence()                             = default;
	virtual void Wait(uint64_t timeout = UINT64_MAX) = 0;
	virtual void Reset()                             = 0;
	virtual bool IsSignaled() const                  = 0;
};

typedef RefCntPtr<IRHIFence> FenceHandle;

// Descriptor set layout interface
class IRHIDescriptorSetLayout : public IRefCounted
{
  public:
	virtual ~IRHIDescriptorSetLayout() = default;
};

typedef RefCntPtr<IRHIDescriptorSetLayout> DescriptorSetLayoutHandle;

// Sampler interface
class IRHISampler : public IRefCounted
{
  public:
	virtual ~IRHISampler() = default;
};

typedef RefCntPtr<IRHISampler> SamplerHandle;

// Descriptor set interface
class IRHIDescriptorSet : public IRefCounted
{
  public:
	virtual ~IRHIDescriptorSet()                                                     = default;
	virtual void BindBuffer(uint32_t binding, const BufferBinding &bufferBinding)    = 0;
	virtual void BindTexture(uint32_t binding, const TextureBinding &textureBinding) = 0;
};

typedef RefCntPtr<IRHIDescriptorSet> DescriptorSetHandle;

// Device creation function
DeviceHandle CreateRHIDevice();

}        // namespace rhi