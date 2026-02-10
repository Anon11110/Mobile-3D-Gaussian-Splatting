#include <algorithm>
#include <stdexcept>

#include "metal_backend.h"
#include "metal_conversions.h"

namespace rhi::metal3
{

MetalTextureView::MetalTextureView(const TextureViewDesc &desc) :
    texture_(dynamic_cast<MetalTexture *>(desc.texture))
{
	if (texture_ == nullptr)
	{
		throw std::invalid_argument("MetalTextureView requires a valid MetalTexture");
	}

	texture_->AddRef();

	format_          = desc.format == TextureFormat::UNDEFINED ? texture_->GetFormat() : desc.format;
	baseMipLevel_    = desc.baseMipLevel;
	mipLevelCount_   = std::max(desc.mipLevelCount, 1u);
	baseArrayLayer_  = desc.baseArrayLayer;
	arrayLayerCount_ = std::max(desc.arrayLayerCount, 1u);

	if (baseMipLevel_ >= texture_->GetMipLevels())
	{
		throw std::out_of_range("MetalTextureView base mip level is out of range");
	}
	if (baseArrayLayer_ >= texture_->GetArrayLayers())
	{
		throw std::out_of_range("MetalTextureView base array layer is out of range");
	}

	mipLevelCount_   = std::min(mipLevelCount_, texture_->GetMipLevels() - baseMipLevel_);
	arrayLayerCount_ = std::min(arrayLayerCount_, texture_->GetArrayLayers() - baseArrayLayer_);

	const bool fullView = (format_ == texture_->GetFormat()) && (baseMipLevel_ == 0) &&
	                      (mipLevelCount_ == texture_->GetMipLevels()) && (baseArrayLayer_ == 0) &&
	                      (arrayLayerCount_ == texture_->GetArrayLayers());

	if (fullView)
	{
		viewTexture_     = texture_->GetHandle();
		ownsViewTexture_ = false;
		return;
	}

	const MTL::PixelFormat viewFormat = TextureFormatToMetal(format_);
	if (viewFormat == MTL::PixelFormatInvalid)
	{
		throw std::runtime_error("MetalTextureView requires a valid Metal pixel format");
	}

	const MTL::TextureType viewType = TextureTypeToMetal(texture_->GetType(), texture_->GetType() == TextureType::TEXTURE_CUBE);
	viewTexture_                    = texture_->GetHandle()->newTextureView(
	                       viewFormat,
	                       viewType,
	                       NS::Range(baseMipLevel_, mipLevelCount_),
	                       NS::Range(baseArrayLayer_, arrayLayerCount_));

	if (viewTexture_ == nullptr)
	{
		throw std::runtime_error("Failed to create Metal texture view");
	}

	ownsViewTexture_ = true;
}

MetalTextureView::~MetalTextureView()
{
	if (ownsViewTexture_ && viewTexture_ != nullptr)
	{
		viewTexture_->release();
	}
	viewTexture_ = nullptr;

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

MTL::Texture *MetalTextureView::GetHandle() const
{
	return viewTexture_;
}

}        // namespace rhi::metal3
