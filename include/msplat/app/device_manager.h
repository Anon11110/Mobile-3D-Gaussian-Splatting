#pragma once

#include "rhi/rhi.h"
#include "core/containers/memory.h"

struct GLFWwindow;

namespace msplat::app {

class IApplication;

/**
 * @class DeviceManager
 * @brief Manages the window, graphics device, and swapchain.
 *
 * This class is responsible for all the low-level details of setting up
 * and managing the application window and the RHI device.
 */
class DeviceManager {
public:
	DeviceManager(IApplication* app);
	~DeviceManager();

	/**
	 * @brief Initializes the window and RHI device and starts the main loop.
	 * @param width The initial width of the window.
	 * @param height The initial height of the window.
	 * @param title The title of the window.
	 * @return 0 on success, non-zero on failure.
	 */
	int Run(int width, int height, const char* title);

	rhi::IRHIDevice* GetDevice() const { return m_device.Get(); }
	rhi::IRHISwapchain* GetSwapchain() const { return m_swapchain.Get(); }
	GLFWwindow* GetWindow() const { return m_window; }

	IApplication* m_app; // Made public for callbacks

private:
	void InitWindow(int width, int height, const char* title);
	void InitRHI();
	void MainLoop();
	void Shutdown();

	GLFWwindow* m_window = nullptr;
	rhi::DeviceHandle m_device;
	rhi::SwapchainHandle m_swapchain;
};

} // namespace msplat::app