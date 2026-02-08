#include <algorithm>
#include <stdexcept>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalTextureView::MetalTextureView(const TextureViewDesc &desc) :
    texture_(dynamic_cast<MetalTexture *>(desc.texture)),
    format_(desc.format == TextureFormat::UNDEFINED ? (desc.texture != nullptr ? desc.texture->GetFormat() : TextureFormat::UNDEFINED) : desc.format),
    baseMipLevel_(desc.baseMipLevel),
    mipLevelCount_(std::max(desc.mipLevelCount, 1u)),
    baseArrayLayer_(desc.baseArrayLayer),
    arrayLayerCount_(std::max(desc.arrayLayerCount, 1u))
{
	if (texture_ == nullptr)
	{
		throw std::invalid_argument("MetalTextureView requires a valid MetalTexture");
	}

	texture_->AddRef();
}

MetalTextureView::~MetalTextureView()
{
	if (texture_ != nullptr)
	{
		texture_->Release();
		texture_ = nullptr;
	}
}

IRHITexture *MetalTextureView::GetTexture()
{
	return texture_;
}

TextureFormat MetalTextureView::GetFormat() const
{
	return format_;
}

uint32_t MetalTextureView::GetWidth() const
{
	if (texture_ == nullptr)
	{
		return 0;
	}
	const uint32_t mipShift = std::min(baseMipLevel_, 31u);
	return std::max(texture_->GetWidth() >> mipShift, 1u);
}

uint32_t MetalTextureView::GetHeight() const
{
	if (texture_ == nullptr)
	{
		return 0;
	}
	const uint32_t mipShift = std::min(baseMipLevel_, 31u);
	return std::max(texture_->GetHeight() >> mipShift, 1u);
}

uint32_t MetalTextureView::GetBaseMipLevel() const
{
	return baseMipLevel_;
}

uint32_t MetalTextureView::GetMipLevelCount() const
{
	return mipLevelCount_;
}

uint32_t MetalTextureView::GetBaseArrayLayer() const
{
	return baseArrayLayer_;
}

uint32_t MetalTextureView::GetArrayLayerCount() const
{
	return arrayLayerCount_;
}

}        // namespace rhi::metal3
