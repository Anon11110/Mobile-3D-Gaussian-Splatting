#include "metal_backend.h"

namespace rhi::metal3
{
namespace
{

template <typename T>
void UpsertBinding(uint32_t binding, const T &value, std::vector<std::pair<uint32_t, T>> &bindings)
{
	for (auto &entry : bindings)
	{
		if (entry.first == binding)
		{
			entry.second = value;
			return;
		}
	}

	bindings.emplace_back(binding, value);
}

}        // namespace

MetalDescriptorSetLayout::MetalDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) :
    desc_(desc)
{}

const DescriptorSetLayoutDesc &MetalDescriptorSetLayout::GetDesc() const
{
	return desc_;
}

MetalSampler::MetalSampler(const SamplerDesc &desc) :
    desc_(desc)
{}

const SamplerDesc &MetalSampler::GetDesc() const
{
	return desc_;
}

MetalDescriptorSet::MetalDescriptorSet(IRHIDescriptorSetLayout *layout) :
    layout_(layout)
{}

void MetalDescriptorSet::BindBuffer(uint32_t binding, const BufferBinding &bufferBinding)
{
	UpsertBinding(binding, bufferBinding, bufferBindings_);
}

void MetalDescriptorSet::BindTexture(uint32_t binding, const TextureBinding &textureBinding)
{
	UpsertBinding(binding, textureBinding, textureBindings_);
}

}        // namespace rhi::metal3
