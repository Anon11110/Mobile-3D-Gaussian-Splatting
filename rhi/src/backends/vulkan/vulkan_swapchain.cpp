#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "vulkan_backend.h"

#if !defined(__ANDROID__)
#	include <GLFW/glfw3.h>
#endif

namespace rhi::vulkan
{

VulkanSwapchain::VulkanSwapchain(VkInstance instance, VkDevice device, VkPhysicalDevice physicalDevice, VmaAllocator allocator,
                                 VkSurfaceKHR surface, VkQueue graphicsQueue, const SwapchainDesc &desc) :
    instance(instance), device(device), physicalDevice(physicalDevice), allocator(allocator), surface(surface), graphicsQueue(graphicsQueue), swapchain(VK_NULL_HANDLE), renderPass(VK_NULL_HANDLE)
{
	// Query surface capabilities
	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

	// Choose surface format
	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

	// Convert requested format to Vulkan format
	VkFormat requestedVkFormat = TextureFormatToVulkan(desc.format);

	// Try to find the requested format
	VkSurfaceFormatKHR chosenFormat         = formats[0];
	bool               foundRequestedFormat = false;

	for (const auto &format : formats)
	{
		// First priority: exact match with requested format
		if (format.format == requestedVkFormat)
		{
			chosenFormat         = format;
			foundRequestedFormat = true;
			break;
		}
	}

	// If requested format not found, try compatible formats
	if (!foundRequestedFormat)
	{
		// Map common format substitutions
		if (requestedVkFormat == VK_FORMAT_R8G8B8A8_UNORM)
		{
			// Try BGRA8 UNORM as fallback
			for (const auto &format : formats)
			{
				if (format.format == VK_FORMAT_B8G8R8A8_UNORM)
				{
					chosenFormat         = format;
					foundRequestedFormat = true;
					break;
				}
			}
		}
		else if (requestedVkFormat == VK_FORMAT_R8G8B8A8_SRGB)
		{
			// Try BGRA8 SRGB as fallback
			for (const auto &format : formats)
			{
				if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					chosenFormat         = format;
					foundRequestedFormat = true;
					break;
				}
			}
		}
	}

	// Final fallback: prefer BGRA8 SRGB (most compatible)
	if (!foundRequestedFormat)
	{
		for (const auto &format : formats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosenFormat = format;
				break;
			}
		}
	}

	swapchainFormat     = chosenFormat.format;
	chosenSurfaceFormat = chosenFormat;

	// Choose present mode
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

	chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;        // Always available
	if (!desc.vsync)
	{
		for (const auto &mode : presentModes)
		{
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				chosenPresentMode = mode;
				break;
			}
			else if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				chosenPresentMode = mode;
			}
		}
	}

	// Choose extent
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		swapchainExtent = capabilities.currentExtent;
	}
	else
	{
		swapchainExtent = {
		    std::clamp(desc.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		    std::clamp(desc.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
	}

	// Choose image count
	requestedBufferCount = desc.bufferCount;
	uint32_t imageCount  = std::max(desc.bufferCount, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0)
	{
		imageCount = std::min(imageCount, capabilities.maxImageCount);
	}

	// Store disablePreRotation flag
	disablePreRotation = desc.disablePreRotation;

	// Create swapchain
	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface          = surface;
	createInfo.minImageCount    = imageCount;
	createInfo.imageFormat      = swapchainFormat;
	createInfo.imageColorSpace  = chosenFormat.colorSpace;
	createInfo.imageExtent      = swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	// Use IDENTITY transform if pre-rotation is disabled, otherwise use current transform
	if (disablePreRotation)
	{
		createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		currentPreTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else
	{
		createInfo.preTransform = capabilities.currentTransform;
		currentPreTransform     = capabilities.currentTransform;
	}
	createInfo.presentMode  = chosenPresentMode;
	createInfo.clipped      = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create Vulkan swapchain");
	}

	// Get swapchain images
	uint32_t actualImageCount;
	vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
	swapchainImages.resize(actualImageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, swapchainImages.data());

	// Create back buffer textures
	backBuffers.reserve(actualImageCount);
	backBufferViews.reserve(actualImageCount);
	for (uint32_t i = 0; i < actualImageCount; i++)
	{
		// Create texture with refCount = 1, use RefCntPtr::Create to avoid extra AddRef
		VulkanTexture *texture = new VulkanTexture(device, allocator, swapchainImages[i], swapchainFormat,
		                                           swapchainExtent.width, swapchainExtent.height, true);
		backBuffers.push_back(RefCntPtr<IRHITexture>::Create(texture));

		TextureViewDesc viewDesc{};
		viewDesc.texture        = backBuffers[i].Get();
		viewDesc.aspectMask     = TextureAspect::COLOR;
		VulkanTextureView *view = new VulkanTextureView(device, viewDesc);
		backBufferViews.push_back(RefCntPtr<IRHITextureView>::Create(view));
	}
}

VulkanSwapchain::~VulkanSwapchain()
{
	// Destroy framebuffers
	for (auto framebuffer : framebuffers)
	{
		if (framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
	}

	backBuffers.clear();

	if (swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device, swapchain, nullptr);
	}

	if (surface != VK_NULL_HANDLE)
	{
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}
}

SwapchainStatus VulkanSwapchain::AcquireNextImage(uint32_t &imageIndex, IRHISemaphore *signalSemaphore)
{
	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (signalSemaphore)
	{
		auto *vkSemaphore = static_cast<VulkanSemaphore *>(signalSemaphore);
		semaphore         = vkSemaphore->GetHandle();
	}

	VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);

	switch (result)
	{
		case VK_SUCCESS:
			return SwapchainStatus::SUCCESS;
		case VK_ERROR_OUT_OF_DATE_KHR:
			return SwapchainStatus::OUT_OF_DATE;
		case VK_SUBOPTIMAL_KHR:
			return SwapchainStatus::SUBOPTIMAL;
		default:
			return SwapchainStatus::ERROR_OCCURRED;
	}
}

SwapchainStatus VulkanSwapchain::Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore)
{
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (waitSemaphore)
	{
		auto *vkSemaphore              = static_cast<VulkanSemaphore *>(waitSemaphore);
		semaphore                      = vkSemaphore->GetHandle();
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores    = &semaphore;
	}

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains    = &swapchain;
	presentInfo.pImageIndices  = &imageIndex;

	VkResult result = vkQueuePresentKHR(graphicsQueue, &presentInfo);

	switch (result)
	{
		case VK_SUCCESS:
			return SwapchainStatus::SUCCESS;
		case VK_ERROR_OUT_OF_DATE_KHR:
			return SwapchainStatus::OUT_OF_DATE;
		case VK_SUBOPTIMAL_KHR:
			return SwapchainStatus::SUBOPTIMAL;
		default:
			return SwapchainStatus::ERROR_OCCURRED;
	}
}

IRHITexture *VulkanSwapchain::GetBackBuffer(uint32_t index)
{
	return backBuffers[index].Get();
}

IRHITextureView *VulkanSwapchain::GetBackBufferView(uint32_t index)
{
	return backBufferViews[index].Get();
}

uint32_t VulkanSwapchain::GetImageCount() const
{
	return static_cast<uint32_t>(backBuffers.size());
}

void VulkanSwapchain::Resize(uint32_t width, uint32_t height)
{
	// Wait for device to be idle before recreating swapchain
	vkDeviceWaitIdle(device);

	for (auto framebuffer : framebuffers)
	{
		if (framebuffer != VK_NULL_HANDLE)
		{
			vkDestroyFramebuffer(device, framebuffer, nullptr);
		}
	}
	framebuffers.clear();

	VkSwapchainKHR oldSwapchain = swapchain;

	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		swapchainExtent = capabilities.currentExtent;
	}
	else
	{
		swapchainExtent = {
		    std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
		    std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
	}

	uint32_t imageCount = std::max(requestedBufferCount, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0)
	{
		imageCount = std::min(imageCount, capabilities.maxImageCount);
	}

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface          = surface;
	createInfo.minImageCount    = imageCount;
	createInfo.imageFormat      = chosenSurfaceFormat.format;
	createInfo.imageColorSpace  = chosenSurfaceFormat.colorSpace;
	createInfo.imageExtent      = swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode      = chosenPresentMode;
	createInfo.clipped          = VK_TRUE;
	createInfo.oldSwapchain     = oldSwapchain;

	// Use IDENTITY transform if pre-rotation is disabled, otherwise use current transform
	if (disablePreRotation)
	{
		createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		currentPreTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else
	{
		createInfo.preTransform = capabilities.currentTransform;
		currentPreTransform     = capabilities.currentTransform;
	}

	if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to recreate Vulkan swapchain");
	}

	// Clear back buffers BEFORE destroying old swapchain
	// This ensures image views are destroyed before the swapchain that owns the images
	backBuffers.clear();
	backBufferViews.clear();

	if (oldSwapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
	}

	// Get new swapchain images
	uint32_t actualImageCount;
	vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
	swapchainImages.resize(actualImageCount);
	vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, swapchainImages.data());

	// Create new back buffer textures
	backBuffers.reserve(actualImageCount);
	backBufferViews.reserve(actualImageCount);
	for (uint32_t i = 0; i < actualImageCount; i++)
	{
		// Create texture with refCount = 1, use RefCntPtr::Create to avoid extra AddRef
		VulkanTexture *texture = new VulkanTexture(device, allocator, swapchainImages[i],
		                                           chosenSurfaceFormat.format,
		                                           swapchainExtent.width, swapchainExtent.height, true);
		backBuffers.push_back(RefCntPtr<IRHITexture>::Create(texture));

		TextureViewDesc viewDesc{};
		viewDesc.texture        = backBuffers[i].Get();
		viewDesc.aspectMask     = TextureAspect::COLOR;
		VulkanTextureView *view = new VulkanTextureView(device, viewDesc);
		backBufferViews.push_back(RefCntPtr<IRHITextureView>::Create(view));
	}
}

VkFramebuffer VulkanSwapchain::GetFramebuffer(uint32_t index, VkRenderPass renderPass)
{
	// Resize framebuffers vector if needed
	if (framebuffers.size() != backBuffers.size())
	{
		// Clean up old framebuffers
		for (auto framebuffer : framebuffers)
		{
			if (framebuffer != VK_NULL_HANDLE)
			{
				vkDestroyFramebuffer(device, framebuffer, nullptr);
			}
		}
		framebuffers.resize(backBuffers.size(), VK_NULL_HANDLE);
	}

	// Create framebuffer for this image if it doesn't exist
	if (framebuffers[index] == VK_NULL_HANDLE)
	{
		// Cast to VulkanTexture to access GetImageView method
		auto       *vkTexture     = static_cast<VulkanTexture *>(backBuffers[index].Get());
		VkImageView attachments[] = {vkTexture->GetImageView()};

		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass      = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments    = attachments;
		framebufferInfo.width           = swapchainExtent.width;
		framebufferInfo.height          = swapchainExtent.height;
		framebufferInfo.layers          = 1;

		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[index]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to create framebuffer");
		}
	}

	return framebuffers[index];
}

SurfaceTransform VulkanSwapchain::GetPreTransform() const
{
	switch (currentPreTransform)
	{
		case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR:
			return SurfaceTransform::ROTATE_90;
		case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR:
			return SurfaceTransform::ROTATE_180;
		case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR:
			return SurfaceTransform::ROTATE_270;
		case VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR:
			return SurfaceTransform::HORIZONTAL_MIRROR;
		case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:
		default:
			return SurfaceTransform::IDENTITY;
	}
}

}        // namespace rhi::vulkan