#include "metal_backend.h"

namespace rhi::metal3
{

MetalShader::MetalShader(const ShaderDesc &desc, MTL::Library *library, MTL::Function *function) :
    stage_(desc.stage),
    entryPoint_(desc.entryPoint != nullptr ? desc.entryPoint : "main"),
    library_(library),
    function_(function)
{}

MetalShader::~MetalShader()
{
	if (function_ != nullptr)
	{
		function_->release();
		function_ = nullptr;
	}
	if (library_ != nullptr)
	{
		library_->release();
		library_ = nullptr;
	}
}

ShaderStage MetalShader::GetStage() const
{
	return stage_;
}

MTL::Function *MetalShader::GetFunction() const
{
	return function_;
}

const char *MetalShader::GetEntryPoint() const
{
	return entryPoint_.c_str();
}

const std::string &MetalShader::GetEntryPointString() const
{
	return entryPoint_;
}

}        // namespace rhi::metal3
