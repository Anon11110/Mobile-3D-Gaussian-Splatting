#include "metal_backend.h"

namespace rhi::metal3
{

MetalPipeline::MetalPipeline(const GraphicsPipelineDesc &desc)
{
	(void) desc;
	pipelineType_ = PipelineType::GRAPHICS;
}

MetalPipeline::MetalPipeline(const ComputePipelineDesc &desc)
{
	(void) desc;
	pipelineType_ = PipelineType::COMPUTE;
}

PipelineType MetalPipeline::GetPipelineType() const
{
	return pipelineType_;
}

}        // namespace rhi::metal3
