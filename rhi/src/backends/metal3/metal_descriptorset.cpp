#include "metal_backend.h"

#include <stdexcept>

namespace rhi::metal3
{
namespace
{

bool IsBufferDescriptorType(DescriptorType type)
{
	switch (type)
	{
		case DescriptorType::UNIFORM_BUFFER:
		case DescriptorType::STORAGE_BUFFER:
		case DescriptorType::UNIFORM_BUFFER_DYNAMIC:
		case DescriptorType::STORAGE_BUFFER_DYNAMIC:
			return true;
		default:
			return false;
	}
}

bool IsTextureDescriptorType(DescriptorType type)
{
	switch (type)
	{
		case DescriptorType::SAMPLED_TEXTURE:
		case DescriptorType::STORAGE_TEXTURE:
		case DescriptorType::UNIFORM_TEXEL_BUFFER:
		case DescriptorType::STORAGE_TEXEL_BUFFER:
		case DescriptorType::SAMPLER:
			return true;
		default:
			return false;
	}
}

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

template <typename T>
const T *FindBinding(uint32_t binding, const std::vector<std::pair<uint32_t, T>> &bindings)
{
	for (const auto &entry : bindings)
	{
		if (entry.first == binding)
		{
			return &entry.second;
		}
	}
	return nullptr;
}

}        // namespace

MetalDescriptorSetLayout::MetalDescriptorSetLayout(const DescriptorSetLayoutDesc &desc) :
    desc_(desc)
{
	for (size_t i = 0; i < desc_.bindings.size(); ++i)
	{
		const DescriptorBinding &binding = desc_.bindings[i];
		if (binding.count == 0)
		{
			throw std::invalid_argument("Descriptor binding count must be greater than zero");
		}
		if (binding.count != 1)
		{
			throw std::invalid_argument("Metal descriptor arrays (count > 1) are not implemented yet");
		}

		const auto [iter, inserted] = bindingLookup_.emplace(binding.binding, i);
		if (!inserted)
		{
			throw std::invalid_argument("Descriptor set layout contains duplicate binding index");
		}
	}
}

const DescriptorSetLayoutDesc &MetalDescriptorSetLayout::GetDesc() const
{
	return desc_;
}

const DescriptorBinding *MetalDescriptorSetLayout::FindBinding(uint32_t binding) const
{
	const auto iter = bindingLookup_.find(binding);
	if (iter == bindingLookup_.end())
	{
		return nullptr;
	}

	return &desc_.bindings[iter->second];
}

MetalSampler::MetalSampler(const SamplerDesc &desc, MTL::SamplerState *samplerState) :
    desc_(desc), samplerState_(samplerState)
{}

MetalSampler::~MetalSampler()
{
	if (samplerState_ != nullptr)
	{
		samplerState_->release();
		samplerState_ = nullptr;
	}
}

const SamplerDesc &MetalSampler::GetDesc() const
{
	return desc_;
}

MTL::SamplerState *MetalSampler::GetHandle() const
{
	return samplerState_;
}

MetalDescriptorSet::MetalDescriptorSet(IRHIDescriptorSetLayout *layout) :
    layout_(layout)
{}

void MetalDescriptorSet::BindBuffer(uint32_t binding, const BufferBinding &bufferBinding)
{
	const MetalDescriptorSetLayout *layout = GetLayout();
	if (layout == nullptr)
	{
		throw std::runtime_error("Descriptor set is missing a Metal layout");
	}

	const DescriptorBinding *bindingDesc = layout->FindBinding(binding);
	if (bindingDesc == nullptr)
	{
		throw std::runtime_error("BindBuffer references a binding not present in the descriptor set layout");
	}
	if (!IsBufferDescriptorType(bindingDesc->type))
	{
		throw std::runtime_error("BindBuffer used on a non-buffer descriptor binding");
	}

	UpsertBinding(binding, bufferBinding, bufferBindings_);
}

void MetalDescriptorSet::BindTexture(uint32_t binding, const TextureBinding &textureBinding)
{
	const MetalDescriptorSetLayout *layout = GetLayout();
	if (layout == nullptr)
	{
		throw std::runtime_error("Descriptor set is missing a Metal layout");
	}

	const DescriptorBinding *bindingDesc = layout->FindBinding(binding);
	if (bindingDesc == nullptr)
	{
		throw std::runtime_error("BindTexture references a binding not present in the descriptor set layout");
	}
	if (!IsTextureDescriptorType(bindingDesc->type))
	{
		throw std::runtime_error("BindTexture used on a non-texture descriptor binding");
	}

	UpsertBinding(binding, textureBinding, textureBindings_);
}

const MetalDescriptorSetLayout *MetalDescriptorSet::GetLayout() const
{
	return dynamic_cast<const MetalDescriptorSetLayout *>(layout_.Get());
}

const std::vector<std::pair<uint32_t, BufferBinding>> &MetalDescriptorSet::GetBufferBindings() const
{
	return bufferBindings_;
}

const std::vector<std::pair<uint32_t, TextureBinding>> &MetalDescriptorSet::GetTextureBindings() const
{
	return textureBindings_;
}

const BufferBinding *MetalDescriptorSet::FindBufferBinding(uint32_t binding) const
{
	return FindBinding(binding, bufferBindings_);
}

const TextureBinding *MetalDescriptorSet::FindTextureBinding(uint32_t binding) const
{
	return FindBinding(binding, textureBindings_);
}

}        // namespace rhi::metal3
