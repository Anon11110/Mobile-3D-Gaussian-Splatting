#include "metal_backend.h"

namespace rhi::metal3
{

MetalShader::MetalShader(const ShaderDesc &desc) :
    stage_(desc.stage)
{}

ShaderStage MetalShader::GetStage() const
{
	return stage_;
}

}        // namespace rhi::metal3
