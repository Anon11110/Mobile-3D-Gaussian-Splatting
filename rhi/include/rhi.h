#pragma once
#include "rhi_types.h"

namespace rhi
{

// Main device interface
class IRHIDevice
{
  public:
	virtual ~IRHIDevice() = default;

	// Resource creation
	virtual std::unique_ptr<IRHIBuffer>              CreateBuffer(const BufferDesc &desc)                           = 0;
	virtual std::unique_ptr<IRHITexture>             CreateTexture(const TextureDesc &desc)                         = 0;
	virtual std::unique_ptr<IRHITextureView>         CreateTextureView(const TextureViewDesc &desc)                 = 0;
	virtual std::unique_ptr<IRHIShader>              CreateShader(const ShaderDesc &desc)                           = 0;
	virtual std::unique_ptr<IRHIPipeline>            CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)       = 0;
	virtual std::unique_ptr<IRHIPipeline>            CreateComputePipeline(const ComputePipelineDesc &desc)         = 0;
	virtual std::unique_ptr<IRHICommandList>         CreateCommandList(QueueType queueType = QueueType::GRAPHICS)   = 0;
	virtual std::unique_ptr<IRHISwapchain>           CreateSwapchain(const SwapchainDesc &desc)                     = 0;
	virtual std::unique_ptr<IRHISemaphore>           CreateSemaphore()                                              = 0;
	virtual std::unique_ptr<IRHIFence>               CreateFence(bool signaled = false)                             = 0;
	virtual std::unique_ptr<IRHIDescriptorSetLayout> CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) = 0;
	virtual std::unique_ptr<IRHIDescriptorSet>       CreateDescriptorSet(IRHIDescriptorSetLayout *layout,
	                                                                     QueueType                queueType = QueueType::GRAPHICS) = 0;

	// Queue operations
	virtual void SubmitCommandLists(IRHICommandList **cmdLists, uint32_t count,
	                                QueueType      queueType       = QueueType::GRAPHICS,
	                                IRHISemaphore *waitSemaphore   = nullptr,
	                                IRHISemaphore *signalSemaphore = nullptr,
	                                IRHIFence     *signalFence     = nullptr) = 0;

	virtual void SubmitCommandLists(IRHICommandList **cmdLists, uint32_t count,
	                                QueueType         queueType,
	                                const SubmitInfo &submitInfo) = 0;

	virtual void WaitQueueIdle(QueueType queueType) = 0;
	virtual void WaitIdle()                         = 0;
};

// Buffer interface
class IRHIBuffer
{
  public:
	virtual ~IRHIBuffer()          = default;
	virtual void  *Map()           = 0;
	virtual void   Unmap()         = 0;
	virtual size_t GetSize() const = 0;
};

// Texture interface
class IRHITexture
{
  public:
	virtual ~IRHITexture()                       = default;
	virtual uint32_t      GetWidth() const       = 0;
	virtual uint32_t      GetHeight() const      = 0;
	virtual uint32_t      GetDepth() const       = 0;
	virtual uint32_t      GetMipLevels() const   = 0;
	virtual uint32_t      GetArrayLayers() const = 0;
	virtual TextureFormat GetFormat() const      = 0;
};

// Texture view interface
class IRHITextureView
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

// Shader interface
class IRHIShader
{
  public:
	virtual ~IRHIShader()                = default;
	virtual ShaderStage GetStage() const = 0;
};

// Pipeline interface
class IRHIPipeline
{
  public:
	virtual ~IRHIPipeline() = default;
};

// Command list interface
class IRHICommandList
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
	                               const uint32_t *dynamicOffsets = nullptr, uint32_t dynamicOffsetCount = 0) = 0;
	virtual void PushConstants(ShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void *data) = 0;
	virtual void SetViewport(float x, float y, float width, float height)                                     = 0;
	virtual void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)                            = 0;

	virtual void Draw(uint32_t vertexCount, uint32_t firstVertex = 0)                                                                             = 0;
	virtual void DrawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0)                                              = 0;
	virtual void DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount, uint32_t stride = sizeof(DrawIndexedIndirectCommand)) = 0;

	virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) = 0;
	virtual void DispatchIndirect(IRHIBuffer *buffer, size_t offset)                                = 0;

	virtual void Barrier(
	    PipelineScope                         src_scope,
	    PipelineScope                         dst_scope,
	    const std::vector<BufferTransition>  &buffer_transitions,
	    const std::vector<TextureTransition> &texture_transitions,
	    const std::vector<MemoryBarrier>     &memory_barriers = {}) = 0;
};

// Swapchain interface
class IRHISwapchain
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

// Synchronization primitives
class IRHISemaphore
{
  public:
	virtual ~IRHISemaphore() = default;
};

class IRHIFence
{
  public:
	virtual ~IRHIFence()                             = default;
	virtual void Wait(uint64_t timeout = UINT64_MAX) = 0;
	virtual void Reset()                             = 0;
	virtual bool IsSignaled() const                  = 0;
};

// Descriptor set layout interface
class IRHIDescriptorSetLayout
{
  public:
	virtual ~IRHIDescriptorSetLayout() = default;
};

// Sampler interface
class IRHISampler
{
  public:
	virtual ~IRHISampler() = default;
};

// Descriptor set interface
class IRHIDescriptorSet
{
  public:
	virtual ~IRHIDescriptorSet()                                                     = default;
	virtual void BindBuffer(uint32_t binding, const BufferBinding &bufferBinding)    = 0;
	virtual void BindTexture(uint32_t binding, const TextureBinding &textureBinding) = 0;
};

// Device creation function
std::unique_ptr<IRHIDevice> CreateRHIDevice();

}        // namespace rhi