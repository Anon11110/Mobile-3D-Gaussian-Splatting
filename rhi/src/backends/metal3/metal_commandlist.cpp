#include "metal_backend.h"

namespace rhi::metal3
{

MetalCommandList::MetalCommandList(QueueType queueType) :
    queueType_(queueType)
{}

void MetalCommandList::Begin()
{
	isRecording_ = true;
}

void MetalCommandList::End()
{
	isRecording_ = false;
}

void MetalCommandList::Reset()
{
	isRecording_ = false;
}

void MetalCommandList::BeginRendering(const RenderingInfo &info)
{
	(void) info;
}

void MetalCommandList::EndRendering()
{}

void MetalCommandList::SetPipeline(IRHIPipeline *pipeline)
{
	(void) pipeline;
}

void MetalCommandList::SetVertexBuffer(uint32_t binding, IRHIBuffer *buffer, size_t offset)
{
	(void) binding;
	(void) buffer;
	(void) offset;
}

void MetalCommandList::BindIndexBuffer(IRHIBuffer *buffer, size_t offset)
{
	(void) buffer;
	(void) offset;
}

void MetalCommandList::BindDescriptorSet(uint32_t setIndex, IRHIDescriptorSet *descriptorSet,
                                         std::span<const uint32_t> dynamicOffsets)
{
	(void) setIndex;
	(void) descriptorSet;
	(void) dynamicOffsets;
}

void MetalCommandList::PushConstants(ShaderStageFlags stageFlags, uint32_t offset,
                                     std::span<const std::byte> data)
{
	(void) stageFlags;
	(void) offset;
	(void) data;
}

void MetalCommandList::SetViewport(float x, float y, float width, float height)
{
	(void) x;
	(void) y;
	(void) width;
	(void) height;
}

void MetalCommandList::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
	(void) x;
	(void) y;
	(void) width;
	(void) height;
}

void MetalCommandList::Draw(uint32_t vertexCount, uint32_t firstVertex)
{
	(void) vertexCount;
	(void) firstVertex;
}

void MetalCommandList::DrawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset)
{
	(void) indexCount;
	(void) firstIndex;
	(void) vertexOffset;
}

void MetalCommandList::DrawIndexedIndirect(IRHIBuffer *buffer, size_t offset, uint32_t drawCount,
                                           uint32_t stride)
{
	(void) buffer;
	(void) offset;
	(void) drawCount;
	(void) stride;
}

void MetalCommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                            uint32_t firstIndex, int32_t vertexOffset,
                                            uint32_t firstInstance)
{
	(void) indexCount;
	(void) instanceCount;
	(void) firstIndex;
	(void) vertexOffset;
	(void) firstInstance;
}

void MetalCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
	(void) groupCountX;
	(void) groupCountY;
	(void) groupCountZ;
}

void MetalCommandList::DispatchIndirect(IRHIBuffer *buffer, size_t offset)
{
	(void) buffer;
	(void) offset;
}

void MetalCommandList::CopyBuffer(IRHIBuffer *srcBuffer, IRHIBuffer *dstBuffer,
                                  std::span<const BufferCopy> regions)
{
	(void) srcBuffer;
	(void) dstBuffer;
	(void) regions;
}

void MetalCommandList::FillBuffer(IRHIBuffer *buffer, size_t offset, size_t size, uint32_t value)
{
	(void) buffer;
	(void) offset;
	(void) size;
	(void) value;
}

void MetalCommandList::CopyTexture(IRHITexture *srcTexture, IRHITexture *dstTexture,
                                   std::span<const TextureCopy> regions)
{
	(void) srcTexture;
	(void) dstTexture;
	(void) regions;
}

void MetalCommandList::BlitTexture(IRHITexture *srcTexture, IRHITexture *dstTexture,
                                   std::span<const TextureBlit> regions, FilterMode filter)
{
	(void) srcTexture;
	(void) dstTexture;
	(void) regions;
	(void) filter;
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

}        // namespace rhi::metal3
