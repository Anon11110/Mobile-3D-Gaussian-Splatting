#include <algorithm>
#include <iostream>
#include <stdexcept>

#include "vulkan_backend.h"

#include <GLFW/glfw3.h>

namespace RHI
{

VulkanSwapchain::VulkanSwapchain(VkDevice device, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                 VkQueue graphicsQueue, const SwapchainDesc &desc) :
    device(device), physicalDevice(physicalDevice), surface(surface), graphicsQueue(graphicsQueue), swapchain(VK_NULL_HANDLE), renderPass(VK_NULL_HANDLE)
{
	// Query surface capabilities
	VkSurfaceCapabilitiesKHR capabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

	// Choose surface format
	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

	VkSurfaceFormatKHR chosenFormat = formats[0];
	for (const auto &format : formats)
	{
		if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			chosenFormat = format;
			break;
		}
	}
	swapchainFormat = chosenFormat.format;

	// Choose present mode
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

	VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;        // Always available
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
	uint32_t imageCount = std::max(desc.bufferCount, capabilities.minImageCount);
	if (capabilities.maxImageCount > 0)
	{
		imageCount = std::min(imageCount, capabilities.maxImageCount);
	}

	// Create swapchain
	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface          = surface;
	createInfo.minImageCount    = imageCount;
	createInfo.imageFormat      = swapchainFormat;
	createInfo.imageColorSpace  = chosenFormat.colorSpace;
	createInfo.imageExtent      = swapchainExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	createInfo.preTransform     = capabilities.currentTransform;
	createInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode      = chosenPresentMode;
	createInfo.clipped          = VK_TRUE;
	createInfo.oldSwapchain     = VK_NULL_HANDLE;

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
	for (uint32_t i = 0; i < actualImageCount; i++)
	{
		backBuffers.push_back(std::make_unique<VulkanTexture>(device, swapchainImages[i], swapchainFormat,
		                                                      swapchainExtent.width, swapchainExtent.height, true));
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
}

uint32_t VulkanSwapchain::AcquireNextImage(IRHISemaphore *signalSemaphore)
{
	uint32_t imageIndex;

	VkSemaphore semaphore = VK_NULL_HANDLE;
	if (signalSemaphore)
	{
		auto *vkSemaphore = static_cast<VulkanSemaphore *>(signalSemaphore);
		semaphore         = vkSemaphore->GetHandle();
	}

	VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("Swapchain out of date - resize needed");
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to acquire swapchain image");
	}

	return imageIndex;
}

void VulkanSwapchain::Present(uint32_t imageIndex, IRHISemaphore *waitSemaphore)
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

	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		throw std::runtime_error("Swapchain out of date - resize needed");
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to present swapchain image");
	}
}

IRHITexture *VulkanSwapchain::GetBackBuffer(uint32_t index)
{
	return backBuffers[index].get();
}

uint32_t VulkanSwapchain::GetImageCount() const
{
	return static_cast<uint32_t>(backBuffers.size());
}

void VulkanSwapchain::Resize(uint32_t width, uint32_t height)
{
	// TODO: Implement swapchain recreation for resize
	throw std::runtime_error("Swapchain resize not implemented");
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
		VkImageView attachments[] = {backBuffers[index]->GetImageView()};

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

}        // namespace RHI