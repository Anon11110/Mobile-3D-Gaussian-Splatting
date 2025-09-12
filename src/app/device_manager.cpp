#include "msplat/app/device_manager.h"
#include "msplat/app/application.h"
#include "core/log.h"
#include "core/timer.h"
#include <stdexcept>
#include <GLFW/glfw3.h>

namespace msplat::app {

// Forward declare static callbacks for GLFW
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

DeviceManager::DeviceManager(IApplication* app)
	: m_app(app) {}

DeviceManager::~DeviceManager() {
	shutdown();
}

void DeviceManager::initWindow(int width, int height, const char* title) {
	if (!glfwInit()) {
		LOG_FATAL("Failed to initialize GLFW");
		throw std::runtime_error("Failed to initialize GLFW");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
	if (!m_window) {
		glfwTerminate();
		LOG_FATAL("Failed to create window");
		throw std::runtime_error("Failed to create window");
	}

	glfwSetWindowUserPointer(m_window, this);
	glfwSetKeyCallback(m_window, key_callback);
	glfwSetMouseButtonCallback(m_window, mouse_button_callback);
	glfwSetCursorPosCallback(m_window, cursor_position_callback);
	glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback);
}

void DeviceManager::initRHI() {
	LOG_INFO("Initializing RHI device");
	m_device = rhi::CreateRHIDevice();
	LOG_ASSERT(m_device != nullptr, "Failed to create RHI device");

	rhi::SwapchainDesc swapchainDesc{};
	int fbw, fbh;
	glfwGetFramebufferSize(m_window, &fbw, &fbh);
	swapchainDesc.windowHandle = m_window;
	swapchainDesc.width = static_cast<uint32_t>(fbw);
	swapchainDesc.height = static_cast<uint32_t>(fbh);
	swapchainDesc.format = rhi::TextureFormat::R8G8B8A8_UNORM;
	m_swapchain = m_device->CreateSwapchain(swapchainDesc);
}

void DeviceManager::mainLoop() {
	timer::Timer frameTimer;
	float deltaTime = 0.016f; // Start with a reasonable delta

	while (!glfwWindowShouldClose(m_window)) {
		frameTimer.start();
		glfwPollEvents();
		m_app->onUpdate(deltaTime);
		m_app->onRender();
		frameTimer.stop();
		deltaTime = static_cast<float>(frameTimer.elapsedSeconds());
	}
}

void DeviceManager::shutdown() {
	if (m_app && m_device) {
		m_device->WaitIdle();
		m_app->onShutdown();
	}

	m_swapchain.reset();
	m_device.reset();

	if (m_window) {
		glfwDestroyWindow(m_window);
		m_window = nullptr;
	}
	glfwTerminate();
}

int DeviceManager::run(int width, int height, const char* title) {
	try {
		initWindow(width, height, title);
		initRHI();

		if (!m_app->onInit(this)) {
			LOG_ERROR("Application initialization failed.");
			shutdown();
			return -1;
		}

		mainLoop();
	} catch (const std::exception& e) {
		LOG_FATAL("An exception occurred: {}", e.what());
		shutdown();
		return -1;
	}
	return 0;
}

// GLFW Callbacks
static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	auto* manager = static_cast<DeviceManager*>(glfwGetWindowUserPointer(window));
	if (manager && manager->m_app) {
		manager->m_app->onKey(key, action, mods);
	}
}

static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	auto* manager = static_cast<DeviceManager*>(glfwGetWindowUserPointer(window));
	if (manager && manager->m_app) {
		manager->m_app->onMouseButton(button, action, mods);
	}
}

static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
	auto* manager = static_cast<DeviceManager*>(glfwGetWindowUserPointer(window));
	if (manager && manager->m_app) {
		manager->m_app->onMouseMove(xpos, ypos);
	}
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	auto* manager = static_cast<DeviceManager*>(glfwGetWindowUserPointer(window));
	if (manager && manager->getSwapchain()) {
		if (width > 0 && height > 0) {
			manager->getDevice()->WaitIdle();
			manager->getSwapchain()->Resize(width, height);
		}
	}
}

} // namespace msplat::app