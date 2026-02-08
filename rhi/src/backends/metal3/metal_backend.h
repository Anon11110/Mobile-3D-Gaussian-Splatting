#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

#include "../../../include/rhi/rhi.h"

namespace rhi::metal3
{

[[noreturn]] void ThrowMetalPhase0NotImplemented(const char *apiName);

class MetalBuffer final : public RefCounter<IRHIBuffer>
{
  public:
	explicit MetalBuffer(const BufferDesc &desc);
	~MetalBuffer() override = default;

	void  *Map() override;
	void   Unmap() override;
	size_t GetSize() const override;

	void      Update(const void *data, size_t size, size_t offset);
	IndexType GetIndexType() const;

  private:
	std::vector<std::byte> storage_;
	IndexType              indexType_ = IndexType::UINT32;
	bool                   isMapped_  = false;
};

class MetalTexture final : public RefCounter<IRHITexture>
{
  public:
	explicit MetalTexture(const TextureDesc &desc);
	~MetalTexture() override = default;

	uint32_t      GetWidth() const override;
	uint32_t      GetHeight() const override;
	uint32_t      GetDepth() const override;
	uint32_t      GetMipLevels() const override;
	uint32_t      GetArrayLayers() const override;
	TextureFormat GetFormat() const override;

  private:
	uint32_t      width_       = 0;
	uint32_t      height_      = 0;
	uint32_t      depth_       = 0;
	uint32_t      mipLevels_   = 1;
	uint32_t      arrayLayers_ = 1;
	TextureFormat format_      = TextureFormat::UNDEFINED;
};

class MetalTextureView final : public RefCounter<IRHITextureView>
{
  public:
	explicit MetalTextureView(const TextureViewDesc &desc);
	~MetalTextureView() override;

	IRHITexture  *GetTexture() override;
	TextureFormat GetFormat() const override;
	uint32_t      GetWidth() const override;
	uint32_t      GetHeight() const override;
	uint32_t      GetBaseMipLevel() const override;
	uint32_t      GetMipLevelCount() const override;
	uint32_t      GetBaseArrayLayer() const override;
	uint32_t      GetArrayLayerCount() const override;

  private:
	MetalTexture *texture_         = nullptr;
	TextureFormat format_          = TextureFormat::UNDEFINED;
	uint32_t      baseMipLevel_    = 0;
	uint32_t      mipLevelCount_   = 1;
	uint32_t      baseArrayLayer_  = 0;
	uint32_t      arrayLayerCount_ = 1;
};

class MetalShader final : public RefCounter<IRHIShader>
{
  public:
	explicit MetalShader(const ShaderDesc &desc);
	~MetalShader() override = default;

	ShaderStage GetStage() const override;

  private:
	ShaderStage stage_ = ShaderStage::VERTEX;
};

class MetalPipeline final : public RefCounter<IRHIPipeline>
{
  public:
	explicit MetalPipeline(const GraphicsPipelineDesc &desc);
	explicit MetalPipeline(const ComputePipelineDesc &desc);
	~MetalPipeline() override = default;

	PipelineType GetPipelineType() const;

  private:
	PipelineType pipelineType_ = PipelineType::GRAPHICS;
};

class MetalCommandList final : public RefCounter<IRHICommandList>
{
  public:
	explicit MetalCommandList(QueueType queueType);
	~MetalCommandList() override = default;

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
	void DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount,
	                         uint32_t stride = sizeof(DrawIndexedIndirectCommand)) override;
	void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex = 0,
	                          int32_t vertexOffset = 0, uint32_t firstInstance = 0) override;

	void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) override;
	void DispatchIndirect(IRHIBuffer *buffer, size_t offset) override;

	void CopyBuffer(IRHIBuffer *RHI_RESTRICT srcBuffer, IRHIBuffer *RHI_RESTRICT dstBuffer,
	                std::span<const BufferCopy> regions) override;
	void FillBuffer(IRHIBuffer *buffer, size_t offset, size_t size, uint32_t value) override;
	void CopyTexture(IRHITexture *RHI_RESTRICT srcTexture, IRHITexture *RHI_RESTRICT dstTexture,
	                 std::span<const TextureCopy> regions) override;
	void BlitTexture(IRHITexture *RHI_RESTRICT srcTexture, IRHITexture *RHI_RESTRICT dstTexture,
	                 std::span<const TextureBlit> regions, FilterMode filter = FilterMode::LINEAR) override;

	void Barrier(PipelineScope src_scope, PipelineScope dst_scope,
	             std::span<const BufferTransition>  buffer_transitions,
	             std::span<const TextureTransition> texture_transitions,
	             std::span<const MemoryBarrier>     memory_barriers = {}) override;

	void ResetQueryPool(IRHIQueryPool *queryPool, uint32_t firstQuery, uint32_t queryCount) override;
	void WriteTimestamp(IRHIQueryPool *queryPool, uint32_t query, StageMask stage = StageMask::AllCommands) override;
	void BeginQuery(IRHIQueryPool *queryPool, uint32_t query) override;
	void EndQuery(IRHIQueryPool *queryPool, uint32_t query) override;
	void CopyQueryPoolResults(IRHIQueryPool *queryPool, uint32_t firstQuery, uint32_t queryCount,
	                          IRHIBuffer *dstBuffer, size_t dstOffset, size_t stride,
	                          QueryResultFlags flags = QueryResultFlags::WAIT) override;

	void ReleaseToQueue(QueueType dstQueue, std::span<const BufferTransition> buffer_transitions,
	                    std::span<const TextureTransition> texture_transitions) override;
	void AcquireFromQueue(QueueType srcQueue, std::span<const BufferTransition> buffer_transitions,
	                      std::span<const TextureTransition> texture_transitions) override;

	QueueType GetQueueType() const;

  private:
	QueueType queueType_   = QueueType::GRAPHICS;
	bool      isRecording_ = false;
};

class MetalSwapchain final : public RefCounter<IRHISwapchain>
{
  public:
	explicit MetalSwapchain(const SwapchainDesc &desc);
	~MetalSwapchain() override = default;

	SwapchainStatus  AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore = nullptr) override;
	SwapchainStatus  Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore = nullptr) override;
	IRHITexture     *GetBackBuffer(uint32_t index) override;
	IRHITextureView *GetBackBufferView(uint32_t index) override;
	uint32_t         GetImageCount() const override;
	void             Resize(uint32_t width, uint32_t height) override;
	SurfaceTransform GetPreTransform() const override;

  private:
	uint32_t                       width_      = 0;
	uint32_t                       height_     = 0;
	uint32_t                       imageCount_ = 0;
	uint32_t                       frameIndex_ = 0;
	std::vector<TextureHandle>     backBuffers_;
	std::vector<TextureViewHandle> backBufferViews_;
};

class MetalSemaphore final : public RefCounter<IRHISemaphore>
{
  public:
	MetalSemaphore()           = default;
	~MetalSemaphore() override = default;
};

class MetalFence final : public RefCounter<IRHIFence>
{
  public:
	explicit MetalFence(bool signaled);
	~MetalFence() override = default;

	void Wait(uint64_t timeout = UINT64_MAX) override;
	void Reset() override;
	bool IsSignaled() const override;

	void Signal();

  private:
	mutable std::mutex              mutex_;
	mutable std::condition_variable cv_;
	bool                            signaled_ = false;
};

class MetalCompositeFence final : public RefCounter<IRHIFence>
{
  public:
	explicit MetalCompositeFence(std::vector<FenceHandle> fences);
	~MetalCompositeFence() override = default;

	void Wait(uint64_t timeout = UINT64_MAX) override;
	void Reset() override;
	bool IsSignaled() const override;

  private:
	std::vector<FenceHandle> fences_;
};

class MetalDescriptorSetLayout final : public RefCounter<IRHIDescriptorSetLayout>
{
  public:
	explicit MetalDescriptorSetLayout(const DescriptorSetLayoutDesc &desc);
	~MetalDescriptorSetLayout() override = default;

	const DescriptorSetLayoutDesc &GetDesc() const;

  private:
	DescriptorSetLayoutDesc desc_{};
};

class MetalSampler final : public RefCounter<IRHISampler>
{
  public:
	explicit MetalSampler(const SamplerDesc &desc);
	~MetalSampler() override = default;

	const SamplerDesc &GetDesc() const;

  private:
	SamplerDesc desc_{};
};

class MetalDescriptorSet final : public RefCounter<IRHIDescriptorSet>
{
  public:
	explicit MetalDescriptorSet(IRHIDescriptorSetLayout *layout);
	~MetalDescriptorSet() override = default;

	void BindBuffer(uint32_t binding, const BufferBinding &bufferBinding) override;
	void BindTexture(uint32_t binding, const TextureBinding &textureBinding) override;

  private:
	RefCntPtr<IRHIDescriptorSetLayout>               layout_;
	std::vector<std::pair<uint32_t, BufferBinding>>  bufferBindings_;
	std::vector<std::pair<uint32_t, TextureBinding>> textureBindings_;
};

class MetalQueryPool final : public RefCounter<IRHIQueryPool>
{
  public:
	explicit MetalQueryPool(const QueryPoolDesc &desc);
	~MetalQueryPool() override = default;

	QueryType GetQueryType() const override;
	uint32_t  GetQueryCount() const override;

  private:
	QueryType queryType_  = QueryType::TIMESTAMP;
	uint32_t  queryCount_ = 0;
};

class MetalDevice final : public RefCounter<IRHIDevice>
{
  public:
	MetalDevice();
	~MetalDevice() override;

	BufferHandle              CreateBuffer(const BufferDesc &desc) override;
	TextureHandle             CreateTexture(const TextureDesc &desc) override;
	TextureViewHandle         CreateTextureView(const TextureViewDesc &desc) override;
	SamplerHandle             CreateSampler(const SamplerDesc &desc) override;
	ShaderHandle              CreateShader(const ShaderDesc &desc) override;
	PipelineHandle            CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
	PipelineHandle            CreateComputePipeline(const ComputePipelineDesc &desc) override;
	CommandListHandle         CreateCommandList(QueueType queueType = QueueType::GRAPHICS) override;
	SwapchainHandle           CreateSwapchain(const SwapchainDesc &desc) override;
	SemaphoreHandle           CreateSemaphore() override;
	FenceHandle               CreateFence(bool signaled = false) override;
	FenceHandle               CreateCompositeFence(const std::vector<FenceHandle> &fences) override;
	DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) override;
	DescriptorSetHandle       CreateDescriptorSet(IRHIDescriptorSetLayout *layout,
	                                              QueueType                queueType = QueueType::GRAPHICS) override;
	QueryPoolHandle           CreateQueryPool(const QueryPoolDesc &desc) override;

	void UpdateBuffer(IRHIBuffer *buffer, const void *RHI_RESTRICT data, size_t size, size_t offset = 0) override;

	FenceHandle UploadBufferAsync(IRHIBuffer *dstBuffer, const void *data, size_t size, size_t offset = 0) override;
	FenceHandle UploadBufferAsync(const BufferHandle &dstBuffer, const void *data, size_t size,
	                              size_t offset = 0) override;

	void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists,
	                        QueueType                         queueType       = QueueType::GRAPHICS,
	                        IRHISemaphore                    *waitSemaphore   = nullptr,
	                        IRHISemaphore                    *signalSemaphore = nullptr,
	                        IRHIFence                        *signalFence     = nullptr) override;

	void SubmitCommandLists(std::span<IRHICommandList *const> cmdLists, QueueType queueType,
	                        const SubmitInfo &submitInfo) override;

	void WaitQueueIdle(QueueType queueType) override;
	void WaitIdle() override;

	void RetireCompletedFrame() override;

	double GetTimestampPeriod() const override;
	bool   GetQueryPoolResults(IRHIQueryPool *queryPool, uint32_t firstQuery, uint32_t queryCount,
	                           void *data, size_t dataSize, size_t stride,
	                           QueryResultFlags flags = QueryResultFlags::WAIT) override;

	GpuMemoryStats GetMemoryStats() const override;

  private:
	void SignalFence(IRHIFence *fence);
};

}        // namespace rhi::metal3
