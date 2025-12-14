#include "msplat/app/application.h"
#include "msplat/app/platform_adapter.h"

#if defined(__ANDROID__)

#	include <android/log.h>
#	include <android/native_window.h>
#	include <android_native_app_glue.h>

#	define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MSplat", __VA_ARGS__)
#	define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "MSplat", __VA_ARGS__)
#	define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MSplat", __VA_ARGS__)

namespace msplat::app
{

/**
 * @brief Android platform adapter using NativeActivity.
 *
 * This adapter handles windowing and input for Android platforms
 * using android_native_app_glue.
 */
class AndroidAdapter : public IPlatformAdapter
{
  public:
	AndroidAdapter() = default;
	~AndroidAdapter() override
	{
		Shutdown();
	}

	bool InitializeWithAndroidApp(android_app *androidApp)
	{
		if (!androidApp)
		{
			LOGE("android_app is null");
			return false;
		}

		m_androidApp = androidApp;
		m_window     = androidApp->window;

		if (!m_window)
		{
			LOGW("ANativeWindow not yet available");
			return false;
		}

		m_width  = ANativeWindow_getWidth(m_window);
		m_height = ANativeWindow_getHeight(m_window);

		LOGI("AndroidAdapter initialized: %dx%d", m_width, m_height);
		return true;
	}

	bool Initialize(int width, int height, const char *title) override
	{
		// On Android, initialization happens via InitializeWithAndroidApp
		// This method is provided for interface compatibility
		(void) width;
		(void) height;
		(void) title;
		LOGW("Initialize() called on Android - use InitializeWithAndroidApp() instead");
		return m_window != nullptr;
	}

	void Shutdown() override
	{
		m_window      = nullptr;
		m_androidApp  = nullptr;
		m_shouldClose = true;
	}

	bool ShouldClose() override
	{
		return m_shouldClose || (m_androidApp && m_androidApp->destroyRequested);
	}

	void PollEvents() override
	{
		// Event polling is handled by android_main loop
		// This is a no-op on Android
	}

	NativeWindowHandle GetNativeWindowHandle() override
	{
		return NativeWindowHandle::FromAndroid(m_window);
	}

	void GetFramebufferSize(int *width, int *height) override
	{
		if (m_window)
		{
			*width  = ANativeWindow_getWidth(m_window);
			*height = ANativeWindow_getHeight(m_window);
		}
		else
		{
			*width  = m_width;
			*height = m_height;
		}
	}

	void SetEventCallbacks(IApplication *app) override
	{
		m_app = app;
	}

	// Android-specific methods
	void SetWindow(ANativeWindow *window)
	{
		m_window = window;
		if (window)
		{
			m_width  = ANativeWindow_getWidth(window);
			m_height = ANativeWindow_getHeight(window);
		}
	}

	void RequestClose()
	{
		m_shouldClose = true;
	}

	android_app *GetAndroidApp() const
	{
		return m_androidApp;
	}
	ANativeWindow *GetWindow() const
	{
		return m_window;
	}
	IApplication *GetApplication() const
	{
		return m_app;
	}

	int32_t HandleInputEvent(AInputEvent *event)
	{
		if (!m_app)
			return 0;

		int32_t eventType = AInputEvent_getType(event);

		if (eventType == AINPUT_EVENT_TYPE_MOTION)
		{
			int32_t action = AMotionEvent_getAction(event);
			float   x      = AMotionEvent_getX(event, 0);
			float   y      = AMotionEvent_getY(event, 0);

			// Map touch events to mouse events for compatibility
			switch (action & AMOTION_EVENT_ACTION_MASK)
			{
				case AMOTION_EVENT_ACTION_DOWN:
					m_app->OnMouseButton(0, 1, 0);        // Left button press
					m_app->OnMouseMove(x, y);
					break;
				case AMOTION_EVENT_ACTION_UP:
					m_app->OnMouseButton(0, 0, 0);        // Left button release
					break;
				case AMOTION_EVENT_ACTION_MOVE:
					m_app->OnMouseMove(x, y);
					break;
			}
			return 1;
		}

		if (eventType == AINPUT_EVENT_TYPE_KEY)
		{
			int32_t keyCode = AKeyEvent_getKeyCode(event);
			int32_t action  = AKeyEvent_getAction(event);

			// Map Android key events to GLFW-compatible key codes
			int glfwAction = (action == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
			m_app->OnKey(keyCode, glfwAction, 0);
			return 1;
		}

		return 0;
	}

  private:
	android_app   *m_androidApp  = nullptr;
	ANativeWindow *m_window      = nullptr;
	IApplication  *m_app         = nullptr;
	int            m_width       = 0;
	int            m_height      = 0;
	bool           m_shouldClose = false;
};

IPlatformAdapter *CreatePlatformAdapter()
{
	return new AndroidAdapter();
}

// Global accessor for AndroidAdapter used by android_main
static AndroidAdapter *g_androidAdapter = nullptr;

AndroidAdapter *GetAndroidAdapter()
{
	return g_androidAdapter;
}

void SetAndroidAdapter(AndroidAdapter *adapter)
{
	g_androidAdapter = adapter;
}

}        // namespace msplat::app

#endif        // __ANDROID__
