#include <stdexcept>

#include "metal_backend.h"
#include "metal_conversions.h"

namespace rhi::metal3
{

MetalPipeline::MetalPipeline(const GraphicsPipelineDesc &desc, MTL::RenderPipelineState *renderPipelineState,
                             MTL::DepthStencilState *depthStencilState) :
    pipelineType_(PipelineType::GRAPHICS),
    renderPipelineState_(renderPipelineState),
    depthStencilState_(depthStencilState)
{
	if (renderPipelineState_ == nullptr)
	{
		throw std::invalid_argument("MetalPipeline requires a valid graphics pipeline state");
	}

	primitiveType_      = PrimitiveTopologyToMetal(desc.topology);
	cullMode_           = CullModeToMetal(desc.rasterizationState.cullMode);
	frontFacingWinding_ = FrontFaceToMetal(desc.rasterizationState.frontFace);
	triangleFillMode_   = PolygonModeToMetal(desc.rasterizationState.polygonMode);

	depthBiasEnable_         = desc.rasterizationState.depthBiasEnable;
	depthBiasConstantFactor_ = desc.rasterizationState.depthBiasConstantFactor;
	depthBiasSlopeFactor_    = desc.rasterizationState.depthBiasSlopeFactor;
	depthBiasClamp_          = desc.rasterizationState.depthBiasClamp;

	descriptorSetLayouts_ = desc.descriptorSetLayouts;
	pushConstantRanges_   = desc.pushConstantRanges;
}

MetalPipeline::MetalPipeline(const ComputePipelineDesc &desc, MTL::ComputePipelineState *computePipelineState,
                             MTL::Size threadsPerThreadgroup) :
    pipelineType_(PipelineType::COMPUTE),
    computePipelineState_(computePipelineState),
    descriptorSetLayouts_(desc.descriptorSetLayouts),
    pushConstantRanges_(desc.pushConstantRanges),
    threadsPerThreadgroup_(threadsPerThreadgroup)
{
	if (computePipelineState_ == nullptr)
	{
		throw std::invalid_argument("MetalPipeline requires a valid compute pipeline state");
	}
}

MetalPipeline::~MetalPipeline()
{
	if (renderPipelineState_ != nullptr)
	{
		renderPipelineState_->release();
		renderPipelineState_ = nullptr;
	}
	if (depthStencilState_ != nullptr)
	{
		depthStencilState_->release();
		depthStencilState_ = nullptr;
	}
	if (computePipelineState_ != nullptr)
	{
		computePipelineState_->release();
		computePipelineState_ = nullptr;
	}
}

PipelineType MetalPipeline::GetPipelineType() const
{
	return pipelineType_;
}

MTL::RenderPipelineState *MetalPipeline::GetRenderPipelineState() const
{
	return renderPipelineState_;
}

MTL::DepthStencilState *MetalPipeline::GetDepthStencilState() const
{
	return depthStencilState_;
}

MTL::ComputePipelineState *MetalPipeline::GetComputePipelineState() const
{
	return computePipelineState_;
}

MTL::PrimitiveType MetalPipeline::GetPrimitiveType() const
{
	return primitiveType_;
}

MTL::CullMode MetalPipeline::GetCullMode() const
{
	return cullMode_;
}

MTL::Winding MetalPipeline::GetFrontFacingWinding() const
{
	return frontFacingWinding_;
}

MTL::TriangleFillMode MetalPipeline::GetTriangleFillMode() const
{
	return triangleFillMode_;
}

bool MetalPipeline::IsDepthBiasEnabled() const
{
	return depthBiasEnable_;
}

float MetalPipeline::GetDepthBiasConstantFactor() const
{
	return depthBiasConstantFactor_;
}

float MetalPipeline::GetDepthBiasSlopeFactor() const
{
	return depthBiasSlopeFactor_;
}

float MetalPipeline::GetDepthBiasClamp() const
{
	return depthBiasClamp_;
}

const std::vector<PushConstantRange> &MetalPipeline::GetPushConstantRanges() const
{
	return pushConstantRanges_;
}

const std::vector<IRHIDescriptorSetLayout *> &MetalPipeline::GetDescriptorSetLayouts() const
{
	return descriptorSetLayouts_;
}

MTL::Size MetalPipeline::GetThreadsPerThreadgroup() const
{
	return threadsPerThreadgroup_;
}

}        // namespace rhi::metal3
