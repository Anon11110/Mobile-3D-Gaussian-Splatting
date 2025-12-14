#include "core/log.h"
#include "msplat/app/application.h"
#include "msplat/app/platform_adapter.h"
#include <GLFW/glfw3.h>
#include <stdexcept>

namespace msplat::app
{

/**
 * @brief Desktop platform adapter using GLFW.
 *
 * This adapter handles windowing and input for desktop platforms
 * (Windows, Linux, macOS).
 */
class DesktopAdapter : public IPlatformAdapter
{
  public:
	DesktopAdapter() = default;
	~DesktopAdapter() override
	{
		Shutdown();
	}

	bool Initialize(int width, int height, const char *title) override
	{
		if (!glfwInit())
		{
			LOG_FATAL("Failed to initialize GLFW");
			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
		if (!m_window)
		{
			glfwTerminate();
			LOG_FATAL("Failed to create window");
			return false;
		}

		glfwSetWindowUserPointer(m_window, this);
		return true;
	}

	void Shutdown() override
	{
		if (m_window)
		{
			glfwDestroyWindow(m_window);
			m_window = nullptr;
		}
		glfwTerminate();
	}

	bool ShouldClose() override
	{
		return m_window && glfwWindowShouldClose(m_window);
	}

	void PollEvents() override
	{
		glfwPollEvents();
	}

	NativeWindowHandle GetNativeWindowHandle() override
	{
		return NativeWindowHandle::FromGLFW(m_window);
	}

	void GetFramebufferSize(int *width, int *height) override
	{
		if (m_window)
		{
			glfwGetFramebufferSize(m_window, width, height);
		}
		else
		{
			*width  = 0;
			*height = 0;
		}
	}

	void SetEventCallbacks(IApplication *app) override
	{
		m_app = app;
		glfwSetKeyCallback(m_window, KeyCallback);
		glfwSetMouseButtonCallback(m_window, MouseButtonCallback);
		glfwSetCursorPosCallback(m_window, CursorPositionCallback);
		glfwSetFramebufferSizeCallback(m_window, FramebufferSizeCallback);
	}

	GLFWwindow *GetGLFWWindow() const
	{
		return m_window;
	}
	IApplication *GetApplication() const
	{
		return m_app;
	}

  private:
	GLFWwindow   *m_window = nullptr;
	IApplication *m_app    = nullptr;

	static void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
	{
		(void) scancode;
		auto *adapter = static_cast<DesktopAdapter *>(glfwGetWindowUserPointer(window));
		if (adapter && adapter->m_app)
		{
			adapter->m_app->OnKey(key, action, mods);
		}
	}

	static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
	{
		auto *adapter = static_cast<DesktopAdapter *>(glfwGetWindowUserPointer(window));
		if (adapter && adapter->m_app)
		{
			adapter->m_app->OnMouseButton(button, action, mods);
		}
	}

	static void CursorPositionCallback(GLFWwindow *window, double xpos, double ypos)
	{
		auto *adapter = static_cast<DesktopAdapter *>(glfwGetWindowUserPointer(window));
		if (adapter && adapter->m_app)
		{
			adapter->m_app->OnMouseMove(xpos, ypos);
		}
	}

	static void FramebufferSizeCallback(GLFWwindow *window, int width, int height)
	{
		auto *adapter = static_cast<DesktopAdapter *>(glfwGetWindowUserPointer(window));
		if (adapter && adapter->m_app)
		{
			adapter->m_app->OnFramebufferResize(width, height);
		}
	}
};

IPlatformAdapter *CreatePlatformAdapter()
{
	return new DesktopAdapter();
}

}        // namespace msplat::app
