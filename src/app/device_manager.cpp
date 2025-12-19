#include "msplat/app/device_manager.h"
#include "core/log.h"
#include "core/timer.h"
#include "msplat/app/application.h"
#include "msplat/app/platform_adapter.h"
#include <stdexcept>

#if !defined(__ANDROID__)
#	include <GLFW/glfw3.h>
#endif

namespace msplat::app
{

// Framebuffer size callback that bridges to DeviceManager
static void OnFramebufferResizeCallback(IApplication *app, int width, int height);

DeviceManager::DeviceManager(IApplication *app) :
    m_app(app)
{}

DeviceManager::~DeviceManager()
{
	Shutdown();
}

bool DeviceManager::InitWithAdapter(IPlatformAdapter *adapter)
{
	if (!adapter)
	{
		LOG_ERROR("Platform adapter is null");
		return false;
	}

	m_platformAdapter = adapter;
	m_ownsAdapter     = false;

	m_platformAdapter->SetEventCallbacks(m_app);

	InitRHI();

	if (!m_app->OnInit(this))
	{
		LOG_ERROR("Application initialization failed");
		return false;
	}

	m_initialized = true;
	return true;
}

void DeviceManager::InitRHI()
{
	LOG_INFO("Initializing RHI device");
	m_device = rhi::CreateRHIDevice();
	LOG_ASSERT(m_device != nullptr, "Failed to create RHI device");

	int fbw, fbh;
	m_platformAdapter->GetFramebufferSize(&fbw, &fbh);

	rhi::SwapchainDesc swapchainDesc{};
	NativeWindowHandle windowHandle = m_platformAdapter->GetNativeWindowHandle();
	swapchainDesc.windowHandle      = windowHandle.handle;
	swapchainDesc.windowHandleType  = static_cast<rhi::WindowHandleType>(
        static_cast<uint32_t>(windowHandle.type));
	swapchainDesc.width  = static_cast<uint32_t>(fbw);
	swapchainDesc.height = static_cast<uint32_t>(fbh);
	swapchainDesc.format = rhi::TextureFormat::R8G8B8A8_UNORM;
	// Query the application if it requires pre-rotation to be disabled.
	// This is needed when using ImGui that don't support pre-rotated rendering.
	swapchainDesc.disablePreRotation = m_app->RequiresDisabledPreRotation();
	m_swapchain                      = m_device->CreateSwapchain(swapchainDesc);
}

void DeviceManager::MainLoop()
{
	timer::Timer frameTimer;
	float        deltaTime = 0.016f;        // Start with a reasonable delta

	while (!m_platformAdapter->ShouldClose())
	{
		frameTimer.start();
		m_platformAdapter->PollEvents();
		m_app->OnUpdate(deltaTime);
		m_app->OnRender();
		// Retire completed GPU operations for resource management
		if (m_device)
		{
			m_device->RetireCompletedFrame();
		}
		frameTimer.stop();
		deltaTime = static_cast<float>(frameTimer.elapsedSeconds());
	}
}

void DeviceManager::RenderFrame(float deltaTime)
{
	if (!m_initialized)
		return;

	m_app->OnUpdate(deltaTime);
	m_app->OnRender();

	// Retire completed GPU operations for resource management
	if (m_device)
	{
		m_device->RetireCompletedFrame();
	}
}

void DeviceManager::HandleResize(int width, int height)
{
	if (m_swapchain && width > 0 && height > 0)
	{
		m_device->WaitIdle();
		m_swapchain->Resize(width, height);
	}
	if (m_app)
	{
		m_app->OnFramebufferResize(width, height);
	}
}

void DeviceManager::Shutdown()
{
	if (m_app && m_device && !m_appShutdown)
	{
		m_appShutdown = true;
		m_device->WaitIdle();
		m_app->OnShutdown();
	}

	m_swapchain = nullptr;
	m_device    = nullptr;

	if (m_platformAdapter && m_ownsAdapter)
	{
		m_platformAdapter->Shutdown();
		delete m_platformAdapter;
	}
	m_platformAdapter = nullptr;
	m_initialized     = false;
}

int DeviceManager::Run(int width, int height, const char *title)
{
	try
	{
		// Create platform adapter
		m_platformAdapter = CreatePlatformAdapter();
		m_ownsAdapter     = true;

		if (!m_platformAdapter)
		{
			LOG_FATAL("Failed to create platform adapter");
			return -1;
		}

		// Initialize window
		if (!m_platformAdapter->Initialize(width, height, title))
		{
			LOG_FATAL("Failed to initialize platform adapter");
			Shutdown();
			return -1;
		}

		m_platformAdapter->SetEventCallbacks(m_app);

		InitRHI();

		// Initialize application
		if (!m_app->OnInit(this))
		{
			LOG_ERROR("Application initialization failed.");
			Shutdown();
			return -1;
		}

		m_initialized = true;

		// Run main loop
		MainLoop();
	}
	catch (const std::exception &e)
	{
		LOG_FATAL("An exception occurred: {}", e.what());
		Shutdown();
		return -1;
	}

	Shutdown();
	return 0;
}

GLFWwindow *DeviceManager::GetWindow() const
{
#if !defined(__ANDROID__)
	if (m_platformAdapter)
	{
		NativeWindowHandle handle = m_platformAdapter->GetNativeWindowHandle();
		if (handle.type == NativeWindowHandle::Type::GLFW)
		{
			return static_cast<GLFWwindow *>(handle.handle);
		}
	}
#endif
	return nullptr;
}

}        // namespace msplat::app
