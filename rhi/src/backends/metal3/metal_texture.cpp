#include <algorithm>
#include <stdexcept>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalTexture::MetalTexture(MTL::Texture *texture, const TextureDesc &desc, bool ownsTexture) :
    texture_(texture),
    ownsTexture_(ownsTexture),
    width_(std::max(desc.width, 1u)),
    height_(std::max(desc.height, 1u)),
    depth_(std::max(desc.depth, 1u)),
    mipLevels_(std::max(desc.mipLevels, 1u)),
    arrayLayers_(std::max(desc.arrayLayers, 1u)),
    type_(desc.type),
    format_(desc.format)
{
	if (texture_ == nullptr)
	{
		throw std::invalid_argument("MetalTexture requires a valid MTL::Texture");
	}
}

MetalTexture::MetalTexture(MTL::Texture *texture, TextureFormat format, uint32_t width, uint32_t height,
                           uint32_t depth, uint32_t mipLevels, uint32_t arrayLayers, TextureType type,
                           bool ownsTexture) :
    texture_(texture),
    ownsTexture_(ownsTexture),
    width_(std::max(width, 1u)),
    height_(std::max(height, 1u)),
    depth_(std::max(depth, 1u)),
    mipLevels_(std::max(mipLevels, 1u)),
    arrayLayers_(std::max(arrayLayers, 1u)),
    type_(type),
    format_(format)
{
	if (texture_ == nullptr)
	{
		throw std::invalid_argument("MetalTexture requires a valid MTL::Texture");
	}
}

MetalTexture::~MetalTexture()
{
	if (ownsTexture_ && texture_ != nullptr)
	{
		texture_->release();
	}
	texture_ = nullptr;
}

uint32_t MetalTexture::GetWidth() const
{
	return width_;
}

uint32_t MetalTexture::GetHeight() const
{
	return height_;
}

uint32_t MetalTexture::GetDepth() const
{
	return depth_;
}

uint32_t MetalTexture::GetMipLevels() const
{
	return mipLevels_;
}

uint32_t MetalTexture::GetArrayLayers() const
{
	return arrayLayers_;
}

TextureFormat MetalTexture::GetFormat() const
{
	return format_;
}

MTL::Texture *MetalTexture::GetHandle() const
{
	return texture_;
}

TextureType MetalTexture::GetType() const
{
	return type_;
}

}        // namespace rhi::metal3
