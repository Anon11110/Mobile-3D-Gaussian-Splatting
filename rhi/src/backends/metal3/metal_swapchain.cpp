#include <algorithm>

#include "metal_backend.h"

namespace rhi::metal3
{
namespace
{

void RecreateDummyBackBuffers(uint32_t width, uint32_t height, uint32_t imageCount,
                              TextureFormat format, std::vector<TextureHandle> &backBuffers,
                              std::vector<TextureViewHandle> &backBufferViews)
{
	backBuffers.clear();
	backBufferViews.clear();

	TextureDesc textureDesc    = {};
	textureDesc.width          = width;
	textureDesc.height         = height;
	textureDesc.format         = format;
	textureDesc.isRenderTarget = true;

	backBuffers.reserve(imageCount);
	backBufferViews.reserve(imageCount);

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		TextureHandle texture = RefCntPtr<IRHITexture>::Create(new MetalTexture(textureDesc));

		TextureViewDesc viewDesc = {};
		viewDesc.texture         = texture.Get();
		viewDesc.format          = format;

		TextureViewHandle view = RefCntPtr<IRHITextureView>::Create(new MetalTextureView(viewDesc));
		backBuffers.push_back(texture);
		backBufferViews.push_back(view);
	}
}

}        // namespace

MetalSwapchain::MetalSwapchain(const SwapchainDesc &desc) :
    width_(std::max(desc.width, 1u)), height_(std::max(desc.height, 1u)), imageCount_(std::max(desc.bufferCount, 1u))
{
	RecreateDummyBackBuffers(width_, height_, imageCount_, desc.format, backBuffers_, backBufferViews_);
}

SwapchainStatus MetalSwapchain::AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore)
{
	(void) signalSemaphore;

	if (imageCount_ == 0)
	{
		imageIndex = 0;
		return SwapchainStatus::ERROR_OCCURRED;
	}

	imageIndex = frameIndex_ % imageCount_;
	frameIndex_++;
	return SwapchainStatus::SUCCESS;
}

SwapchainStatus MetalSwapchain::Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore)
{
	(void) waitSemaphore;
	if (imageIndex >= imageCount_)
	{
		return SwapchainStatus::ERROR_OCCURRED;
	}
	return SwapchainStatus::SUCCESS;
}

IRHITexture *MetalSwapchain::GetBackBuffer(uint32_t index)
{
	if (index >= backBuffers_.size())
	{
		return nullptr;
	}
	return backBuffers_[index].Get();
}

IRHITextureView *MetalSwapchain::GetBackBufferView(uint32_t index)
{
	if (index >= backBufferViews_.size())
	{
		return nullptr;
	}
	return backBufferViews_[index].Get();
}

uint32_t MetalSwapchain::GetImageCount() const
{
	return imageCount_;
}

void MetalSwapchain::Resize(uint32_t width, uint32_t height)
{
	width_  = std::max(width, 1u);
	height_ = std::max(height, 1u);

	TextureFormat format = TextureFormat::B8G8R8A8_UNORM;
	if (!backBuffers_.empty())
	{
		format = backBuffers_[0]->GetFormat();
	}

	RecreateDummyBackBuffers(width_, height_, imageCount_, format, backBuffers_, backBufferViews_);
}

SurfaceTransform MetalSwapchain::GetPreTransform() const
{
	return SurfaceTransform::IDENTITY;
}

}        // namespace rhi::metal3
