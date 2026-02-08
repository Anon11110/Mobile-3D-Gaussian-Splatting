#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

#include "metal_backend.h"

namespace rhi::metal3
{

[[noreturn]] void ThrowMetalPhase0NotImplemented(const char *apiName)
{
	throw std::runtime_error(
	    std::string("Metal3 backend phase 0 skeleton: ") + apiName + " is not implemented yet");
}

MetalDevice::MetalDevice() = default;

MetalDevice::~MetalDevice() = default;

BufferHandle MetalDevice::CreateBuffer(const BufferDesc &desc)
{
	MetalBuffer *buffer = new MetalBuffer(desc);
	if (desc.initialData != nullptr)
	{
		buffer->Update(desc.initialData, desc.size, 0);
	}

	return RefCntPtr<IRHIBuffer>::Create(buffer);
}

TextureHandle MetalDevice::CreateTexture(const TextureDesc &desc)
{
	return RefCntPtr<IRHITexture>::Create(new MetalTexture(desc));
}

TextureViewHandle MetalDevice::CreateTextureView(const TextureViewDesc &desc)
{
	return RefCntPtr<IRHITextureView>::Create(new MetalTextureView(desc));
}

SamplerHandle MetalDevice::CreateSampler(const SamplerDesc &desc)
{
	return RefCntPtr<IRHISampler>::Create(new MetalSampler(desc));
}

ShaderHandle MetalDevice::CreateShader(const ShaderDesc &desc)
{
	return RefCntPtr<IRHIShader>::Create(new MetalShader(desc));
}

PipelineHandle MetalDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc &desc)
{
	return RefCntPtr<IRHIPipeline>::Create(new MetalPipeline(desc));
}

PipelineHandle MetalDevice::CreateComputePipeline(const ComputePipelineDesc &desc)
{
	return RefCntPtr<IRHIPipeline>::Create(new MetalPipeline(desc));
}

CommandListHandle MetalDevice::CreateCommandList(QueueType queueType)
{
	return RefCntPtr<IRHICommandList>::Create(new MetalCommandList(queueType));
}

SwapchainHandle MetalDevice::CreateSwapchain(const SwapchainDesc &desc)
{
	return RefCntPtr<IRHISwapchain>::Create(new MetalSwapchain(desc));
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
	UpdateBuffer(dstBuffer, data, size, offset);
	return RefCntPtr<IRHIFence>::Create(new MetalFence(true));
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
	(void) cmdLists;
	(void) queueType;
	(void) waitSemaphore;
	(void) signalSemaphore;
	SignalFence(signalFence);
}

void MetalDevice::SubmitCommandLists(std::span<IRHICommandList *const> cmdLists, QueueType queueType,
                                     const SubmitInfo &submitInfo)
{
	(void) cmdLists;
	(void) queueType;
	SignalFence(submitInfo.signalFence);
}

void MetalDevice::WaitQueueIdle(QueueType queueType)
{
	(void) queueType;
}

void MetalDevice::WaitIdle()
{}

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
	return {};
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
