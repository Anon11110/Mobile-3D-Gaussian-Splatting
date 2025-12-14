#pragma once

#include "core/math/math.h"

#if !defined(__ANDROID__)
struct GLFWwindow;
#endif

namespace msplat::app
{

/**
 * @class Camera
 * @brief FPS-style camera for 3D scene navigation
 *
 * Provides WASD keyboard movement and mouse-look controls for navigating 3D scenes.
 * Supports both perspective and orthographic projections.
 */
class Camera
{
  public:
	/**
	 * @brief Constructs a camera with default settings
	 */
	Camera();

	/**
	 * @brief Updates camera position based on keyboard input
	 * @param deltaTime Time elapsed since last update
	 * @param window Platform window handle for input polling
	 */
#if !defined(__ANDROID__)
	void Update(float deltaTime, GLFWwindow *window);
#else
	void Update(float deltaTime, void *window);
#endif

	/**
	 * @brief Sets the camera position
	 * @param position New camera position
	 */
	void SetPosition(const math::vec3 &position);

	/**
	 * @brief Sets the camera target (look-at point)
	 * @param target Point to look at
	 */
	void SetTarget(const math::vec3 &target);

	/**
	 * @brief Configures perspective projection
	 * @param fov Field of view in degrees
	 * @param aspect Aspect ratio (width/height)
	 * @param nearPlane Near clipping plane
	 * @param farPlane Far clipping plane
	 */
	void SetPerspectiveProjection(float fov, float aspect, float nearPlane, float farPlane);

	/**
	 * @brief Configures orthographic projection
	 * @param left Left clipping plane
	 * @param right Right clipping plane
	 * @param bottom Bottom clipping plane
	 * @param top Top clipping plane
	 * @param nearPlane Near clipping plane
	 * @param farPlane Far clipping plane
	 */
	void SetOrthographicProjection(float left, float right, float bottom, float top,
	                               float nearPlane, float farPlane);

	/**
	 * @brief Gets the view matrix
	 * @return Current view matrix
	 */
	const math::mat4 &GetViewMatrix() const
	{
		return m_viewMatrix;
	}

	/**
	 * @brief Gets the projection matrix
	 * @return Current projection matrix
	 */
	const math::mat4 &GetProjectionMatrix() const
	{
		return m_projectionMatrix;
	}

	/**
	 * @brief Gets the combined view-projection matrix
	 * @return View * Projection matrix
	 */
	math::mat4 GetViewProjectionMatrix() const
	{
		return m_projectionMatrix * m_viewMatrix;
	}

	/**
	 * @brief Gets the camera position
	 * @return Current camera position
	 */
	const math::vec3 &GetPosition() const
	{
		return m_position;
	}

	/**
	 * @brief Gets the camera forward direction
	 * @return Normalized forward vector
	 */
	const math::vec3 &GetFront() const
	{
		return m_front;
	}

	/**
	 * @brief Gets the camera up direction
	 * @return Normalized up vector
	 */
	const math::vec3 &GetUp() const
	{
		return m_up;
	}

	/**
	 * @brief Gets the camera right direction
	 * @return Normalized right vector
	 */
	const math::vec3 &GetRight() const
	{
		return m_right;
	}

	/**
	 * @brief Handles mouse movement events
	 * @param xpos Mouse x position
	 * @param ypos Mouse y position
	 */
	void OnMouseMove(double xpos, double ypos);

	/**
	 * @brief Handles mouse button events
	 * @param button Mouse button code
	 * @param action Button action (press/release)
	 * @param mods Modifier keys
	 */
	void OnMouseButton(int button, int action, int mods);

	/**
	 * @brief Handles keyboard events
	 * @param key Keyboard key code
	 * @param action Key action (press/release/repeat)
	 * @param mods Modifier keys
	 */
	void OnKey(int key, int action, int mods);

	/**
	 * @brief Sets the camera movement speed
	 * @param speed Units per second
	 */
	void SetMovementSpeed(float speed)
	{
		m_movementSpeed = speed;
	}

	/**
	 * @brief Sets the mouse sensitivity
	 * @param sensitivity Mouse rotation multiplier
	 */
	void SetMouseSensitivity(float sensitivity)
	{
		m_mouseSensitivity = sensitivity;
	}

	/**
	 * @brief Gets the camera movement speed
	 * @return Current movement speed
	 */
	float GetMovementSpeed() const
	{
		return m_movementSpeed;
	}

	/**
	 * @brief Gets the mouse sensitivity
	 * @return Current mouse sensitivity
	 */
	float GetMouseSensitivity() const
	{
		return m_mouseSensitivity;
	}

  private:
	/**
	 * @brief Updates internal direction vectors based on yaw/pitch
	 */
	void UpdateCameraVectors();

	/**
	 * @brief Updates the view matrix based on current position and orientation
	 */
	void UpdateViewMatrix();

	// Camera position and orientation
	math::vec3 m_position;
	math::vec3 m_front;
	math::vec3 m_up;
	math::vec3 m_right;
	math::vec3 m_worldUp;

	// Euler angles
	float m_yaw;
	float m_pitch;

	// Camera options
	float m_movementSpeed;
	float m_mouseSensitivity;

	// Matrices
	math::mat4 m_viewMatrix;
	math::mat4 m_projectionMatrix;

	// Mouse state
	double m_lastMouseX;
	double m_lastMouseY;
	bool   m_mouseButtonPressed;
	bool   m_firstMouse;

	// Keyboard state (for smooth movement)
	bool m_keysPressed[512];        // Track key states for smooth movement
};

}        // namespace msplat::app