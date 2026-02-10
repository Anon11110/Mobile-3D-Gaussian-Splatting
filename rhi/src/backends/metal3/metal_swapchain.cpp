#include <algorithm>
#include <stdexcept>

#include "metal_backend.h"
#include "metal_conversions.h"

#if defined(__APPLE__)
#	define GLFW_EXPOSE_NATIVE_COCOA
#	include <GLFW/glfw3.h>
#	include <GLFW/glfw3native.h>
#	include <objc/message.h>
#	include <objc/objc.h>
#	include <objc/runtime.h>
#endif

namespace rhi::metal3
{
namespace
{

constexpr signed char kObjcYes = 1;

TextureFormat ResolveLayerCompatibleFormat(TextureFormat requested)
{
	switch (requested)
	{
		case TextureFormat::B8G8R8A8_UNORM:
		case TextureFormat::B8G8R8A8_SRGB:
			return requested;
		case TextureFormat::R8G8B8A8_SRGB:
			return TextureFormat::B8G8R8A8_SRGB;
		case TextureFormat::R8G8B8A8_UNORM:
		default:
			return TextureFormat::B8G8R8A8_UNORM;
	}
}

CA::MetalLayer *CreateLayerForGLFWWindow(void *windowHandle, MTL::Device *device)
{
#if defined(__APPLE__)
	auto *glfwWindow = static_cast<GLFWwindow *>(windowHandle);
	if (glfwWindow == nullptr)
	{
		throw std::invalid_argument("Metal swapchain requires a valid GLFW window handle");
	}

	id nsWindow = glfwGetCocoaWindow(glfwWindow);
	if (nsWindow == nullptr)
	{
		throw std::runtime_error("glfwGetCocoaWindow returned null");
	}

	SEL   contentViewSel = sel_registerName("contentView");
	auto *contentView    = ((id(*)(id, SEL)) objc_msgSend)(nsWindow, contentViewSel);
	if (contentView == nullptr)
	{
		throw std::runtime_error("Failed to obtain NSWindow contentView");
	}

	CA::MetalLayer *layer = CA::MetalLayer::layer();
	if (layer == nullptr)
	{
		throw std::runtime_error("Failed to create CAMetalLayer");
	}
	layer->retain();
	layer->setDevice(device);

	SEL setLayerSel = sel_registerName("setLayer:");
	((void (*)(id, SEL, id)) objc_msgSend)(contentView, setLayerSel, reinterpret_cast<id>(layer));

	SEL setWantsLayerSel = sel_registerName("setWantsLayer:");
	((void (*)(id, SEL, signed char)) objc_msgSend)(contentView, setWantsLayerSel, kObjcYes);

	return layer;
#else
	(void) windowHandle;
	(void) device;
	throw std::runtime_error("GLFW Metal layer adapter is only supported on Apple platforms");
#endif
}

void ConfigureLayer(CA::MetalLayer *layer, MTL::Device *device, TextureFormat format,
                    uint32_t width, uint32_t height, uint32_t imageCount, bool vsync)
{
	layer->setDevice(device);
	layer->setPixelFormat(TextureFormatToMetal(format));
	layer->setFramebufferOnly(true);
	layer->setDrawableSize(CGSize{static_cast<CGFloat>(width), static_cast<CGFloat>(height)});
	layer->setMaximumDrawableCount(imageCount);
	layer->setDisplaySyncEnabled(vsync);
}

void CreatePlaceholderBackBuffers(MetalDevice *device, uint32_t width, uint32_t height,
                                  uint32_t imageCount, TextureFormat format,
                                  std::vector<TextureHandle>     &backBuffers,
                                  std::vector<TextureViewHandle> &backBufferViews)
{
	backBuffers.resize(imageCount);
	backBufferViews.resize(imageCount);

	MTL::TextureDescriptor *textureDesc = MTL::TextureDescriptor::alloc()->init();
	textureDesc->setTextureType(MTL::TextureType2D);
	textureDesc->setPixelFormat(TextureFormatToMetal(format));
	textureDesc->setWidth(width);
	textureDesc->setHeight(height);
	textureDesc->setDepth(1);
	textureDesc->setMipmapLevelCount(1);
	textureDesc->setArrayLength(1);
	textureDesc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
	textureDesc->setStorageMode(SelectStorageMode(ResourceUsage::Static, AllocationHints{}, device->IsUnifiedMemory()));

	TextureDesc rhiTextureDesc{};
	rhiTextureDesc.width          = width;
	rhiTextureDesc.height         = height;
	rhiTextureDesc.depth          = 1;
	rhiTextureDesc.mipLevels      = 1;
	rhiTextureDesc.arrayLayers    = 1;
	rhiTextureDesc.format         = format;
	rhiTextureDesc.type           = TextureType::TEXTURE_2D;
	rhiTextureDesc.resourceUsage  = ResourceUsage::Static;
	rhiTextureDesc.isRenderTarget = true;

	for (uint32_t i = 0; i < imageCount; ++i)
	{
		MTL::Texture *texture = device->GetMTLDevice()->newTexture(textureDesc);
		if (texture == nullptr)
		{
			textureDesc->release();
			throw std::runtime_error("Failed to create placeholder Metal swapchain texture");
		}

		TextureHandle   backBuffer = RefCntPtr<IRHITexture>::Create(new MetalTexture(texture, rhiTextureDesc, true));
		TextureViewDesc viewDesc{};
		viewDesc.texture       = backBuffer.Get();
		viewDesc.format        = format;
		TextureViewHandle view = RefCntPtr<IRHITextureView>::Create(new MetalTextureView(viewDesc));

		backBuffers[i]     = backBuffer;
		backBufferViews[i] = view;
	}

	textureDesc->release();
}

}        // namespace

MetalSwapchain::MetalSwapchain(MetalDevice *device, const SwapchainDesc &desc) :
    device_(device),
    format_(ResolveLayerCompatibleFormat(desc.format)),
    width_(std::max(desc.width, 1u)),
    height_(std::max(desc.height, 1u)),
    imageCount_(std::max(desc.bufferCount, 2u))
{
	if (device_ == nullptr)
	{
		throw std::invalid_argument("MetalSwapchain requires a valid MetalDevice");
	}

	switch (desc.windowHandleType)
	{
		case WindowHandleType::MetalLayer:
			layer_ = reinterpret_cast<CA::MetalLayer *>(desc.windowHandle);
			if (layer_ == nullptr)
			{
				throw std::invalid_argument("WindowHandleType::MetalLayer requires a valid CAMetalLayer handle");
			}
			layer_->retain();
			ownsLayer_ = true;
			break;
		case WindowHandleType::GLFW:
			layer_     = CreateLayerForGLFWWindow(desc.windowHandle, device_->GetMTLDevice());
			ownsLayer_ = true;
			break;
		default:
			throw std::runtime_error("Unsupported window handle type for Metal swapchain");
	}

	ConfigureLayer(layer_, device_->GetMTLDevice(), format_, width_, height_, imageCount_, desc.vsync);

	drawables_.resize(imageCount_, nullptr);
	CreatePlaceholderBackBuffers(device_, width_, height_, imageCount_, format_, backBuffers_, backBufferViews_);
}

MetalSwapchain::~MetalSwapchain()
{
	ReleaseDrawables();
	backBuffers_.clear();
	backBufferViews_.clear();

	if (ownsLayer_ && layer_ != nullptr)
	{
		layer_->release();
	}
	layer_ = nullptr;
}

void MetalSwapchain::ReleaseDrawables()
{
	for (CA::MetalDrawable *&drawable : drawables_)
	{
		if (drawable != nullptr)
		{
			drawable->release();
			drawable = nullptr;
		}
	}
}

void MetalSwapchain::RebuildBackBufferWrappers(uint32_t index, CA::MetalDrawable *drawable)
{
	if (index >= imageCount_ || drawable == nullptr)
	{
		return;
	}

	MTL::Texture *texture = drawable->texture();
	if (texture == nullptr)
	{
		backBuffers_[index]     = nullptr;
		backBufferViews_[index] = nullptr;
		return;
	}

	const TextureFormat textureFormat = TextureFormatFromMetal(texture->pixelFormat());
	const TextureType   textureType   = TextureTypeFromMetal(texture->textureType());

	TextureHandle backBuffer = RefCntPtr<IRHITexture>::Create(new MetalTexture(
	    texture,
	    textureFormat == TextureFormat::UNDEFINED ? format_ : textureFormat,
	    static_cast<uint32_t>(texture->width()),
	    static_cast<uint32_t>(texture->height()),
	    static_cast<uint32_t>(texture->depth()),
	    static_cast<uint32_t>(texture->mipmapLevelCount()),
	    std::max(static_cast<uint32_t>(texture->arrayLength()), 1u),
	    textureType,
	    false));

	TextureViewDesc viewDesc{};
	viewDesc.texture = backBuffer.Get();
	viewDesc.format  = backBuffer->GetFormat();

	TextureViewHandle backBufferView = RefCntPtr<IRHITextureView>::Create(new MetalTextureView(viewDesc));

	backBuffers_[index]     = backBuffer;
	backBufferViews_[index] = backBufferView;
}

SwapchainStatus MetalSwapchain::AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore)
{
	(void) signalSemaphore;

	if (layer_ == nullptr || imageCount_ == 0)
	{
		imageIndex = 0;
		return SwapchainStatus::ERROR_OCCURRED;
	}

	NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

	imageIndex = frameIndex_ % imageCount_;

	if (drawables_[imageIndex] != nullptr)
	{
		drawables_[imageIndex]->release();
		drawables_[imageIndex] = nullptr;
	}

	CA::MetalDrawable *drawable = layer_->nextDrawable();
	if (drawable == nullptr)
	{
		pool->release();
		return SwapchainStatus::OUT_OF_DATE;
	}

	drawable->retain();
	drawables_[imageIndex] = drawable;
	RebuildBackBufferWrappers(imageIndex, drawable);

	frameIndex_++;

	pool->release();
	return SwapchainStatus::SUCCESS;
}

SwapchainStatus MetalSwapchain::Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore)
{
	(void) waitSemaphore;

	if (imageIndex >= imageCount_ || drawables_[imageIndex] == nullptr)
	{
		return SwapchainStatus::ERROR_OCCURRED;
	}

	MTL::CommandQueue *queue = device_->GetCommandQueue(QueueType::GRAPHICS);
	if (queue == nullptr)
	{
		return SwapchainStatus::ERROR_OCCURRED;
	}

	NS::AutoreleasePool *pool = NS::AutoreleasePool::alloc()->init();

	MTL::CommandBuffer *commandBuffer = queue->commandBuffer();
	if (commandBuffer == nullptr)
	{
		pool->release();
		return SwapchainStatus::ERROR_OCCURRED;
	}

	commandBuffer->presentDrawable(drawables_[imageIndex]);
	commandBuffer->commit();

	drawables_[imageIndex]->release();
	drawables_[imageIndex] = nullptr;

	pool->release();
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

	if (layer_ != nullptr)
	{
		layer_->setDrawableSize(CGSize{static_cast<CGFloat>(width_), static_cast<CGFloat>(height_)});
	}

	ReleaseDrawables();
	CreatePlaceholderBackBuffers(device_, width_, height_, imageCount_, format_, backBuffers_, backBufferViews_);
}

SurfaceTransform MetalSwapchain::GetPreTransform() const
{
	return SurfaceTransform::IDENTITY;
}

}        // namespace rhi::metal3
