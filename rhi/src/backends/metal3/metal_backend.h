#pragma once

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../../include/rhi/rhi.h"

namespace rhi::metal3
{

class MetalDevice;

class MetalBuffer final : public RefCounter<IRHIBuffer>
{
  public:
	MetalBuffer(MTL::Buffer *buffer, size_t size, IndexType indexType, MTL::StorageMode storageMode);
	~MetalBuffer() override;

	void  *Map() override;
	void   Unmap() override;
	size_t GetSize() const override;

	void Update(const void *data, size_t size, size_t offset);

	[[nodiscard]] MTL::Buffer     *GetHandle() const;
	[[nodiscard]] IndexType        GetIndexType() const;
	[[nodiscard]] MTL::StorageMode GetStorageMode() const;
	[[nodiscard]] bool             IsCpuVisible() const;

  private:
	MTL::Buffer     *buffer_      = nullptr;
	size_t           size_        = 0;
	IndexType        indexType_   = IndexType::UINT32;
	MTL::StorageMode storageMode_ = MTL::StorageModePrivate;
	bool             isMapped_    = false;
};

class MetalTexture final : public RefCounter<IRHITexture>
{
  public:
	MetalTexture(MTL::Texture *texture, const TextureDesc &desc, bool ownsTexture);
	MetalTexture(MTL::Texture *texture, TextureFormat format, uint32_t width, uint32_t height,
	             uint32_t depth, uint32_t mipLevels, uint32_t arrayLayers, TextureType type,
	             bool ownsTexture);
	~MetalTexture() override;

	uint32_t      GetWidth() const override;
	uint32_t      GetHeight() const override;
	uint32_t      GetDepth() const override;
	uint32_t      GetMipLevels() const override;
	uint32_t      GetArrayLayers() const override;
	TextureFormat GetFormat() const override;
	TextureType   GetType() const override;

	[[nodiscard]] MTL::Texture *GetHandle() const;

  private:
	MTL::Texture *texture_     = nullptr;
	bool          ownsTexture_ = false;

	uint32_t      width_       = 0;
	uint32_t      height_      = 0;
	uint32_t      depth_       = 0;
	uint32_t      mipLevels_   = 1;
	uint32_t      arrayLayers_ = 1;
	TextureType   type_        = TextureType::TEXTURE_2D;
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

	[[nodiscard]] MTL::Texture *GetHandle() const;

  private:
	MetalTexture *texture_         = nullptr;
	MTL::Texture *viewTexture_     = nullptr;
	bool          ownsViewTexture_ = false;

	TextureFormat format_          = TextureFormat::UNDEFINED;
	uint32_t      baseMipLevel_    = 0;
	uint32_t      mipLevelCount_   = 1;
	uint32_t      baseArrayLayer_  = 0;
	uint32_t      arrayLayerCount_ = 1;
};

class MetalShader final : public RefCounter<IRHIShader>
{
  public:
	MetalShader(const ShaderDesc &desc, MTL::Library *library, MTL::Function *function);
	~MetalShader() override;

	ShaderStage GetStage() const override;

	[[nodiscard]] MTL::Function     *GetFunction() const;
	[[nodiscard]] const char        *GetEntryPoint() const;
	[[nodiscard]] const std::string &GetEntryPointString() const;

  private:
	ShaderStage    stage_ = ShaderStage::VERTEX;
	std::string    entryPoint_;
	MTL::Library  *library_  = nullptr;
	MTL::Function *function_ = nullptr;
};

class MetalPipeline final : public RefCounter<IRHIPipeline>
{
  public:
	MetalPipeline(const GraphicsPipelineDesc &desc, MTL::RenderPipelineState *renderPipelineState,
	              MTL::DepthStencilState *depthStencilState);
	MetalPipeline(const ComputePipelineDesc &desc, MTL::ComputePipelineState *computePipelineState,
	              MTL::Size threadsPerThreadgroup);
	~MetalPipeline() override;

	[[nodiscard]] PipelineType                                  GetPipelineType() const;
	[[nodiscard]] MTL::RenderPipelineState                     *GetRenderPipelineState() const;
	[[nodiscard]] MTL::DepthStencilState                       *GetDepthStencilState() const;
	[[nodiscard]] MTL::ComputePipelineState                    *GetComputePipelineState() const;
	[[nodiscard]] MTL::PrimitiveType                            GetPrimitiveType() const;
	[[nodiscard]] MTL::CullMode                                 GetCullMode() const;
	[[nodiscard]] MTL::Winding                                  GetFrontFacingWinding() const;
	[[nodiscard]] MTL::TriangleFillMode                         GetTriangleFillMode() const;
	[[nodiscard]] bool                                          IsDepthBiasEnabled() const;
	[[nodiscard]] float                                         GetDepthBiasConstantFactor() const;
	[[nodiscard]] float                                         GetDepthBiasSlopeFactor() const;
	[[nodiscard]] float                                         GetDepthBiasClamp() const;
	[[nodiscard]] const std::vector<PushConstantRange>         &GetPushConstantRanges() const;
	[[nodiscard]] const std::vector<IRHIDescriptorSetLayout *> &GetDescriptorSetLayouts() const;
	[[nodiscard]] MTL::Size                                     GetThreadsPerThreadgroup() const;

  private:
	PipelineType pipelineType_ = PipelineType::GRAPHICS;

	MTL::RenderPipelineState  *renderPipelineState_  = nullptr;
	MTL::DepthStencilState    *depthStencilState_    = nullptr;
	MTL::ComputePipelineState *computePipelineState_ = nullptr;

	MTL::PrimitiveType    primitiveType_      = MTL::PrimitiveTypeTriangle;
	MTL::CullMode         cullMode_           = MTL::CullModeNone;
	MTL::Winding          frontFacingWinding_ = MTL::WindingCounterClockwise;
	MTL::TriangleFillMode triangleFillMode_   = MTL::TriangleFillModeFill;

	bool  depthBiasEnable_         = false;
	float depthBiasConstantFactor_ = 0.0f;
	float depthBiasSlopeFactor_    = 0.0f;
	float depthBiasClamp_          = 0.0f;

	std::vector<IRHIDescriptorSetLayout *> descriptorSetLayouts_;
	std::vector<PushConstantRange>         pushConstantRanges_;
	MTL::Size                              threadsPerThreadgroup_ = {1, 1, 1};
};

class MetalCommandList final : public RefCounter<IRHICommandList>
{
  public:
	MetalCommandList(MetalDevice *device, QueueType queueType);
	~MetalCommandList() override;

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

	[[nodiscard]] QueueType           GetQueueType() const;
	[[nodiscard]] MTL::CommandBuffer *GetHandle() const;

  private:
	void RequireRecording(const char *apiName) const;
	void EndActiveEncoders();

	MTL::BlitCommandEncoder    *EnsureBlitEncoder();
	MTL::ComputeCommandEncoder *EnsureComputeEncoder();

	void ApplyCurrentPipelineState();
	void ApplyDescriptorSetBindings(uint32_t setIndex, class MetalDescriptorSet *descriptorSet,
	                                std::span<const uint32_t> dynamicOffsets);
	void TrackBufferBinding(ShaderStageFlags stageFlags, uint32_t slot, MTL::Buffer *buffer,
	                        size_t offset, size_t range);
	void ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags stage);
	void ApplyTopLevelArgumentBufferForCompute();
	void ClearTrackedBindings();
	void EnsureTopLevelArgumentBufferCapacity(size_t requiredBytes);
	void ResetTopLevelArgumentBufferAllocator();

	struct TopLevelArgumentAllocation
	{
		std::byte *cpuPtr = nullptr;
		size_t     offset = 0;
	};

	[[nodiscard]] TopLevelArgumentAllocation AllocateTopLevelArgumentData(size_t bytes, size_t alignment);

  private:
	struct TrackedBufferBinding
	{
		MTL::Buffer *buffer = nullptr;
		uint64_t     offset = 0;
		uint64_t     size   = 0;
		bool         valid  = false;
	};

	MetalDevice *device_    = nullptr;
	QueueType    queueType_ = QueueType::GRAPHICS;

	NS::AutoreleasePool *autoreleasePool_ = nullptr;
	MTL::CommandBuffer  *commandBuffer_   = nullptr;

	MTL::RenderCommandEncoder  *renderEncoder_  = nullptr;
	MTL::ComputeCommandEncoder *computeEncoder_ = nullptr;
	MTL::BlitCommandEncoder    *blitEncoder_    = nullptr;

	MetalPipeline *currentPipeline_ = nullptr;

	MetalBuffer *indexBuffer_       = nullptr;
	size_t       indexBufferOffset_ = 0;
	IndexType    indexType_         = IndexType::UINT32;

	// Metal shader converter top-level argument buffer uses slot 2.
	// Track root CBV bindings by register index [0..30].
	std::array<TrackedBufferBinding, 31> vertexTrackedBuffers_{};
	std::array<TrackedBufferBinding, 31> fragmentTrackedBuffers_{};
	std::array<TrackedBufferBinding, 31> computeTrackedBuffers_{};
	MTL::Buffer                         *pushConstantBackingBuffer_      = nullptr;
	MTL::Buffer                         *topLevelArgumentBuffer_         = nullptr;
	size_t                               topLevelArgumentBufferCapacity_ = 0;
	size_t                               topLevelArgumentBufferOffset_   = 0;

	bool isRecording_ = false;
};

class MetalSwapchain final : public RefCounter<IRHISwapchain>
{
  public:
	MetalSwapchain(MetalDevice *device, const SwapchainDesc &desc);
	~MetalSwapchain() override;

	SwapchainStatus  AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore = nullptr) override;
	SwapchainStatus  Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore = nullptr) override;
	IRHITexture     *GetBackBuffer(uint32_t index) override;
	IRHITextureView *GetBackBufferView(uint32_t index) override;
	uint32_t         GetImageCount() const override;
	void             Resize(uint32_t width, uint32_t height) override;
	SurfaceTransform GetPreTransform() const override;

  private:
	void ReleaseDrawables();
	void RebuildBackBufferWrappers(uint32_t index, CA::MetalDrawable *drawable);

  private:
	MetalDevice    *device_     = nullptr;
	CA::MetalLayer *layer_      = nullptr;
	bool            ownsLayer_  = false;
	TextureFormat   format_     = TextureFormat::B8G8R8A8_UNORM;
	uint32_t        width_      = 0;
	uint32_t        height_     = 0;
	uint32_t        imageCount_ = 0;
	uint32_t        frameIndex_ = 0;

	std::vector<CA::MetalDrawable *> drawables_;
	std::vector<TextureHandle>       backBuffers_;
	std::vector<TextureViewHandle>   backBufferViews_;
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

	[[nodiscard]] const DescriptorSetLayoutDesc &GetDesc() const;
	[[nodiscard]] const DescriptorBinding       *FindBinding(uint32_t binding) const;

  private:
	DescriptorSetLayoutDesc              desc_{};
	std::unordered_map<uint32_t, size_t> bindingLookup_;
};

class MetalSampler final : public RefCounter<IRHISampler>
{
  public:
	MetalSampler(const SamplerDesc &desc, MTL::SamplerState *samplerState);
	~MetalSampler() override;

	[[nodiscard]] const SamplerDesc &GetDesc() const;
	[[nodiscard]] MTL::SamplerState *GetHandle() const;

  private:
	SamplerDesc        desc_{};
	MTL::SamplerState *samplerState_ = nullptr;
};

class MetalDescriptorSet final : public RefCounter<IRHIDescriptorSet>
{
  public:
	explicit MetalDescriptorSet(IRHIDescriptorSetLayout *layout);
	~MetalDescriptorSet() override = default;

	void BindBuffer(uint32_t binding, const BufferBinding &bufferBinding) override;
	void BindTexture(uint32_t binding, const TextureBinding &textureBinding) override;

	[[nodiscard]] const MetalDescriptorSetLayout                         *GetLayout() const;
	[[nodiscard]] const std::vector<std::pair<uint32_t, BufferBinding>>  &GetBufferBindings() const;
	[[nodiscard]] const std::vector<std::pair<uint32_t, TextureBinding>> &GetTextureBindings() const;
	[[nodiscard]] const BufferBinding                                    *FindBufferBinding(uint32_t binding) const;
	[[nodiscard]] const TextureBinding                                   *FindTextureBinding(uint32_t binding) const;

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

	[[nodiscard]] MTL::Device       *GetMTLDevice() const;
	[[nodiscard]] MTL::CommandQueue *GetCommandQueue(QueueType queueType) const;
	[[nodiscard]] bool               IsUnifiedMemory() const;

  private:
	void SignalFence(IRHIFence *fence);
	void WaitForQueue(MTL::CommandQueue *queue) const;

  private:
	MTL::Device       *device_        = nullptr;
	MTL::CommandQueue *graphicsQueue_ = nullptr;
	MTL::CommandQueue *computeQueue_  = nullptr;
	MTL::CommandQueue *transferQueue_ = nullptr;
	bool               unifiedMemory_ = true;

	mutable std::mutex submitMutex_;
};

}        // namespace rhi::metal3
