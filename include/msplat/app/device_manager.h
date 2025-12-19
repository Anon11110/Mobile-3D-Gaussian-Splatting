#pragma once

#include "core/containers/memory.h"
#include "core/vfs.h"
#include "msplat/app/platform_adapter.h"
#include "rhi/rhi.h"

struct GLFWwindow;

namespace msplat::app
{

class IApplication;
class IPlatformAdapter;

/**
 * @class DeviceManager
 * @brief Manages the window, graphics device, and swapchain.
 *
 * This class is responsible for all the low-level details of setting up
 * and managing the application window and the RHI device.
 *
 * It uses a platform adapter to abstract platform-specific windowing,
 * allowing the same DeviceManager to work on desktop, Android, and iOS.
 */
class DeviceManager
{
  public:
	DeviceManager(IApplication *app);
	~DeviceManager();

	DeviceManager(const DeviceManager &)            = delete;
	DeviceManager &operator=(const DeviceManager &) = delete;
	DeviceManager(DeviceManager &&)                 = delete;
	DeviceManager &operator=(DeviceManager &&)      = delete;

	/**
	 * @brief Initializes the window and RHI device and starts the main loop.
	 * @param width The initial width of the window.
	 * @param height The initial height of the window.
	 * @param title The title of the window.
	 * @return 0 on success, non-zero on failure.
	 */
	int Run(int width, int height, const char *title);

	/**
	 * @brief Initialize with an existing platform adapter (for Android).
	 * @param adapter Pre-created platform adapter.
	 * @return True on success.
	 */
	bool InitWithAdapter(IPlatformAdapter *adapter);

	/**
	 * @brief Render a single frame. Called from platform main loop.
	 * @param deltaTime Time since last frame.
	 */
	void RenderFrame(float deltaTime);

	/**
	 * @brief Handle window resize event.
	 * @param width New width.
	 * @param height New height.
	 */
	void HandleResize(int width, int height);

	void Shutdown();

	rhi::IRHIDevice *GetDevice() const
	{
		return m_device.Get();
	}
	rhi::IRHISwapchain *GetSwapchain() const
	{
		return m_swapchain.Get();
	}
	IPlatformAdapter *GetPlatformAdapter() const
	{
		return m_platformAdapter;
	}

	/**
	 * @brief Set the virtual file system for asset loading.
	 * @param vfs Shared pointer to the VFS.
	 */
	void SetVFS(container::shared_ptr<vfs::IFileSystem> vfs)
	{
		m_vfs = vfs;
	}

	/**
	 * @brief Get the virtual file system.
	 * @return Shared pointer to the VFS, may be nullptr if not set.
	 */
	container::shared_ptr<vfs::IFileSystem> GetVFS() const
	{
		return m_vfs;
	}

	GLFWwindow *GetWindow() const;

	IApplication *GetApplication() const
	{
		return m_app;
	}

  private:
	void InitRHI();
	void MainLoop();

	IApplication                           *m_app             = nullptr;
	IPlatformAdapter                       *m_platformAdapter = nullptr;
	bool                                    m_ownsAdapter     = false;
	rhi::DeviceHandle                       m_device;
	rhi::SwapchainHandle                    m_swapchain;
	container::shared_ptr<vfs::IFileSystem> m_vfs;
	bool                                    m_initialized = false;
	bool                                    m_appShutdown = false;
};

}        // namespace msplat::app
