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
	int run(int width, int height, const char* title);

	rhi::IRHIDevice* getDevice() const { return m_device.get(); }
	rhi::IRHISwapchain* getSwapchain() const { return m_swapchain.get(); }
	GLFWwindow* getWindow() const { return m_window; }

	IApplication* m_app; // Made public for callbacks

private:
	void initWindow(int width, int height, const char* title);
	void initRHI();
	void mainLoop();
	void shutdown();

	GLFWwindow* m_window = nullptr;
	container::unique_ptr<rhi::IRHIDevice> m_device;
	container::unique_ptr<rhi::IRHISwapchain> m_swapchain;
};

} // namespace msplat::app