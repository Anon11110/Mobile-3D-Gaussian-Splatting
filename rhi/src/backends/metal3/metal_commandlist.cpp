#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "metal_backend.h"
#include "metal_conversions.h"

namespace rhi::metal3
{
namespace
{

constexpr uint32_t kBindingsPerSet = 32;
// Some Metal pipelines validate buffer index with "< 31", so reserve slot 30.
constexpr uint32_t kPushConstantSlot                       = 30;
constexpr uint32_t kIRArgumentBufferBindPoint              = 2;
constexpr uint32_t kIRVertexBufferBindPoint                = 6;
constexpr uint32_t kIRArgumentBufferDrawArgumentsBindPoint = 4;
constexpr uint32_t kIRArgumentBufferUniformsBindPoint      = 5;
constexpr size_t   kPushConstantBackingBytes               = 4096;
constexpr size_t   kTopLevelArgumentBufferInitialBytes     = 64 * 1024;
constexpr uint16_t kIRNonIndexedDraw                       = 0;

struct IRRuntimeDrawArgument
{
	uint32_t vertexCountPerInstance;
	uint32_t instanceCount;
	uint32_t startVertexLocation;
	uint32_t startInstanceLocation;
};

struct IRRuntimeDrawIndexedArgument
{
	uint32_t indexCountPerInstance;
	uint32_t instanceCount;
	uint32_t startIndexLocation;
	int32_t  baseVertexLocation;
	uint32_t startInstanceLocation;
};

union IRRuntimeDrawParams
{
	IRRuntimeDrawArgument        draw;
	IRRuntimeDrawIndexedArgument drawIndexed;
};

template <typename T>
T AlignUp(T value, T alignment)
{
	const T mask = alignment - 1;
	return (value + mask) & ~mask;
}

size_t IndexTypeSize(IndexType type)
{
	switch (type)
	{
		case IndexType::UINT16:
			return 2;
		case IndexType::UINT32:
			return 4;
	}

	return 4;
}

uint16_t IRIndexType(IndexType type)
{
	return static_cast<uint16_t>(IndexTypeToMetal(type)) + 1;
}

}        // namespace

MetalCommandList::MetalCommandList(MetalDevice *device, QueueType queueType) :
    device_(device), queueType_(queueType)
{}

MetalCommandList::~MetalCommandList()
{
	Reset();

	if (pushConstantBackingBuffer_ != nullptr)
	{
		pushConstantBackingBuffer_->release();
		pushConstantBackingBuffer_ = nullptr;
	}
	if (topLevelArgumentBuffer_ != nullptr)
	{
		topLevelArgumentBuffer_->release();
		topLevelArgumentBuffer_         = nullptr;
		topLevelArgumentBufferCapacity_ = 0;
		topLevelArgumentBufferOffset_   = 0;
	}
}

void MetalCommandList::RequireRecording(const char *apiName) const
{
	if (!isRecording_)
	{
		throw std::runtime_error(std::string(apiName) + " requires an active recording session (Begin/End)");
	}
}

void MetalCommandList::EndActiveEncoders()
{
	if (renderEncoder_ != nullptr)
	{
		renderEncoder_->endEncoding();
		renderEncoder_ = nullptr;
	}
	if (computeEncoder_ != nullptr)
	{
		computeEncoder_->endEncoding();
		computeEncoder_ = nullptr;
	}
	if (blitEncoder_ != nullptr)
	{
		blitEncoder_->endEncoding();
		blitEncoder_ = nullptr;
	}
}

MTL::BlitCommandEncoder *MetalCommandList::EnsureBlitEncoder()
{
	RequireRecording("Blit encoder access");
	if (blitEncoder_ != nullptr)
	{
		return blitEncoder_;
	}

	if (renderEncoder_ != nullptr)
	{
		EndRendering();
	}
	if (computeEncoder_ != nullptr)
	{
		computeEncoder_->endEncoding();
		computeEncoder_ = nullptr;
	}

	blitEncoder_ = commandBuffer_->blitCommandEncoder();
	if (blitEncoder_ == nullptr)
	{
		throw std::runtime_error("Failed to create Metal blit encoder");
	}
	return blitEncoder_;
}

MTL::ComputeCommandEncoder *MetalCommandList::EnsureComputeEncoder()
{
	RequireRecording("Compute encoder access");
	if (computeEncoder_ != nullptr)
	{
		return computeEncoder_;
	}

	if (renderEncoder_ != nullptr)
	{
		EndRendering();
	}
	if (blitEncoder_ != nullptr)
	{
		blitEncoder_->endEncoding();
		blitEncoder_ = nullptr;
	}

	computeEncoder_ = commandBuffer_->computeCommandEncoder();
	if (computeEncoder_ == nullptr)
	{
		throw std::runtime_error("Failed to create Metal compute encoder");
	}

	ApplyCurrentPipelineState();
	return computeEncoder_;
}

void MetalCommandList::ApplyCurrentPipelineState()
{
	if (currentPipeline_ == nullptr)
	{
		return;
	}

	if (renderEncoder_ != nullptr && currentPipeline_->GetPipelineType() == PipelineType::GRAPHICS)
	{
		renderEncoder_->setRenderPipelineState(currentPipeline_->GetRenderPipelineState());
		if (currentPipeline_->GetDepthStencilState() != nullptr)
		{
			renderEncoder_->setDepthStencilState(currentPipeline_->GetDepthStencilState());
		}
		renderEncoder_->setCullMode(currentPipeline_->GetCullMode());
		renderEncoder_->setFrontFacingWinding(currentPipeline_->GetFrontFacingWinding());
		renderEncoder_->setTriangleFillMode(currentPipeline_->GetTriangleFillMode());
		if (currentPipeline_->IsDepthBiasEnabled())
		{
			renderEncoder_->setDepthBias(currentPipeline_->GetDepthBiasConstantFactor(),
			                             currentPipeline_->GetDepthBiasSlopeFactor(),
			                             currentPipeline_->GetDepthBiasClamp());
		}
	}

	if (computeEncoder_ != nullptr && currentPipeline_->GetPipelineType() == PipelineType::COMPUTE)
	{
		computeEncoder_->setComputePipelineState(currentPipeline_->GetComputePipelineState());
	}
}

void MetalCommandList::ClearTrackedBindings()
{
	for (TrackedBufferBinding &binding : vertexTrackedBuffers_)
	{
		binding = {};
	}
	for (TrackedBufferBinding &binding : fragmentTrackedBuffers_)
	{
		binding = {};
	}
	for (TrackedBufferBinding &binding : computeTrackedBuffers_)
	{
		binding = {};
	}
}

void MetalCommandList::TrackBufferBinding(ShaderStageFlags stageFlags, uint32_t slot, MTL::Buffer *buffer,
                                          size_t offset, size_t range)
{
	if (slot >= vertexTrackedBuffers_.size() || buffer == nullptr)
	{
		return;
	}

	const uint64_t clampedOffset = static_cast<uint64_t>(offset);
	uint64_t       clampedSize   = static_cast<uint64_t>(range);
	const uint64_t bufferLength  = static_cast<uint64_t>(buffer->length());
	if (clampedOffset >= bufferLength)
	{
		return;
	}
	if (clampedSize == 0 || clampedOffset + clampedSize > bufferLength)
	{
		clampedSize = bufferLength - clampedOffset;
	}

	TrackedBufferBinding tracked{};
	tracked.buffer = buffer;
	tracked.offset = clampedOffset;
	tracked.size   = clampedSize;
	tracked.valid  = true;

	if (HasShaderStage(stageFlags, ShaderStageFlags::VERTEX))
	{
		vertexTrackedBuffers_[slot] = tracked;
	}
	if (HasShaderStage(stageFlags, ShaderStageFlags::FRAGMENT))
	{
		fragmentTrackedBuffers_[slot] = tracked;
	}
	if (HasShaderStage(stageFlags, ShaderStageFlags::COMPUTE))
	{
		computeTrackedBuffers_[slot] = tracked;
	}
}

void MetalCommandList::EnsureTopLevelArgumentBufferCapacity(size_t requiredBytes)
{
	if (requiredBytes == 0)
	{
		return;
	}

	if (topLevelArgumentBuffer_ != nullptr && requiredBytes <= topLevelArgumentBufferCapacity_)
	{
		return;
	}

	const size_t newCapacity = std::max(
	    AlignUp(requiredBytes, static_cast<size_t>(4096)),
	    topLevelArgumentBufferCapacity_ == 0 ? kTopLevelArgumentBufferInitialBytes : topLevelArgumentBufferCapacity_ * 2);

	MTL::ResourceOptions options   = MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined;
	MTL::Buffer         *newBuffer = device_->GetMTLDevice()->newBuffer(newCapacity, options);
	if (newBuffer == nullptr)
	{
		throw std::runtime_error("Failed to allocate Metal top-level argument buffer");
	}

	if (topLevelArgumentBuffer_ != nullptr)
	{
		topLevelArgumentBuffer_->release();
	}

	topLevelArgumentBuffer_         = newBuffer;
	topLevelArgumentBufferCapacity_ = newCapacity;
	topLevelArgumentBufferOffset_   = 0;
}

void MetalCommandList::ResetTopLevelArgumentBufferAllocator()
{
	topLevelArgumentBufferOffset_ = 0;
}

MetalCommandList::TopLevelArgumentAllocation MetalCommandList::AllocateTopLevelArgumentData(size_t bytes, size_t alignment)
{
	if (bytes == 0)
	{
		return {};
	}
	if (alignment == 0 || (alignment & (alignment - 1)) != 0)
	{
		throw std::runtime_error("Top-level argument allocation alignment must be a non-zero power of two");
	}

	size_t alignedOffset = AlignUp(topLevelArgumentBufferOffset_, alignment);
	size_t requiredBytes = alignedOffset + bytes;
	EnsureTopLevelArgumentBufferCapacity(requiredBytes);

	alignedOffset = AlignUp(topLevelArgumentBufferOffset_, alignment);
	requiredBytes = alignedOffset + bytes;
	if (requiredBytes > topLevelArgumentBufferCapacity_)
	{
		throw std::runtime_error("Top-level argument buffer allocation exceeded capacity");
	}

	topLevelArgumentBufferOffset_ = requiredBytes;

	TopLevelArgumentAllocation allocation{};
	allocation.offset = alignedOffset;
	allocation.cpuPtr = reinterpret_cast<std::byte *>(topLevelArgumentBuffer_->contents()) + alignedOffset;
	return allocation;
}

void MetalCommandList::ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags stage)
{
	if (renderEncoder_ == nullptr)
	{
		return;
	}

	const std::array<TrackedBufferBinding, 31> *bindings = nullptr;
	MTL::RenderStages                           metalStageMask{};
	if (stage == ShaderStageFlags::VERTEX)
	{
		bindings       = &vertexTrackedBuffers_;
		metalStageMask = MTL::RenderStageVertex;
	}
	else if (stage == ShaderStageFlags::FRAGMENT)
	{
		bindings       = &fragmentTrackedBuffers_;
		metalStageMask = MTL::RenderStageFragment;
	}
	else
	{
		return;
	}

	struct TopLevelBufferEntry
	{
		uint64_t gpuAddress;
		uint64_t textureOrSamplerHandle;
		uint64_t flags;
	};

	std::array<TopLevelBufferEntry, 31> entries{};
	std::array<MTL::Buffer *, 31>       resources{};
	size_t                              entryCount = 0;

	// Metal shader converter linear layout addresses resources by compact binding
	// index, not raw register number. Pack valid slots densely in ascending slot order.
	for (size_t slot = 0; slot < bindings->size(); ++slot)
	{
		const TrackedBufferBinding &binding = (*bindings)[slot];
		if (!binding.valid || binding.buffer == nullptr)
		{
			continue;
		}

		TopLevelBufferEntry &entry   = entries[entryCount];
		entry.gpuAddress             = static_cast<uint64_t>(binding.buffer->gpuAddress()) + binding.offset;
		entry.textureOrSamplerHandle = 0;
		entry.flags                  = std::min<uint64_t>(binding.size, 0xFFFFFFFFull);
		resources[entryCount]        = binding.buffer;
		++entryCount;
	}

	if (entryCount == 0)
	{
		return;
	}

	const size_t bytes      = entryCount * sizeof(TopLevelBufferEntry);
	auto         allocation = AllocateTopLevelArgumentData(bytes, 8);
	std::memcpy(allocation.cpuPtr, entries.data(), bytes);

	if (stage == ShaderStageFlags::VERTEX)
	{
		renderEncoder_->setVertexBuffer(topLevelArgumentBuffer_, allocation.offset, kIRArgumentBufferBindPoint);
	}
	else
	{
		renderEncoder_->setFragmentBuffer(topLevelArgumentBuffer_, allocation.offset, kIRArgumentBufferBindPoint);
	}

	for (size_t slot = 0; slot < entryCount; ++slot)
	{
		if (resources[slot] != nullptr)
		{
			renderEncoder_->useResource(resources[slot], MTL::ResourceUsageRead, metalStageMask);
		}
	}
}

void MetalCommandList::ApplyTopLevelArgumentBufferForCompute()
{
	if (computeEncoder_ == nullptr)
	{
		return;
	}

	struct TopLevelBufferEntry
	{
		uint64_t gpuAddress;
		uint64_t textureOrSamplerHandle;
		uint64_t flags;
	};

	std::array<TopLevelBufferEntry, 31> entries{};
	std::array<MTL::Buffer *, 31>       resources{};
	size_t                              entryCount = 0;

	// Metal shader converter linear layout addresses resources by compact binding
	// index, not raw register number. Pack valid slots densely in ascending slot order.
	for (size_t slot = 0; slot < computeTrackedBuffers_.size(); ++slot)
	{
		const TrackedBufferBinding &binding = computeTrackedBuffers_[slot];
		if (!binding.valid || binding.buffer == nullptr)
		{
			continue;
		}

		TopLevelBufferEntry &entry   = entries[entryCount];
		entry.gpuAddress             = static_cast<uint64_t>(binding.buffer->gpuAddress()) + binding.offset;
		entry.textureOrSamplerHandle = 0;
		entry.flags                  = std::min<uint64_t>(binding.size, 0xFFFFFFFFull);
		resources[entryCount]        = binding.buffer;
		++entryCount;
	}

	if (entryCount == 0)
	{
		return;
	}

	const size_t bytes      = entryCount * sizeof(TopLevelBufferEntry);
	auto         allocation = AllocateTopLevelArgumentData(bytes, 8);
	std::memcpy(allocation.cpuPtr, entries.data(), bytes);

	computeEncoder_->setBuffer(topLevelArgumentBuffer_, allocation.offset, kIRArgumentBufferBindPoint);
	for (size_t slot = 0; slot < entryCount; ++slot)
	{
		if (resources[slot] != nullptr)
		{
			computeEncoder_->useResource(resources[slot], MTL::ResourceUsageRead);
		}
	}
}

void MetalCommandList::Begin()
{
	if (isRecording_)
	{
		throw std::runtime_error("MetalCommandList::Begin called while already recording");
	}

	autoreleasePool_ = NS::AutoreleasePool::alloc()->init();

	MTL::CommandQueue *queue = device_->GetCommandQueue(queueType_);
	if (queue == nullptr)
	{
		throw std::runtime_error("Metal command queue is not available");
	}

	commandBuffer_ = queue->commandBuffer();
	if (commandBuffer_ == nullptr)
	{
		throw std::runtime_error("Failed to allocate Metal command buffer");
	}
	commandBuffer_->retain();

	if (pushConstantBackingBuffer_ == nullptr)
	{
		MTL::ResourceOptions options = MTL::ResourceStorageModeShared | MTL::ResourceCPUCacheModeWriteCombined;
		pushConstantBackingBuffer_   = device_->GetMTLDevice()->newBuffer(kPushConstantBackingBytes, options);
		if (pushConstantBackingBuffer_ == nullptr)
		{
			throw std::runtime_error("Failed to allocate Metal push-constant backing buffer");
		}
	}
	EnsureTopLevelArgumentBufferCapacity(kTopLevelArgumentBufferInitialBytes);
	ResetTopLevelArgumentBufferAllocator();

	currentPipeline_   = nullptr;
	indexBuffer_       = nullptr;
	indexBufferOffset_ = 0;
	indexType_         = IndexType::UINT32;
	ClearTrackedBindings();
	isRecording_ = true;
}

void MetalCommandList::End()
{
	RequireRecording("MetalCommandList::End");

	EndActiveEncoders();
	isRecording_ = false;

	if (autoreleasePool_ != nullptr)
	{
		autoreleasePool_->release();
		autoreleasePool_ = nullptr;
	}
}

void MetalCommandList::Reset()
{
	EndActiveEncoders();

	if (commandBuffer_ != nullptr)
	{
		commandBuffer_->release();
		commandBuffer_ = nullptr;
	}

	if (autoreleasePool_ != nullptr)
	{
		autoreleasePool_->release();
		autoreleasePool_ = nullptr;
	}

	currentPipeline_   = nullptr;
	indexBuffer_       = nullptr;
	indexBufferOffset_ = 0;
	indexType_         = IndexType::UINT32;
	ResetTopLevelArgumentBufferAllocator();
	ClearTrackedBindings();
	isRecording_ = false;
}

void MetalCommandList::BeginRendering(const RenderingInfo &info)
{
	RequireRecording("BeginRendering");
	if (renderEncoder_ != nullptr)
	{
		throw std::runtime_error("BeginRendering called while a render pass is already active");
	}

	if (blitEncoder_ != nullptr)
	{
		blitEncoder_->endEncoding();
		blitEncoder_ = nullptr;
	}
	if (computeEncoder_ != nullptr)
	{
		computeEncoder_->endEncoding();
		computeEncoder_ = nullptr;
	}

	MTL::RenderPassDescriptor *renderPassDesc = MTL::RenderPassDescriptor::renderPassDescriptor();
	if (renderPassDesc == nullptr)
	{
		throw std::runtime_error("Failed to allocate Metal render pass descriptor");
	}

	renderPassDesc->setRenderTargetArrayLength(std::max(info.layerCount, 1u));

	for (size_t i = 0; i < info.colorAttachments.size(); ++i)
	{
		const ColorAttachment &attachment = info.colorAttachments[i];
		if (attachment.view == nullptr)
		{
			throw std::invalid_argument("Color attachment view cannot be null");
		}

		auto *view = dynamic_cast<MetalTextureView *>(attachment.view);
		if (view == nullptr)
		{
			throw std::runtime_error("Color attachment is not a Metal texture view");
		}

		MTL::RenderPassColorAttachmentDescriptor *colorAttachment = renderPassDesc->colorAttachments()->object(i);
		colorAttachment->setTexture(view->GetHandle());
		colorAttachment->setLoadAction(LoadOpToMetal(attachment.loadOp));
		colorAttachment->setStoreAction(StoreOpToMetal(attachment.storeOp));
		if (attachment.loadOp == LoadOp::CLEAR)
		{
			colorAttachment->setClearColor(MTL::ClearColor(
			    attachment.clearValue.color[0],
			    attachment.clearValue.color[1],
			    attachment.clearValue.color[2],
			    attachment.clearValue.color[3]));
		}

		if (attachment.resolveTarget != nullptr)
		{
			auto *resolveView = dynamic_cast<MetalTextureView *>(attachment.resolveTarget);
			if (resolveView == nullptr)
			{
				throw std::runtime_error("Resolve target is not a Metal texture view");
			}
			colorAttachment->setResolveTexture(resolveView->GetHandle());
		}
	}

	if (info.depthStencilAttachment.view != nullptr)
	{
		auto *depthView = dynamic_cast<MetalTextureView *>(info.depthStencilAttachment.view);
		if (depthView == nullptr)
		{
			throw std::runtime_error("Depth attachment is not a Metal texture view");
		}

		MTL::RenderPassDepthAttachmentDescriptor *depthAttachment = renderPassDesc->depthAttachment();
		depthAttachment->setTexture(depthView->GetHandle());
		depthAttachment->setLoadAction(LoadOpToMetal(info.depthStencilAttachment.depthLoadOp));
		depthAttachment->setStoreAction(StoreOpToMetal(info.depthStencilAttachment.depthStoreOp));
		if (info.depthStencilAttachment.depthLoadOp == LoadOp::CLEAR)
		{
			depthAttachment->setClearDepth(info.depthStencilAttachment.clearValue.depthStencil.depth);
		}

		if (HasStencil(depthView->GetFormat()))
		{
			MTL::RenderPassStencilAttachmentDescriptor *stencilAttachment = renderPassDesc->stencilAttachment();
			stencilAttachment->setTexture(depthView->GetHandle());
			stencilAttachment->setLoadAction(LoadOpToMetal(info.depthStencilAttachment.stencilLoadOp));
			stencilAttachment->setStoreAction(StoreOpToMetal(info.depthStencilAttachment.stencilStoreOp));
			if (info.depthStencilAttachment.stencilLoadOp == LoadOp::CLEAR)
			{
				stencilAttachment->setClearStencil(info.depthStencilAttachment.clearValue.depthStencil.stencil);
			}
		}
	}

	renderEncoder_ = commandBuffer_->renderCommandEncoder(renderPassDesc);
	if (renderEncoder_ == nullptr)
	{
		throw std::runtime_error("Failed to create Metal render command encoder");
	}

	ApplyCurrentPipelineState();
}

void MetalCommandList::EndRendering()
{
	if (renderEncoder_ != nullptr)
	{
		renderEncoder_->endEncoding();
		renderEncoder_ = nullptr;
	}
}

void MetalCommandList::SetPipeline(IRHIPipeline *pipeline)
{
	RequireRecording("SetPipeline");

	auto *metalPipeline = dynamic_cast<MetalPipeline *>(pipeline);
	if (metalPipeline == nullptr)
	{
		throw std::runtime_error("SetPipeline requires a Metal pipeline");
	}

	currentPipeline_ = metalPipeline;
	ApplyCurrentPipelineState();
}

void MetalCommandList::SetVertexBuffer(uint32_t binding, IRHIBuffer *buffer, size_t offset)
{
	RequireRecording("SetVertexBuffer");
	if (renderEncoder_ == nullptr)
	{
		throw std::runtime_error("SetVertexBuffer requires an active render pass");
	}

	auto *metalBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (metalBuffer == nullptr)
	{
		throw std::runtime_error("SetVertexBuffer requires a Metal buffer");
	}

	renderEncoder_->setVertexBuffer(metalBuffer->GetHandle(), offset, kIRVertexBufferBindPoint + binding);
}

void MetalCommandList::BindIndexBuffer(IRHIBuffer *buffer, size_t offset)
{
	RequireRecording("BindIndexBuffer");
	auto *metalBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (metalBuffer == nullptr)
	{
		throw std::runtime_error("BindIndexBuffer requires a Metal buffer");
	}

	indexBuffer_       = metalBuffer;
	indexBufferOffset_ = offset;
	indexType_         = metalBuffer->GetIndexType();
}

void MetalCommandList::ApplyDescriptorSetBindings(uint32_t setIndex, MetalDescriptorSet *descriptorSet,
                                                  std::span<const uint32_t> dynamicOffsets)
{
	const MetalDescriptorSetLayout *layout = descriptorSet->GetLayout();
	if (layout == nullptr)
	{
		throw std::runtime_error("Descriptor set is missing a Metal layout");
	}

	size_t dynamicOffsetIndex = 0;

	for (const DescriptorBinding &bindingDesc : layout->GetDesc().bindings)
	{
		const uint32_t slot = setIndex * kBindingsPerSet + bindingDesc.binding;

		switch (bindingDesc.type)
		{
			case DescriptorType::UNIFORM_BUFFER:
			case DescriptorType::STORAGE_BUFFER:
			case DescriptorType::UNIFORM_BUFFER_DYNAMIC:
			case DescriptorType::STORAGE_BUFFER_DYNAMIC:
			{
				if (slot >= kPushConstantSlot)
				{
					throw std::runtime_error(
					    "Descriptor buffer slot conflicts with reserved Metal push-constant slot");
				}

				const BufferBinding *bufferBinding = descriptorSet->FindBufferBinding(bindingDesc.binding);
				if (bufferBinding == nullptr)
				{
					continue;
				}

				auto *buffer = dynamic_cast<MetalBuffer *>(bufferBinding->buffer);
				if (buffer == nullptr)
				{
					throw std::runtime_error("Descriptor buffer binding is not a MetalBuffer");
				}

				size_t offset = bufferBinding->offset;
				if (bindingDesc.type == DescriptorType::UNIFORM_BUFFER_DYNAMIC ||
				    bindingDesc.type == DescriptorType::STORAGE_BUFFER_DYNAMIC)
				{
					if (dynamicOffsetIndex < dynamicOffsets.size())
					{
						offset += dynamicOffsets[dynamicOffsetIndex++];
					}
				}

				// Metal Shader Converter descriptor buffers are sourced through the
				// top-level argument buffer at slot 2. Do not bind them directly to
				// stage slots because this can alias other runtime bind points.
				size_t range = bufferBinding->range;
				if (range == 0 && buffer->GetSize() > offset)
				{
					range = buffer->GetSize() - offset;
				}
				TrackBufferBinding(bindingDesc.stageFlags, slot, buffer->GetHandle(), offset, range);
				break;
			}
			case DescriptorType::SAMPLED_TEXTURE:
			case DescriptorType::STORAGE_TEXTURE:
			case DescriptorType::UNIFORM_TEXEL_BUFFER:
			case DescriptorType::STORAGE_TEXEL_BUFFER:
			case DescriptorType::SAMPLER:
			{
				const TextureBinding *textureBinding = descriptorSet->FindTextureBinding(bindingDesc.binding);
				if (textureBinding == nullptr)
				{
					continue;
				}

				MTL::Texture *textureHandle = nullptr;
				if (textureBinding->texture != nullptr)
				{
					auto *texture = dynamic_cast<MetalTexture *>(textureBinding->texture);
					if (texture == nullptr)
					{
						throw std::runtime_error("Descriptor texture binding is not a MetalTexture");
					}
					textureHandle = texture->GetHandle();
				}

				MTL::SamplerState *samplerHandle = nullptr;
				if (textureBinding->sampler != nullptr)
				{
					auto *sampler = dynamic_cast<MetalSampler *>(textureBinding->sampler);
					if (sampler == nullptr)
					{
						throw std::runtime_error("Descriptor sampler binding is not a MetalSampler");
					}
					samplerHandle = sampler->GetHandle();
				}

				if (renderEncoder_ != nullptr)
				{
					if (textureHandle != nullptr)
					{
						if (HasShaderStage(bindingDesc.stageFlags, ShaderStageFlags::VERTEX))
						{
							renderEncoder_->setVertexTexture(textureHandle, slot);
						}
						if (HasShaderStage(bindingDesc.stageFlags, ShaderStageFlags::FRAGMENT))
						{
							renderEncoder_->setFragmentTexture(textureHandle, slot);
						}
					}

					if (samplerHandle != nullptr)
					{
						if (HasShaderStage(bindingDesc.stageFlags, ShaderStageFlags::VERTEX))
						{
							renderEncoder_->setVertexSamplerState(samplerHandle, slot);
						}
						if (HasShaderStage(bindingDesc.stageFlags, ShaderStageFlags::FRAGMENT))
						{
							renderEncoder_->setFragmentSamplerState(samplerHandle, slot);
						}
					}
				}

				if (computeEncoder_ != nullptr && HasShaderStage(bindingDesc.stageFlags, ShaderStageFlags::COMPUTE))
				{
					if (textureHandle != nullptr)
					{
						computeEncoder_->setTexture(textureHandle, slot);
					}
					if (samplerHandle != nullptr)
					{
						computeEncoder_->setSamplerState(samplerHandle, slot);
					}
				}
				break;
			}
		}
	}
}

void MetalCommandList::BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
                                         std::span<const uint32_t> dynamicOffsets)
{
	RequireRecording("BindDescriptorSet");
	auto *metalDescriptorSet = dynamic_cast<MetalDescriptorSet *>(descriptorSet);
	if (metalDescriptorSet == nullptr)
	{
		throw std::runtime_error("BindDescriptorSet requires a Metal descriptor set");
	}

	ApplyDescriptorSetBindings(setIndex, metalDescriptorSet, dynamicOffsets);
}

void MetalCommandList::PushConstants(ShaderStageFlags stageFlags, uint32_t offset,
                                     std::span<const std::byte> data)
{
	RequireRecording("PushConstants");
	if (data.empty())
	{
		return;
	}
	if (offset != 0)
	{
		throw std::runtime_error("Metal push constants currently support only offset=0");
	}
	if (data.size() > kPushConstantBackingBytes)
	{
		throw std::runtime_error("Metal push constant payload exceeds backing buffer size");
	}
	if (pushConstantBackingBuffer_ == nullptr)
	{
		throw std::runtime_error("Metal push constant backing buffer is not initialized");
	}

	std::memcpy(pushConstantBackingBuffer_->contents(), data.data(), data.size());
	TrackBufferBinding(stageFlags, kPushConstantSlot, pushConstantBackingBuffer_, 0, data.size());

	if (renderEncoder_ != nullptr)
	{
		if (HasShaderStage(stageFlags, ShaderStageFlags::VERTEX))
		{
			renderEncoder_->setVertexBytes(data.data(), data.size(), kPushConstantSlot);
		}
		if (HasShaderStage(stageFlags, ShaderStageFlags::FRAGMENT))
		{
			renderEncoder_->setFragmentBytes(data.data(), data.size(), kPushConstantSlot);
		}
	}

	if (computeEncoder_ != nullptr && HasShaderStage(stageFlags, ShaderStageFlags::COMPUTE))
	{
		computeEncoder_->setBytes(data.data(), data.size(), kPushConstantSlot);
	}
}

void MetalCommandList::SetViewport(float x, float y, float width, float height)
{
	RequireRecording("SetViewport");
	if (renderEncoder_ == nullptr)
	{
		throw std::runtime_error("SetViewport requires an active render pass");
	}

	MTL::Viewport viewport{};
	viewport.originX = x;
	// SPECIAL (Vulkan parity): keep the RHI viewport convention identical across
	// backends by applying a Y-flipped Metal viewport transform.
	viewport.originY = y + height;
	viewport.width   = width;
	viewport.height  = -height;
	viewport.znear   = 0.0;
	viewport.zfar    = 1.0;
	renderEncoder_->setViewport(viewport);
}

void MetalCommandList::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	RequireRecording("SetScissor");
	if (renderEncoder_ == nullptr)
	{
		throw std::runtime_error("SetScissor requires an active render pass");
	}

	MTL::ScissorRect scissor{};
	scissor.x      = static_cast<NS::UInteger>(std::max(x, 0));
	scissor.y      = static_cast<NS::UInteger>(std::max(y, 0));
	scissor.width  = static_cast<NS::UInteger>(width);
	scissor.height = static_cast<NS::UInteger>(height);
	renderEncoder_->setScissorRect(scissor);
}

void MetalCommandList::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
	RequireRecording("Draw");
	if (renderEncoder_ == nullptr || currentPipeline_ == nullptr ||
	    currentPipeline_->GetPipelineType() != PipelineType::GRAPHICS)
	{
		throw std::runtime_error("Draw requires an active graphics pipeline and render pass");
	}

	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::VERTEX);
	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::FRAGMENT);

	IRRuntimeDrawParams drawParams{};
	drawParams.draw.vertexCountPerInstance = vertexCount;
	drawParams.draw.instanceCount          = 1;
	drawParams.draw.startVertexLocation    = firstVertex;
	drawParams.draw.startInstanceLocation  = 0;
	renderEncoder_->setVertexBytes(&drawParams, sizeof(IRRuntimeDrawParams), kIRArgumentBufferDrawArgumentsBindPoint);
	renderEncoder_->setVertexBytes(&kIRNonIndexedDraw, sizeof(kIRNonIndexedDraw), kIRArgumentBufferUniformsBindPoint);

	renderEncoder_->drawPrimitives(currentPipeline_->GetPrimitiveType(), firstVertex, vertexCount);
}

void MetalCommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
{
	RequireRecording("DrawIndexed");
	if (renderEncoder_ == nullptr || currentPipeline_ == nullptr ||
	    currentPipeline_->GetPipelineType() != PipelineType::GRAPHICS)
	{
		throw std::runtime_error("DrawIndexed requires an active graphics pipeline and render pass");
	}
	if (indexBuffer_ == nullptr)
	{
		throw std::runtime_error("DrawIndexed requires an index buffer");
	}

	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::VERTEX);
	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::FRAGMENT);
	const size_t byteOffset = indexBufferOffset_ + firstIndex * IndexTypeSize(indexType_);

	IRRuntimeDrawParams drawParams{};
	drawParams.drawIndexed.indexCountPerInstance = indexCount;
	drawParams.drawIndexed.instanceCount         = 1;
	drawParams.drawIndexed.startIndexLocation    = static_cast<uint32_t>(byteOffset);
	drawParams.drawIndexed.baseVertexLocation    = vertexOffset;
	drawParams.drawIndexed.startInstanceLocation = 0;
	const uint16_t irIndexType                   = IRIndexType(indexType_);
	renderEncoder_->setVertexBytes(&drawParams, sizeof(IRRuntimeDrawParams), kIRArgumentBufferDrawArgumentsBindPoint);
	renderEncoder_->setVertexBytes(&irIndexType, sizeof(irIndexType), kIRArgumentBufferUniformsBindPoint);

	renderEncoder_->drawIndexedPrimitives(
	    currentPipeline_->GetPrimitiveType(),
	    indexCount,
	    IndexTypeToMetal(indexType_),
	    indexBuffer_->GetHandle(),
	    byteOffset,
	    1,
	    vertexOffset,
	    0);
}

void MetalCommandList::DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount,
                                           uint32_t stride)
{
	(void) stride;
	RequireRecording("DrawIndexedIndirect");
	if (drawCount == 0)
	{
		return;
	}
	if (renderEncoder_ == nullptr || currentPipeline_ == nullptr ||
	    currentPipeline_->GetPipelineType() != PipelineType::GRAPHICS)
	{
		throw std::runtime_error("DrawIndexedIndirect requires an active graphics pipeline and render pass");
	}
	if (indexBuffer_ == nullptr)
	{
		throw std::runtime_error("DrawIndexedIndirect requires an index buffer");
	}

	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::VERTEX);
	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::FRAGMENT);
	auto *indirectBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (indirectBuffer == nullptr)
	{
		throw std::runtime_error("DrawIndexedIndirect requires a Metal buffer");
	}

	const uint16_t irIndexType = IRIndexType(indexType_);
	renderEncoder_->setVertexBuffer(indirectBuffer->GetHandle(), offset, kIRArgumentBufferDrawArgumentsBindPoint);
	renderEncoder_->setVertexBytes(&irIndexType, sizeof(irIndexType), kIRArgumentBufferUniformsBindPoint);

	renderEncoder_->drawIndexedPrimitives(
	    currentPipeline_->GetPrimitiveType(),
	    IndexTypeToMetal(indexType_),
	    indexBuffer_->GetHandle(),
	    indexBufferOffset_,
	    indirectBuffer->GetHandle(),
	    offset);
}

void MetalCommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                            uint32_t firstIndex, int32_t vertexOffset,
                                            uint32_t firstInstance)
{
	RequireRecording("DrawIndexedInstanced");
	if (renderEncoder_ == nullptr || currentPipeline_ == nullptr ||
	    currentPipeline_->GetPipelineType() != PipelineType::GRAPHICS)
	{
		throw std::runtime_error("DrawIndexedInstanced requires an active graphics pipeline and render pass");
	}
	if (indexBuffer_ == nullptr)
	{
		throw std::runtime_error("DrawIndexedInstanced requires an index buffer");
	}

	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::VERTEX);
	ApplyTopLevelArgumentBufferForRenderStage(ShaderStageFlags::FRAGMENT);
	const size_t byteOffset = indexBufferOffset_ + firstIndex * IndexTypeSize(indexType_);

	IRRuntimeDrawParams drawParams{};
	drawParams.drawIndexed.indexCountPerInstance = indexCount;
	drawParams.drawIndexed.instanceCount         = instanceCount;
	drawParams.drawIndexed.startIndexLocation    = static_cast<uint32_t>(byteOffset);
	drawParams.drawIndexed.baseVertexLocation    = vertexOffset;
	drawParams.drawIndexed.startInstanceLocation = firstInstance;
	const uint16_t irIndexType                   = IRIndexType(indexType_);
	renderEncoder_->setVertexBytes(&drawParams, sizeof(IRRuntimeDrawParams), kIRArgumentBufferDrawArgumentsBindPoint);
	renderEncoder_->setVertexBytes(&irIndexType, sizeof(irIndexType), kIRArgumentBufferUniformsBindPoint);

	renderEncoder_->drawIndexedPrimitives(
	    currentPipeline_->GetPrimitiveType(),
	    indexCount,
	    IndexTypeToMetal(indexType_),
	    indexBuffer_->GetHandle(),
	    byteOffset,
	    instanceCount,
	    vertexOffset,
	    firstInstance);
}

void MetalCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	RequireRecording("Dispatch");
	if (currentPipeline_ == nullptr || currentPipeline_->GetPipelineType() != PipelineType::COMPUTE)
	{
		throw std::runtime_error("Dispatch requires an active compute pipeline");
	}

	MTL::ComputeCommandEncoder *encoder = EnsureComputeEncoder();
	ApplyTopLevelArgumentBufferForCompute();
	encoder->dispatchThreadgroups(
	    MTL::Size{groupCountX, groupCountY, groupCountZ},
	    currentPipeline_->GetThreadsPerThreadgroup());
}

void MetalCommandList::DispatchIndirect(IRHIBuffer *buffer, size_t offset)
{
	RequireRecording("DispatchIndirect");
	if (currentPipeline_ == nullptr || currentPipeline_->GetPipelineType() != PipelineType::COMPUTE)
	{
		throw std::runtime_error("DispatchIndirect requires an active compute pipeline");
	}

	auto *indirectBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (indirectBuffer == nullptr)
	{
		throw std::runtime_error("DispatchIndirect requires a Metal buffer");
	}

	MTL::ComputeCommandEncoder *encoder = EnsureComputeEncoder();
	ApplyTopLevelArgumentBufferForCompute();
	encoder->dispatchThreadgroups(indirectBuffer->GetHandle(), offset, currentPipeline_->GetThreadsPerThreadgroup());
}

void MetalCommandList::CopyBuffer(IRHIBuffer *srcBuffer, IRHIBuffer *dstBuffer,
                                  std::span<const BufferCopy> regions)
{
	RequireRecording("CopyBuffer");
	auto *src = dynamic_cast<MetalBuffer *>(srcBuffer);
	auto *dst = dynamic_cast<MetalBuffer *>(dstBuffer);
	if (src == nullptr || dst == nullptr)
	{
		throw std::runtime_error("CopyBuffer requires Metal buffers");
	}

	MTL::BlitCommandEncoder *encoder = EnsureBlitEncoder();
	for (const BufferCopy &region : regions)
	{
		encoder->copyFromBuffer(src->GetHandle(), region.srcOffset,
		                        dst->GetHandle(), region.dstOffset, region.size);
	}
}

void MetalCommandList::FillBuffer(IRHIBuffer *buffer, size_t offset, size_t size, uint32_t value)
{
	RequireRecording("FillBuffer");
	auto *metalBuffer = dynamic_cast<MetalBuffer *>(buffer);
	if (metalBuffer == nullptr)
	{
		throw std::runtime_error("FillBuffer requires a Metal buffer");
	}

	MTL::BlitCommandEncoder *encoder = EnsureBlitEncoder();
	encoder->fillBuffer(metalBuffer->GetHandle(), NS::Range(offset, size), static_cast<uint8_t>(value & 0xFFu));
}

void MetalCommandList::CopyTexture(IRHITexture *srcTexture, IRHITexture *dstTexture,
                                   std::span<const TextureCopy> regions)
{
	RequireRecording("CopyTexture");
	auto *src = dynamic_cast<MetalTexture *>(srcTexture);
	auto *dst = dynamic_cast<MetalTexture *>(dstTexture);
	if (src == nullptr || dst == nullptr)
	{
		throw std::runtime_error("CopyTexture requires Metal textures");
	}

	MTL::BlitCommandEncoder *encoder = EnsureBlitEncoder();
	for (const TextureCopy &region : regions)
	{
		encoder->copyFromTexture(
		    src->GetHandle(),
		    region.srcArrayLayer,
		    region.srcMipLevel,
		    MTL::Origin{region.srcX, region.srcY, region.srcZ},
		    MTL::Size{region.width, region.height, region.depth},
		    dst->GetHandle(),
		    region.dstArrayLayer,
		    region.dstMipLevel,
		    MTL::Origin{region.dstX, region.dstY, region.dstZ});
	}
}

void MetalCommandList::BlitTexture(IRHITexture *srcTexture, IRHITexture *dstTexture,
                                   std::span<const TextureBlit> regions, FilterMode filter)
{
	(void) filter;
	RequireRecording("BlitTexture");
	auto *src = dynamic_cast<MetalTexture *>(srcTexture);
	auto *dst = dynamic_cast<MetalTexture *>(dstTexture);
	if (src == nullptr || dst == nullptr)
	{
		throw std::runtime_error("BlitTexture requires Metal textures");
	}

	MTL::BlitCommandEncoder *encoder = EnsureBlitEncoder();
	for (const TextureBlit &region : regions)
	{
		const uint32_t srcWidth  = region.srcX1 > region.srcX0 ? region.srcX1 - region.srcX0 : 0;
		const uint32_t srcHeight = region.srcY1 > region.srcY0 ? region.srcY1 - region.srcY0 : 0;
		const uint32_t srcDepth  = region.srcZ1 > region.srcZ0 ? region.srcZ1 - region.srcZ0 : 0;
		const uint32_t dstWidth  = region.dstX1 > region.dstX0 ? region.dstX1 - region.dstX0 : 0;
		const uint32_t dstHeight = region.dstY1 > region.dstY0 ? region.dstY1 - region.dstY0 : 0;
		const uint32_t dstDepth  = region.dstZ1 > region.dstZ0 ? region.dstZ1 - region.dstZ0 : 0;

		if (srcWidth != dstWidth || srcHeight != dstHeight || srcDepth != dstDepth)
		{
			throw std::runtime_error("Scaled BlitTexture is not implemented in Metal phase 1 backend");
		}

		encoder->copyFromTexture(
		    src->GetHandle(),
		    region.srcArrayLayer,
		    region.srcMipLevel,
		    MTL::Origin{region.srcX0, region.srcY0, region.srcZ0},
		    MTL::Size{srcWidth, srcHeight, srcDepth},
		    dst->GetHandle(),
		    region.dstArrayLayer,
		    region.dstMipLevel,
		    MTL::Origin{region.dstX0, region.dstY0, region.dstZ0});
	}
}

void MetalCommandList::Barrier(PipelineScope src_scope, PipelineScope dst_scope,
                               std::span<const BufferTransition>  buffer_transitions,
                               std::span<const TextureTransition> texture_transitions,
                               std::span<const MemoryBarrier>     memory_barriers)
{
	(void) src_scope;
	(void) dst_scope;
	(void) buffer_transitions;
	(void) texture_transitions;
	(void) memory_barriers;
	// Metal does not expose Vulkan-style explicit layout transitions; keep as no-op metadata path.
}

void MetalCommandList::ResetQueryPool(IRHIQueryPool *queryPool, uint32_t firstQuery,
                                      uint32_t queryCount)
{
	(void) queryPool;
	(void) firstQuery;
	(void) queryCount;
}

void MetalCommandList::WriteTimestamp(IRHIQueryPool *queryPool, uint32_t query, StageMask stage)
{
	(void) queryPool;
	(void) query;
	(void) stage;
}

void MetalCommandList::BeginQuery(IRHIQueryPool *queryPool, uint32_t query)
{
	(void) queryPool;
	(void) query;
}

void MetalCommandList::EndQuery(IRHIQueryPool *queryPool, uint32_t query)
{
	(void) queryPool;
	(void) query;
}

void MetalCommandList::CopyQueryPoolResults(IRHIQueryPool *queryPool, uint32_t firstQuery,
                                            uint32_t queryCount, IRHIBuffer *dstBuffer,
                                            size_t dstOffset, size_t stride,
                                            QueryResultFlags flags)
{
	(void) queryPool;
	(void) firstQuery;
	(void) queryCount;
	(void) dstBuffer;
	(void) dstOffset;
	(void) stride;
	(void) flags;
}

void MetalCommandList::ReleaseToQueue(QueueType                          dstQueue,
                                      std::span<const BufferTransition>  buffer_transitions,
                                      std::span<const TextureTransition> texture_transitions)
{
	(void) dstQueue;
	(void) buffer_transitions;
	(void) texture_transitions;
}

void MetalCommandList::AcquireFromQueue(QueueType                          srcQueue,
                                        std::span<const BufferTransition>  buffer_transitions,
                                        std::span<const TextureTransition> texture_transitions)
{
	(void) srcQueue;
	(void) buffer_transitions;
	(void) texture_transitions;
}

QueueType MetalCommandList::GetQueueType() const
{
	return queueType_;
}

MTL::CommandBuffer *MetalCommandList::GetHandle() const
{
	return commandBuffer_;
}

}        // namespace rhi::metal3
