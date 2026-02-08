#include <algorithm>

#include "metal_backend.h"

namespace rhi::metal3
{

MetalTexture::MetalTexture(const TextureDesc &desc) :
    width_(std::max(desc.width, 1u)), height_(std::max(desc.height, 1u)), depth_(std::max(desc.depth, 1u)), mipLevels_(std::max(desc.mipLevels, 1u)), arrayLayers_(std::max(desc.arrayLayers, 1u)), format_(desc.format)
{}

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

}        // namespace rhi::metal3
