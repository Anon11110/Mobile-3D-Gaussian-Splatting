/**
 * @file android_main.cpp
 * @brief Android entry point for Mobile 3D Gaussian Splatting
 *
 * This file provides the android_main() entry point required by
 * android_native_app_glue for pure C++ NativeActivity applications.
 */

#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include "core/log.h"
#include "core/timer.h"
#include "core/vfs.h"
#include "msplat/app/application.h"
#include "msplat/app/device_manager.h"
#include "msplat/app/platform_adapter.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

#include "hybrid_splat_renderer_app.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MSplat", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "MSplat", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MSplat", __VA_ARGS__)

using namespace msplat;

namespace msplat::app
{
class AndroidAdapter;
AndroidAdapter *GetAndroidAdapter();
void            SetAndroidAdapter(AndroidAdapter *adapter);
}        // namespace msplat::app

/**
 * @brief Android platform adapter (inline implementation for simplicity)
 */
class AndroidPlatformAdapter : public app::IPlatformAdapter
{
  public:
	AndroidPlatformAdapter(android_app *androidApp) :
	    m_androidApp(androidApp), m_window(nullptr), m_app(nullptr), m_shouldClose(false), m_pointerCount(0), m_lastPinchDistance(0.0f), m_isPinching(false)
	{}

	~AndroidPlatformAdapter() override
	{
		Shutdown();
	}

	bool Initialize(int width, int height, const char *title) override
	{
		(void) width;
		(void) height;
		(void) title;

		return true;
	}

	void SetWindow(ANativeWindow *window)
	{
		m_window = window;
		if (window)
		{
			LOGI("Window set: %dx%d",
			     ANativeWindow_getWidth(window),
			     ANativeWindow_getHeight(window));
		}
	}

	void Shutdown() override
	{
		m_window      = nullptr;
		m_shouldClose = true;
	}

	bool ShouldClose() override
	{
		return m_shouldClose || (m_androidApp && m_androidApp->destroyRequested);
	}

	void PollEvents() override
	{
		// Events are polled in android_main loop
	}

	app::NativeWindowHandle GetNativeWindowHandle() override
	{
		return app::NativeWindowHandle(
		    app::NativeWindowHandle::Type::AndroidNative,
		    m_window);
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
			*width  = 0;
			*height = 0;
		}
	}

	void SetEventCallbacks(app::IApplication *app) override
	{
		m_app = app;
	}

	void RequestClose()
	{
		m_shouldClose = true;
	}

	float CalculatePinchDistance(AInputEvent *event)
	{
		if (AMotionEvent_getPointerCount(event) < 2)
			return 0.0f;
		float x0 = AMotionEvent_getX(event, 0);
		float y0 = AMotionEvent_getY(event, 0);
		float x1 = AMotionEvent_getX(event, 1);
		float y1 = AMotionEvent_getY(event, 1);
		float dx = x1 - x0;
		float dy = y1 - y0;
		return sqrtf(dx * dx + dy * dy);
	}

	// Handle touch/key input with multi-touch support
	int32_t HandleInputEvent(AInputEvent *event)
	{
		if (!m_app)
			return 0;

		int32_t eventType = AInputEvent_getType(event);

		if (eventType == AINPUT_EVENT_TYPE_MOTION)
		{
			int32_t action       = AMotionEvent_getAction(event);
			int32_t maskedAction = action & AMOTION_EVENT_ACTION_MASK;
			size_t  pointerCount = AMotionEvent_getPointerCount(event);

			float x = AMotionEvent_getX(event, 0);
			float y = AMotionEvent_getY(event, 0);

			switch (maskedAction)
			{
				case AMOTION_EVENT_ACTION_DOWN:
					// First finger down
					m_pointerCount = 1;
					m_isPinching   = false;
					m_app->OnMouseMove(x, y);
					m_app->OnMouseButton(0, 1, 0);
					break;

				case AMOTION_EVENT_ACTION_POINTER_DOWN:
					// Second+ finger down, start pinch zoom
					m_pointerCount = pointerCount;
					if (pointerCount >= 2)
					{
						m_isPinching        = true;
						m_lastPinchDistance = CalculatePinchDistance(event);
						// Release single-finger drag when starting pinch
						m_app->OnMouseButton(0, 0, 0);
					}
					break;

				case AMOTION_EVENT_ACTION_POINTER_UP:
					// One finger lifted, but others still down
					m_pointerCount = pointerCount - 1;
					if (m_pointerCount == 1)
					{
						// Back to single finger, resume drag
						m_isPinching = false;
						m_app->OnMouseMove(x, y);
						m_app->OnMouseButton(0, 1, 0);
					}
					break;

				case AMOTION_EVENT_ACTION_UP:
					// Last finger up
					m_pointerCount = 0;
					m_isPinching   = false;
					m_app->OnMouseMove(x, y);
					m_app->OnMouseButton(0, 0, 0);
					break;

				case AMOTION_EVENT_ACTION_MOVE:
					if (m_isPinching && pointerCount >= 2)
					{
						// Handle pinch zoom
						float currentDistance = CalculatePinchDistance(event);
						float delta           = currentDistance - m_lastPinchDistance;

						// Convert pinch delta to scroll (positive = zoom in, negative = zoom out)
						// Scale factor to make pinch feel natural
						float scrollAmount = delta * 0.01f;

						if (fabsf(scrollAmount) > 0.001f)
						{
							m_app->OnScroll(0.0, scrollAmount);
						}

						m_lastPinchDistance = currentDistance;
					}
					else if (!m_isPinching && m_pointerCount == 1)
					{
						// Single finger drag for rotation
						m_app->OnMouseMove(x, y);
					}
					break;

				case AMOTION_EVENT_ACTION_CANCEL:
					m_pointerCount = 0;
					m_isPinching   = false;
					m_app->OnMouseButton(0, 0, 0);
					break;

				default:
					break;
			}
			return 1;
		}

		if (eventType == AINPUT_EVENT_TYPE_KEY)
		{
			int32_t keyCode    = AKeyEvent_getKeyCode(event);
			int32_t action     = AKeyEvent_getAction(event);
			int     glfwAction = (action == AKEY_EVENT_ACTION_DOWN) ? 1 : 0;
			m_app->OnKey(keyCode, glfwAction, 0);

			if (keyCode == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_UP)
			{
				m_shouldClose = true;
				return 1;
			}
		}

		return 0;
	}

	ANativeWindow *GetWindow() const
	{
		return m_window;
	}
	app::IApplication *GetApplication() const
	{
		return m_app;
	}

  private:
	android_app       *m_androidApp;
	ANativeWindow     *m_window;
	app::IApplication *m_app;
	bool               m_shouldClose;

	size_t m_pointerCount;
	float  m_lastPinchDistance;
	bool   m_isPinching;
};

static AndroidPlatformAdapter *g_platformAdapter = nullptr;
static HybridSplatRendererApp *g_app             = nullptr;
static app::DeviceManager     *g_deviceManager   = nullptr;
static bool                    g_initialized     = false;
static timer::Timer            g_frameTimer;
static std::string             g_internalDataPath;

/**
 * @brief Extract an asset file to internal storage
 * @param assetManager Android asset manager
 * @param assetPath Path within assets (e.g., "models/flowers_1.ply")
 * @param outputPath Full path to write the extracted file
 * @return true if extraction succeeded
 */
static bool ExtractAssetToFile(AAssetManager *assetManager, const char *assetPath, const char *outputPath)
{
	AAsset *asset = AAssetManager_open(assetManager, assetPath, AASSET_MODE_STREAMING);
	if (!asset)
	{
		LOGE("Failed to open asset: %s", assetPath);
		return false;
	}

	off_t assetSize = AAsset_getLength(asset);
	LOGI("Extracting asset %s (%ld bytes) to %s", assetPath, (long) assetSize, outputPath);

	std::ofstream outFile(outputPath, std::ios::binary);
	if (!outFile.is_open())
	{
		LOGE("Failed to create output file: %s", outputPath);
		AAsset_close(asset);
		return false;
	}

	const size_t      bufferSize = 64 * 1024;
	std::vector<char> buffer(bufferSize);
	int               bytesRead;
	size_t            totalRead = 0;

	while ((bytesRead = AAsset_read(asset, buffer.data(), bufferSize)) > 0)
	{
		outFile.write(buffer.data(), bytesRead);
		totalRead += bytesRead;
	}

	outFile.close();
	AAsset_close(asset);

	LOGI("Extracted %zu bytes to %s", totalRead, outputPath);
	return totalRead == static_cast<size_t>(assetSize);
}

static bool FileExists(const char *path)
{
	struct stat buffer;
	return (stat(path, &buffer) == 0);
}

/**
 * @brief Handle Android app commands
 */
static void HandleAppCmd(android_app *androidApp, int32_t cmd)
{
	switch (cmd)
	{
		case APP_CMD_INIT_WINDOW:
			LOGI("APP_CMD_INIT_WINDOW");
			if (androidApp->window != nullptr && !g_initialized)
			{
				// Store internal data path for file extraction
				g_internalDataPath = androidApp->activity->internalDataPath;
				LOGI("Internal data path: %s", g_internalDataPath.c_str());

				g_platformAdapter = new AndroidPlatformAdapter(androidApp);
				g_platformAdapter->SetWindow(androidApp->window);

				g_app = new HybridSplatRendererApp();

				// Extract PLY file from assets to internal storage if not already extracted
				auto        assetManager = androidApp->activity->assetManager;
				const char *assetPath    = HybridSplatRendererApp::GetDefaultAssetPath();

				// Extract filename from asset path (e.g., "models/flowers_1.ply" -> "flowers_1.ply")
				const char *filename = strrchr(assetPath, '/');
				filename             = filename ? filename + 1 : assetPath;

				std::string plyOutputPath = g_internalDataPath + "/" + filename;

				if (!FileExists(plyOutputPath.c_str()))
				{
					if (ExtractAssetToFile(assetManager, assetPath, plyOutputPath.c_str()))
					{
						LOGI("PLY file extracted successfully");
					}
					else
					{
						LOGW("Failed to extract PLY file, will use test data");
						plyOutputPath.clear();
					}
				}
				else
				{
					LOGI("PLY file already exists at %s", plyOutputPath.c_str());
				}

				if (!plyOutputPath.empty())
				{
					g_app->SetSplatPath(plyOutputPath.c_str());
				}

				g_deviceManager = new app::DeviceManager(g_app);

				// Create Android asset VFS for shader loading
				auto assetVfs = container::make_shared<vfs::AndroidAssetFileSystem>(assetManager);
				auto rootVfs  = container::make_shared<vfs::RootFileSystem>();
				rootVfs->mount("/", assetVfs);
				g_deviceManager->SetVFS(rootVfs);
				LOGI("Android VFS initialized with asset manager");

				// Initialize with our adapter
				if (g_deviceManager->InitWithAdapter(g_platformAdapter))
				{
					g_initialized = true;
					g_frameTimer.start();
					LOGI("Application initialized successfully");
				}
				else
				{
					LOGE("Failed to initialize application");
					delete g_deviceManager;
					delete g_app;
					delete g_platformAdapter;
					g_deviceManager   = nullptr;
					g_app             = nullptr;
					g_platformAdapter = nullptr;
				}
			}
			break;

		case APP_CMD_TERM_WINDOW:
			LOGI("APP_CMD_TERM_WINDOW");
			if (g_initialized)
			{
				g_deviceManager->Shutdown();
				delete g_deviceManager;
				delete g_app;
				delete g_platformAdapter;
				g_deviceManager   = nullptr;
				g_app             = nullptr;
				g_platformAdapter = nullptr;
				g_initialized     = false;
			}
			break;

		case APP_CMD_WINDOW_RESIZED:
			LOGI("APP_CMD_WINDOW_RESIZED");
			if (g_initialized && g_deviceManager && androidApp->window)
			{
				int width  = ANativeWindow_getWidth(androidApp->window);
				int height = ANativeWindow_getHeight(androidApp->window);
				g_deviceManager->HandleResize(width, height);
			}
			break;

		case APP_CMD_GAINED_FOCUS:
			LOGI("APP_CMD_GAINED_FOCUS");
			break;

		case APP_CMD_LOST_FOCUS:
			LOGI("APP_CMD_LOST_FOCUS");
			break;

		case APP_CMD_PAUSE:
			LOGI("APP_CMD_PAUSE");
			break;

		case APP_CMD_RESUME:
			LOGI("APP_CMD_RESUME");
			break;

		case APP_CMD_SAVE_STATE:
			LOGI("APP_CMD_SAVE_STATE");
			break;

		case APP_CMD_DESTROY:
			LOGI("APP_CMD_DESTROY");
			break;
	}
}

/**
 * @brief Handle input events
 */
static int32_t HandleInputEvent(android_app *androidApp, AInputEvent *event)
{
	(void) androidApp;
	if (g_platformAdapter)
	{
		return g_platformAdapter->HandleInputEvent(event);
	}
	return 0;
}

/**
 * @brief Android main entry point for NativeActivity applications
 */
void android_main(struct android_app *androidApp)
{
	LOGI("android_main started");

	androidApp->onAppCmd     = HandleAppCmd;
	androidApp->onInputEvent = HandleInputEvent;

	// Main loop
	while (true)
	{
		int                         events;
		struct android_poll_source *source;

		// Poll events. Block if not initialized (waiting for window).
		while (ALooper_pollOnce(g_initialized ? 0 : -1, nullptr, &events, (void **) &source) >= 0)
		{
			if (source != nullptr)
			{
				source->process(androidApp, source);
			}

			if (androidApp->destroyRequested != 0)
			{
				LOGI("Destroy requested, exiting");
				if (g_initialized)
				{
					g_deviceManager->Shutdown();
					delete g_deviceManager;
					delete g_app;
					delete g_platformAdapter;
					g_deviceManager   = nullptr;
					g_app             = nullptr;
					g_platformAdapter = nullptr;
					g_initialized     = false;
				}
				return;
			}
		}

		// Render frame if initialized
		if (g_initialized && g_deviceManager)
		{
			g_frameTimer.stop();
			float deltaTime = static_cast<float>(g_frameTimer.elapsedSeconds());
			g_frameTimer.start();

			g_deviceManager->RenderFrame(deltaTime);
		}
	}
}
