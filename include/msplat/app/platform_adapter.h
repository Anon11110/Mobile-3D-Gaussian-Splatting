#pragma once

#include <cstdint>

namespace msplat::app
{

class IApplication;

/**
 * @brief Platform-agnostic window handle wrapper.
 *
 * This struct provides type-safe handling of platform-specific window handles,
 * allowing the RHI to create appropriate surfaces for each platform.
 */
struct NativeWindowHandle
{
	enum class Type
	{
		Unknown,
		GLFW,                 // Desktop: GLFWwindow*
		AndroidNative,        // Android: ANativeWindow*
		MetalLayer,           // iOS/macOS Metal: CAMetalLayer*
	};

	Type  type   = Type::Unknown;
	void *handle = nullptr;

	NativeWindowHandle() = default;
	NativeWindowHandle(Type t, void *h) :
	    type(t), handle(h)
	{}

	bool IsValid() const
	{
		return type != Type::Unknown && handle != nullptr;
	}

	// Platform-specific factory methods
#if defined(__ANDROID__)
	static NativeWindowHandle FromAndroid(void *window)
	{
		return NativeWindowHandle(Type::AndroidNative, window);
	}
#elif defined(__APPLE__)
	static NativeWindowHandle FromMetalLayer(void *layer)
	{
		return NativeWindowHandle(Type::MetalLayer, layer);
	}
#endif

	// GLFW is available on desktop platforms
#if !defined(__ANDROID__) && !defined(__APPLE__) || (defined(__APPLE__) && !TARGET_OS_IPHONE)
	static NativeWindowHandle FromGLFW(void *window)
	{
		return NativeWindowHandle(Type::GLFW, window);
	}
#endif
};

/**
 * @brief Graphics backend selection.
 */
enum class RHIBackend
{
	Auto,          // Platform default
	Vulkan,        // All platforms (MoltenVK on Apple)
	Metal,         // Apple platforms only (future)
};

/**
 * @brief Interface for platform-specific windowing and input handling.
 *
 * Implementations:
 * - DesktopAdapter: Uses GLFW for Windows/Linux/macOS
 * - AndroidAdapter: Uses NativeActivity and android_native_app_glue
 * - iOSAdapter (future): Uses UIKit
 */
class IPlatformAdapter
{
  public:
	virtual ~IPlatformAdapter() = default;

	virtual bool Initialize(int width, int height, const char *title) = 0;

	virtual void Shutdown() = 0;

	virtual bool ShouldClose() = 0;

	virtual void PollEvents() = 0;

	virtual NativeWindowHandle GetNativeWindowHandle() = 0;

	virtual void GetFramebufferSize(int *width, int *height) = 0;

	/**
	 * @brief Set up input event callbacks.
	 * @param app Application to receive events.
	 */
	virtual void SetEventCallbacks(IApplication *app) = 0;

	/**
	 * @brief Get the preferred RHI backend for this platform.
	 * @return The recommended graphics backend.
	 */
	virtual RHIBackend GetPreferredRHIBackend() const
	{
#if defined(__APPLE__) && TARGET_OS_IPHONE
		return RHIBackend::Metal;        // iOS prefers Metal
#else
		return RHIBackend::Vulkan;        // All other platforms use Vulkan
#endif
	}

	/**
	 * @brief Check if the platform supports the given backend.
	 * @param backend The backend to check.
	 * @return True if supported.
	 */
	virtual bool SupportsBackend(RHIBackend backend) const
	{
		switch (backend)
		{
			case RHIBackend::Vulkan:
				return true;
			case RHIBackend::Metal:
#if defined(__APPLE__)
				return true;
#else
				return false;
#endif
			case RHIBackend::Auto:
				return true;
		}
		return false;
	}
};

/**
 * @brief Factory function to create platform-appropriate adapter.
 *
 * On desktop platforms, this creates a DesktopAdapter (GLFW-based).
 * On Android, the adapter is created differently via AndroidAdapter.
 *
 * @return Unique pointer to platform adapter, or nullptr on failure.
 */
IPlatformAdapter *CreatePlatformAdapter();

}        // namespace msplat::app
