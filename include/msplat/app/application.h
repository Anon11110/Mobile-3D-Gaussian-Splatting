#pragma once

namespace msplat::app
{

class DeviceManager;

/**
 * @class IApplication
 * @brief An interface for a demo application.
 *
 * This abstract base class defines the structure of a demo application.
 * Each demo in the examples/ directory will implement this interface.
 */
class IApplication
{
  public:
	virtual ~IApplication() = default;

	/**
	 * @brief Called once when the application starts.
	 * @param deviceManager A pointer to the DeviceManager instance.
	 * @return True if initialization is successful, false otherwise.
	 */
	virtual bool OnInit(DeviceManager *deviceManager) = 0;

	/**
	 * @brief Called every frame to update the application logic.
	 * @param deltaTime The time elapsed since the last frame.
	 */
	virtual void OnUpdate(float deltaTime) = 0;

	/**
	 * @brief Called every frame to render the scene.
	 */
	virtual void OnRender() = 0;

	/**
	 * @brief Called when the application is shutting down.
	 */
	virtual void OnShutdown() = 0;

	/**
	 * @brief Handles keyboard input events.
	 * @param key The keyboard key that was pressed or released.
	 * @param action The key action (e.g., press, release, repeat).
	 * @param mods Key modifiers (e.g., shift, ctrl, alt).
	 */
	virtual void OnKey(int key, int action, int mods) = 0;

	/**
	 * @brief Handles mouse button input events.
	 * @param button The mouse button that was pressed or released.
	 * @param action The button action (e.g., press, release).
	 * @param mods Key modifiers.
	 */
	virtual void OnMouseButton(int button, int action, int mods) = 0;

	/**
	 * @brief Handles mouse movement events.
	 * @param xpos The new x-coordinate of the mouse cursor.
	 * @param ypos The new y-coordinate of the mouse cursor.
	 */
	virtual void OnMouseMove(double xpos, double ypos) = 0;

	/**
	 * @brief Handles framebuffer resize events.
	 * @param width The new framebuffer width.
	 * @param height The new framebuffer height.
	 *
	 * Default implementation does nothing. Override to handle resize events.
	 */
	virtual void OnFramebufferResize(int width, int height)
	{
		(void) width;
		(void) height;
	}
};

}        // namespace msplat::app