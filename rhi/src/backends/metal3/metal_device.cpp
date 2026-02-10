#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "metal_backend.h"
#include "metal_conversions.h"

namespace rhi::metal3
{
namespace
{

constexpr uint32_t kMetalShaderConverterVertexAttributeBase = 11;
constexpr uint32_t kMetalShaderConverterVertexBufferBase    = 6;

std::string ToStdString(const NS::String *string)
{
	if (string == nullptr)
	{
		return {};
	}

	const char *utf8 = string->utf8String();
	if (utf8 == nullptr)
	{
		return {};
	}

	return utf8;
}

std::string ToErrorMessage(NS::Error *error, const char *fallback)
{
	if (error == nullptr)
	{
		return fallback;
	}

	std::string message = ToStdString(error->localizedDescription());
	if (message.empty())
	{
		message = fallback;
	}
	return message;
}

void ConfigureStencilDesc(MTL::StencilDescriptor *desc, const StencilOpState &state)
{
	desc->setStencilCompareFunction(CompareOpToMetal(state.compareOp));
	desc->setStencilFailureOperation(StencilOpToMetal(state.failOp));
	desc->setDepthFailureOperation(StencilOpToMetal(state.depthFailOp));
	desc->setDepthStencilPassOperation(StencilOpToMetal(state.passOp));
	desc->setReadMask(state.compareMask);
	desc->setWriteMask(state.writeMask);
}

void UploadToPrivateBufferSync(MTL::Device *device, MTL::CommandQueue *queue, MTL::Buffer *dst,
                               const void *data, size_t size, size_t offset)
{
	MTL::ResourceOptions stagingOptions = MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined;
	MTL::Buffer         *stagingBuffer  = device->newBuffer(size, stagingOptions);
	if (stagingBuffer == nullptr)
	{
		throw std::runtime_error("Failed to create staging buffer for Metal upload");
	}

	std::memcpy(stagingBuffer->contents(), data, size);

	MTL::CommandBuffer *commandBuffer = queue->commandBuffer();
	if (commandBuffer == nullptr)
	{
		stagingBuffer->release();
		throw std::runtime_error("Failed to allocate Metal command buffer for upload");
	}

	MTL::BlitCommandEncoder *blit = commandBuffer->blitCommandEncoder();
	if (blit == nullptr)
	{
		stagingBuffer->release();
		throw std::runtime_error("Failed to allocate Metal blit encoder for upload");
	}

	blit->copyFromBuffer(stagingBuffer, 0, dst, offset, size);
	blit->endEncoding();
	commandBuffer->commit();
	commandBuffer->waitUntilCompleted();

	stagingBuffer->release();
}

}        // namespace

MetalDevice::MetalDevice()
{
	device_ = MTL::CreateSystemDefaultDevice();
	if (device_ == nullptr)
	{
		throw std::runtime_error("Failed to create default Metal device");
	}

	graphicsQueue_ = device_->newCommandQueue();
	computeQueue_  = device_->newCommandQueue();
	transferQueue_ = device_->newCommandQueue();
	if (graphicsQueue_ == nullptr || computeQueue_ == nullptr || transferQueue_ == nullptr)
	{
		throw std::runtime_error("Failed to create one or more Metal command queues");
	}

	unifiedMemory_ = device_->hasUnifiedMemory();
}

MetalDevice::~MetalDevice()
{
	WaitIdle();

	if (transferQueue_ != nullptr)
	{
		transferQueue_->release();
		transferQueue_ = nullptr;
	}
	if (computeQueue_ != nullptr)
	{
		computeQueue_->release();
		computeQueue_ = nullptr;
	}
	if (graphicsQueue_ != nullptr)
	{
		graphicsQueue_->release();
		graphicsQueue_ = nullptr;
	}
	if (device_ != nullptr)
	{
		device_->release();
		device_ = nullptr;
	}
}

BufferHandle MetalDevice::CreateBuffer(const BufferDesc &desc)
{
	const size_t         allocSize   = std::max<size_t>(desc.size, 1);
	MTL::StorageMode     storageMode = SelectStorageMode(desc.resourceUsage, desc.hints, unifiedMemory_);
	MTL::ResourceOptions options     = MakeResourceOptions(desc.resourceUsage, desc.hints, unifiedMemory_);

	MTL::Buffer *buffer = device_->newBuffer(allocSize, options);
	if (buffer == nullptr)
	{
		throw std::runtime_error("Failed to create Metal buffer");
	}

	MetalBuffer *metalBuffer = new MetalBuffer(buffer, desc.size, desc.indexType, storageMode);

	if (desc.initialData != nullptr && desc.size > 0)
	{
		if (storageMode == MTL::StorageModePrivate)
		{
			UploadToPrivateBufferSync(device_, transferQueue_, buffer, desc.initialData, desc.size, 0);
		}
		else
		{
			metalBuffer->Update(desc.initialData, desc.size, 0);
		}
	}

	return RefCntPtr<IRHIBuffer>::Create(metalBuffer);
}

TextureHandle MetalDevice::CreateTexture(const TextureDesc &desc)
{
	MTL::PixelFormat pixelFormat = TextureFormatToMetal(desc.format);
	if (pixelFormat == MTL::PixelFormatInvalid)
	{
		throw std::runtime_error("Unsupported texture format for Metal texture creation");
	}

	MTL::TextureDescriptor *textureDesc = MTL::TextureDescriptor::alloc()->init();
	textureDesc->setTextureType(TextureTypeToMetal(desc.type, desc.isCubeMap));
	textureDesc->setPixelFormat(pixelFormat);
	textureDesc->setWidth(std::max(desc.width, 1u));
	textureDesc->setHeight(std::max(desc.height, 1u));
	textureDesc->setDepth(std::max(desc.depth, 1u));
	textureDesc->setMipmapLevelCount(std::max(desc.mipLevels, 1u));
	textureDesc->setArrayLength(std::max(desc.arrayLayers, 1u));
	textureDesc->setUsage(MakeTextureUsage(desc));
	textureDesc->setStorageMode(SelectStorageMode(desc.resourceUsage, desc.hints, unifiedMemory_));

	MTL::Texture *texture = device_->newTexture(textureDesc);
	textureDesc->release();

	if (texture == nullptr)
	{
		throw std::runtime_error("Failed to create Metal texture");
	}

	if (desc.initialData != nullptr && desc.initialDataSize > 0)
	{
		throw std::runtime_error("Initial texture data upload is not implemented for Metal backend yet");
	}

	return RefCntPtr<IRHITexture>::Create(new MetalTexture(texture, desc, true));
}

TextureViewHandle MetalDevice::CreateTextureView(const TextureViewDesc &desc)
{
	return RefCntPtr<IRHITextureView>::Create(new MetalTextureView(desc));
}

SamplerHandle MetalDevice::CreateSampler(const SamplerDesc &desc)
{
	MTL::SamplerDescriptor *samplerDesc = MTL::SamplerDescriptor::alloc()->init();
	samplerDesc->setMinFilter(FilterModeToMetal(desc.minFilter));
	samplerDesc->setMagFilter(FilterModeToMetal(desc.magFilter));
	samplerDesc->setMipFilter(MipmapModeToMetal(desc.mipmapMode));
	samplerDesc->setSAddressMode(SamplerAddressModeToMetal(desc.addressModeU));
	samplerDesc->setTAddressMode(SamplerAddressModeToMetal(desc.addressModeV));
	samplerDesc->setRAddressMode(SamplerAddressModeToMetal(desc.addressModeW));
	samplerDesc->setLodMinClamp(desc.minLod);
	samplerDesc->setLodMaxClamp(desc.maxLod);
	samplerDesc->setCompareFunction(desc.compareEnable ? CompareOpToMetal(desc.compareOp) : MTL::CompareFunctionAlways);
	samplerDesc->setMaxAnisotropy(desc.anisotropyEnable ? static_cast<NS::UInteger>(std::max(1.0f, desc.maxAnisotropy)) : 1);
	samplerDesc->setBorderColor(BorderColorToMetal(desc.borderColor));
	samplerDesc->setNormalizedCoordinates(!desc.unnormalizedCoordinates);

	// Required for Metal Shader Converter generated pipelines that use argument buffers.
	samplerDesc->setSupportArgumentBuffers(true);

	MTL::SamplerState *sampler = device_->newSamplerState(samplerDesc);
	samplerDesc->release();

	if (sampler == nullptr)
	{
		throw std::runtime_error("Failed to create Metal sampler state");
	}

	return RefCntPtr<IRHISampler>::Create(new MetalSampler(desc, sampler));
}

ShaderHandle MetalDevice::CreateShader(const ShaderDesc &desc)
{
	if (desc.code == nullptr || desc.codeSize == 0)
	{
		throw std::invalid_argument("ShaderDesc requires non-empty shader bytecode");
	}

	void *libraryData = std::malloc(desc.codeSize);
	if (libraryData == nullptr)
	{
		throw std::bad_alloc();
	}
	std::memcpy(libraryData, desc.code, desc.codeSize);

	dispatch_data_t dispatchData = dispatch_data_create(
	    libraryData,
	    desc.codeSize,
	    dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
	    DISPATCH_DATA_DESTRUCTOR_FREE);
	if (dispatchData == nullptr)
	{
		std::free(libraryData);
		throw std::runtime_error("Failed to create dispatch data for Metal library bytecode");
	}

	NS::Error    *error   = nullptr;
	MTL::Library *library = device_->newLibrary(dispatchData, &error);

#if !defined(OS_OBJECT_USE_OBJC) || !OS_OBJECT_USE_OBJC
	dispatch_release(dispatchData);
#endif

	if (library == nullptr)
	{
		throw std::runtime_error(ToErrorMessage(error, "Failed to create Metal library from .metallib bytecode"));
	}

	const char    *entryPoint     = desc.entryPoint != nullptr ? desc.entryPoint : "main";
	NS::String    *entryPointName = NS::String::string(entryPoint, NS::UTF8StringEncoding);
	MTL::Function *function       = library->newFunction(entryPointName);
	if (function == nullptr)
	{
		library->release();
		throw std::runtime_error(std::string("Failed to find shader entry point in Metal library: ") + entryPoint);
	}

	return RefCntPtr<IRHIShader>::Create(new MetalShader(desc, library, function));
}

PipelineHandle MetalDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)
{
	auto *vertexShader   = dynamic_cast<MetalShader *>(desc.vertexShader);
	auto *fragmentShader = dynamic_cast<MetalShader *>(desc.fragmentShader);
	if (vertexShader == nullptr || fragmentShader == nullptr)
	{
		throw std::runtime_error("CreateGraphicsPipeline requires Metal vertex and fragment shaders");
	}

	MTL::RenderPipelineDescriptor *pipelineDesc = MTL::RenderPipelineDescriptor::alloc()->init();
	pipelineDesc->setVertexFunction(vertexShader->GetFunction());
	pipelineDesc->setFragmentFunction(fragmentShader->GetFunction());
	pipelineDesc->setInputPrimitiveTopology(PrimitiveTopologyClassToMetal(desc.topology));
	pipelineDesc->setSampleCount(SampleCountToUInt(desc.targetSignature.sampleCount));

	MTL::VertexDescriptor *vertexDesc = MTL::VertexDescriptor::alloc()->init();
	for (const VertexBinding &binding : desc.vertexLayout.bindings)
	{
		MTL::VertexBufferLayoutDescriptor *layoutDesc =
		    vertexDesc->layouts()->object(binding.binding + kMetalShaderConverterVertexBufferBase);
		layoutDesc->setStride(binding.stride);
		layoutDesc->setStepRate(1);
		layoutDesc->setStepFunction(binding.perInstance ? MTL::VertexStepFunctionPerInstance : MTL::VertexStepFunctionPerVertex);
	}
	for (const VertexAttribute &attribute : desc.vertexLayout.attributes)
	{
		MTL::VertexAttributeDescriptor *attributeDesc = vertexDesc->attributes()->object(
		    attribute.location + kMetalShaderConverterVertexAttributeBase);
		attributeDesc->setFormat(VertexFormatToMetal(attribute.format));
		attributeDesc->setOffset(attribute.offset);
		attributeDesc->setBufferIndex(attribute.binding + kMetalShaderConverterVertexBufferBase);
	}
	pipelineDesc->setVertexDescriptor(vertexDesc);
	vertexDesc->release();

	for (size_t i = 0; i < desc.targetSignature.colorFormats.size(); ++i)
	{
		MTL::RenderPipelineColorAttachmentDescriptor *colorAttachment = pipelineDesc->colorAttachments()->object(i);
		colorAttachment->setPixelFormat(TextureFormatToMetal(desc.targetSignature.colorFormats[i]));

		static const ColorBlendAttachmentState defaultBlendState{};
		const ColorBlendAttachmentState       *blendState = &defaultBlendState;
		if (!desc.colorBlendAttachments.empty())
		{
			blendState = i < desc.colorBlendAttachments.size() ? &desc.colorBlendAttachments[i] : &desc.colorBlendAttachments.front();
		}
		colorAttachment->setBlendingEnabled(blendState->blendEnable);
		colorAttachment->setSourceRGBBlendFactor(BlendFactorToMetal(blendState->srcColorBlendFactor));
		colorAttachment->setDestinationRGBBlendFactor(BlendFactorToMetal(blendState->dstColorBlendFactor));
		colorAttachment->setRgbBlendOperation(BlendOpToMetal(blendState->colorBlendOp));
		colorAttachment->setSourceAlphaBlendFactor(BlendFactorToMetal(blendState->srcAlphaBlendFactor));
		colorAttachment->setDestinationAlphaBlendFactor(BlendFactorToMetal(blendState->dstAlphaBlendFactor));
		colorAttachment->setAlphaBlendOperation(BlendOpToMetal(blendState->alphaBlendOp));
		colorAttachment->setWriteMask(ColorWriteMaskToMetal(blendState->colorWriteMask));
	}

	if (desc.targetSignature.depthFormat != TextureFormat::UNDEFINED)
	{
		const MTL::PixelFormat depthFormat = TextureFormatToMetal(desc.targetSignature.depthFormat);
		pipelineDesc->setDepthAttachmentPixelFormat(depthFormat);
		if (HasStencil(desc.targetSignature.depthFormat))
		{
			pipelineDesc->setStencilAttachmentPixelFormat(depthFormat);
		}
	}

	NS::Error                *error         = nullptr;
	MTL::RenderPipelineState *pipelineState = device_->newRenderPipelineState(pipelineDesc, &error);
	pipelineDesc->release();
	if (pipelineState == nullptr)
	{
		throw std::runtime_error(ToErrorMessage(error, "Failed to create Metal render pipeline state"));
	}

	MTL::DepthStencilDescriptor *depthDesc = MTL::DepthStencilDescriptor::alloc()->init();
	depthDesc->setDepthCompareFunction(
	    desc.depthStencilState.depthTestEnable ? CompareOpToMetal(desc.depthStencilState.depthCompareOp) : MTL::CompareFunctionAlways);
	depthDesc->setDepthWriteEnabled(desc.depthStencilState.depthWriteEnable);

	if (desc.depthStencilState.stencilTestEnable)
	{
		MTL::StencilDescriptor *frontStencil = MTL::StencilDescriptor::alloc()->init();
		MTL::StencilDescriptor *backStencil  = MTL::StencilDescriptor::alloc()->init();
		ConfigureStencilDesc(frontStencil, desc.depthStencilState.front);
		ConfigureStencilDesc(backStencil, desc.depthStencilState.back);
		depthDesc->setFrontFaceStencil(frontStencil);
		depthDesc->setBackFaceStencil(backStencil);
		frontStencil->release();
		backStencil->release();
	}

	MTL::DepthStencilState *depthState = device_->newDepthStencilState(depthDesc);
	depthDesc->release();

	return RefCntPtr<IRHIPipeline>::Create(new MetalPipeline(desc, pipelineState, depthState));
}

PipelineHandle MetalDevice::CreateComputePipeline(const ComputePipelineDesc &desc)
{
	auto *computeShader = dynamic_cast<MetalShader *>(desc.computeShader);
	if (computeShader == nullptr)
	{
		throw std::runtime_error("CreateComputePipeline requires a Metal compute shader");
	}

	NS::Error                 *error        = nullptr;
	MTL::ComputePipelineState *computeState = device_->newComputePipelineState(computeShader->GetFunction(), &error);
	if (computeState == nullptr)
	{
		throw std::runtime_error(ToErrorMessage(error, "Failed to create Metal compute pipeline state"));
	}

	const NS::UInteger width                 = std::max<NS::UInteger>(computeState->threadExecutionWidth(), 1);
	MTL::Size          threadsPerThreadgroup = {width, 1, 1};

	return RefCntPtr<IRHIPipeline>::Create(new MetalPipeline(desc, computeState, threadsPerThreadgroup));
}

CommandListHandle MetalDevice::CreateCommandList(QueueType queueType)
{
	return RefCntPtr<IRHICommandList>::Create(new MetalCommandList(this, queueType));
}

SwapchainHandle MetalDevice::CreateSwapchain(const SwapchainDesc &desc)
{
	return RefCntPtr<IRHISwapchain>::Create(new MetalSwapchain(this, desc));
}

SemaphoreHandle MetalDevice::CreateSemaphore()
{
	return RefCntPtr<IRHISemaphore>::Create(new MetalSemaphore());
}

FenceHandle MetalDevice::CreateFence(bool signaled)
{
	return RefCntPtr<IRHIFence>::Create(new MetalFence(signaled));
}

FenceHandle MetalDevice::CreateCompositeFence(const std::vector<FenceHandle> &fences)
{
	std::vector<FenceHandle> copiedFences(fences.begin(), fences.end());
	return RefCntPtr<IRHIFence>::Create(new MetalCompositeFence(std::move(copiedFences)));
}

DescriptorSetLayoutHandle MetalDevice::CreateDescriptorSetLayout(const DescriptorSetLayoutDesc &desc)
{
	return RefCntPtr<IRHIDescriptorSetLayout>::Create(new MetalDescriptorSetLayout(desc));
}

DescriptorSetHandle MetalDevice::CreateDescriptorSet(IRHIDescriptorSetLayout *layout, QueueType queueType)
{
	(void) queueType;

	if (layout == nullptr)
	{
		throw std::invalid_argument("MetalDescriptorSet requires a valid descriptor set layout");
	}

	return RefCntPtr<IRHIDescriptorSet>::Create(new MetalDescriptorSet(layout));
}

QueryPoolHandle MetalDevice::CreateQueryPool(const QueryPoolDesc &desc)
{
	return RefCntPtr<IRHIQueryPool>::Create(new MetalQueryPool(desc));
}

void MetalDevice::UpdateBuffer(IRHIBuffer *buffer, const void *data, size_t size, size_t offset)
{
	if (buffer == nullptr)
	{
		throw std::invalid_argument("UpdateBuffer called with null buffer");
	}
	if (data == nullptr && size > 0)
	{
		throw std::invalid_argument("UpdateBuffer called with null data and non-zero size");
	}

	auto *metalBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (metalBuffer == nullptr)
	{
		throw std::runtime_error("UpdateBuffer requires a MetalBuffer instance");
	}

	metalBuffer->Update(data, size, offset);
}

FenceHandle MetalDevice::UploadBufferAsync(IRHIBuffer *dstBuffer, const void *data, size_t size, size_t offset)
{
	if (dstBuffer == nullptr)
	{
		throw std::invalid_argument("UploadBufferAsync called with null destination buffer");
	}
	if (data == nullptr && size > 0)
	{
		throw std::invalid_argument("UploadBufferAsync called with null data and non-zero size");
	}

	auto *metalBuffer = dynamic_cast<MetalBuffer *>(dstBuffer);
	if (metalBuffer == nullptr)
	{
		throw std::runtime_error("UploadBufferAsync requires a MetalBuffer destination");
	}

	if (metalBuffer->IsCpuVisible())
	{
		metalBuffer->Update(data, size, offset);
		return RefCntPtr<IRHIFence>::Create(new MetalFence(true));
	}

	MTL::ResourceOptions stagingOptions = MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined;
	MTL::Buffer         *stagingBuffer  = device_->newBuffer(size, stagingOptions);
	if (stagingBuffer == nullptr)
	{
		throw std::runtime_error("Failed to create Metal staging buffer for async upload");
	}

	std::memcpy(stagingBuffer->contents(), data, size);

	MTL::CommandBuffer *commandBuffer = transferQueue_->commandBuffer();
	if (commandBuffer == nullptr)
	{
		stagingBuffer->release();
		throw std::runtime_error("Failed to allocate Metal command buffer for async upload");
	}
	commandBuffer->retain();

	MTL::BlitCommandEncoder *blit = commandBuffer->blitCommandEncoder();
	if (blit == nullptr)
	{
		commandBuffer->release();
		stagingBuffer->release();
		throw std::runtime_error("Failed to allocate Metal blit encoder for async upload");
	}

	blit->copyFromBuffer(stagingBuffer, 0, metalBuffer->GetHandle(), offset, size);
	blit->endEncoding();

	auto       *fenceRaw = new MetalFence(false);
	FenceHandle fence    = RefCntPtr<IRHIFence>::Create(fenceRaw);

	fenceRaw->AddRef();
	commandBuffer->addCompletedHandler([fenceRaw, stagingBuffer, commandBuffer](MTL::CommandBuffer *) {
		fenceRaw->Signal();
		fenceRaw->Release();
		stagingBuffer->release();
		commandBuffer->release();
	});

	commandBuffer->commit();
	return fence;
}

FenceHandle MetalDevice::UploadBufferAsync(const BufferHandle &dstBuffer, const void *data, size_t size,
                                           size_t offset)
{
	return UploadBufferAsync(dstBuffer.Get(), data, size, offset);
}

void MetalDevice::SubmitCommandLists(std::span<IRHICommandList *const> cmdLists, QueueType queueType,
                                     IRHISemaphore *waitSemaphore, IRHISemaphore *signalSemaphore,
                                     IRHIFence *signalFence)
{
	SemaphoreWaitInfo                  waitInfo{};
	std::span<const SemaphoreWaitInfo> waitSemaphores;
	if (waitSemaphore != nullptr)
	{
		waitInfo       = SemaphoreWaitInfo{waitSemaphore, StageMask::AllCommands};
		waitSemaphores = std::span<const SemaphoreWaitInfo>(&waitInfo, 1);
	}

	IRHISemaphore                  *signalSemaphoreRaw = signalSemaphore;
	std::span<IRHISemaphore *const> signalSemaphores;
	if (signalSemaphoreRaw != nullptr)
	{
		signalSemaphores = std::span<IRHISemaphore *const>(&signalSemaphoreRaw, 1);
	}

	SubmitInfo submitInfo{};
	submitInfo.waitSemaphores   = waitSemaphores;
	submitInfo.signalSemaphores = signalSemaphores;
	submitInfo.signalFence      = signalFence;

	SubmitCommandLists(cmdLists, queueType, submitInfo);
}

void MetalDevice::SubmitCommandLists(std::span<IRHICommandList *const> cmdLists, QueueType queueType,
                                     const SubmitInfo &submitInfo)
{
	std::lock_guard<std::mutex> lock(submitMutex_);

	if (cmdLists.empty())
	{
		SignalFence(submitInfo.signalFence);
		return;
	}

	std::vector<MTL::CommandBuffer *> commandBuffers;
	commandBuffers.reserve(cmdLists.size());

	for (IRHICommandList *cmdList : cmdLists)
	{
		auto *metalCmdList = dynamic_cast<MetalCommandList *>(cmdList);
		if (metalCmdList == nullptr)
		{
			throw std::runtime_error("SubmitCommandLists requires Metal command lists");
		}
		if (metalCmdList->GetQueueType() != queueType)
		{
			throw std::runtime_error("SubmitCommandLists queue type mismatch");
		}

		MTL::CommandBuffer *commandBuffer = metalCmdList->GetHandle();
		if (commandBuffer == nullptr)
		{
			throw std::runtime_error("Cannot submit command list before Begin/End recording");
		}
		commandBuffers.push_back(commandBuffer);
	}

	if (submitInfo.signalFence != nullptr)
	{
		auto *fence = dynamic_cast<MetalFence *>(submitInfo.signalFence);
		if (fence != nullptr)
		{
			fence->AddRef();
			commandBuffers.back()->addCompletedHandler([fence](MTL::CommandBuffer *) {
				fence->Signal();
				fence->Release();
			});
		}
	}

	for (MTL::CommandBuffer *commandBuffer : commandBuffers)
	{
		commandBuffer->commit();
	}
}

void MetalDevice::WaitForQueue(MTL::CommandQueue *queue) const
{
	if (queue == nullptr)
	{
		return;
	}

	MTL::CommandBuffer *fenceBuffer = queue->commandBuffer();
	if (fenceBuffer == nullptr)
	{
		return;
	}

	fenceBuffer->commit();
	fenceBuffer->waitUntilCompleted();
}

void MetalDevice::WaitQueueIdle(QueueType queueType)
{
	WaitForQueue(GetCommandQueue(queueType));
}

void MetalDevice::WaitIdle()
{
	WaitForQueue(graphicsQueue_);
	WaitForQueue(computeQueue_);
	WaitForQueue(transferQueue_);
}

void MetalDevice::RetireCompletedFrame()
{}

double MetalDevice::GetTimestampPeriod() const
{
	return 1.0;
}

bool MetalDevice::GetQueryPoolResults(IRHIQueryPool *queryPool, uint32_t firstQuery, uint32_t queryCount,
                                      void *data, size_t dataSize, size_t stride, QueryResultFlags flags)
{
	(void) queryPool;
	(void) firstQuery;
	(void) queryCount;
	(void) stride;
	(void) flags;

	if (data != nullptr && dataSize > 0)
	{
		std::memset(data, 0, dataSize);
	}
	return true;
}

GpuMemoryStats MetalDevice::GetMemoryStats() const
{
	GpuMemoryStats stats{};

	const uint64_t allocated = static_cast<uint64_t>(device_->currentAllocatedSize());
	const uint64_t budget    = device_->recommendedMaxWorkingSetSize();

	stats.deviceLocalUsage  = allocated;
	stats.deviceLocalBudget = budget;

	if (unifiedMemory_)
	{
		stats.hostVisibleUsage  = allocated;
		stats.hostVisibleBudget = budget;
	}

	stats.totalUsage  = allocated;
	stats.totalBudget = budget;
	return stats;
}

MTL::Device *MetalDevice::GetMTLDevice() const
{
	return device_;
}

MTL::CommandQueue *MetalDevice::GetCommandQueue(QueueType queueType) const
{
	switch (queueType)
	{
		case QueueType::GRAPHICS:
			return graphicsQueue_;
		case QueueType::COMPUTE:
			return computeQueue_;
		case QueueType::TRANSFER:
			return transferQueue_;
	}

	return graphicsQueue_;
}

bool MetalDevice::IsUnifiedMemory() const
{
	return unifiedMemory_;
}

void MetalDevice::SignalFence(IRHIFence *fence)
{
	auto *metalFence = dynamic_cast<MetalFence *>(fence);
	if (metalFence != nullptr)
	{
		metalFence->Signal();
	}
}

}        // namespace rhi::metal3

namespace rhi
{

DeviceHandle CreateRHIDevice()
{
	return RefCntPtr<IRHIDevice>::Create(new metal3::MetalDevice());
}

}        // namespace rhi
