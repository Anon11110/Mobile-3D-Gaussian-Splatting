#include "app/camera.h"
#include <cstring>

#include <GLFW/glfw3.h>

namespace msplat::app {

Camera::Camera()
    : m_position(0.0f, 0.0f, 6.0f),
      m_worldUp(0.0f, 1.0f, 0.0f),
      m_yaw(-90.0f),
      m_pitch(0.0f),
      m_movementSpeed(5.0f),
      m_mouseSensitivity(0.1f),
      m_lastMouseX(0.0),
      m_lastMouseY(0.0),
      m_mouseButtonPressed(false),
      m_firstMouse(true)
{
	// Initialize key states
	std::memset(m_keysPressed, 0, sizeof(m_keysPressed));

	// Calculate initial camera vectors
	UpdateCameraVectors();
	UpdateViewMatrix();

	// Set default perspective projection
	SetPerspectiveProjection(45.0f, 1.0f, 0.1f, 100.0f);
}

void Camera::Update(float deltaTime, GLFWwindow* window)
{
	float velocity = m_movementSpeed * deltaTime;

	// Update position based on key states (for smooth movement)
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		m_position += m_front * velocity;
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		m_position -= m_front * velocity;
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		m_position -= m_right * velocity;
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		m_position += m_right * velocity;
	if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
		m_position -= m_worldUp * velocity;
	if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
		m_position += m_worldUp * velocity;

	UpdateViewMatrix();
}

void Camera::SetPosition(const math::vec3& position)
{
	m_position = position;
	UpdateViewMatrix();
}

void Camera::SetTarget(const math::vec3& target)
{
	m_front = math::Normalize(target - m_position);

	// Calculate yaw and pitch from the front vector
	m_pitch = math::Degrees(math::Asin(m_front.y));
	m_yaw = math::Degrees(math::Atan2(m_front.z, m_front.x));

	UpdateCameraVectors();
	UpdateViewMatrix();
}

void Camera::SetPerspectiveProjection(float fov, float aspect, float nearPlane, float farPlane)
{
	m_projectionMatrix = math::Perspective(math::Radians(fov), aspect, nearPlane, farPlane);
	// Flip Y for Vulkan's coordinate system
	m_projectionMatrix[1][1] *= -1;
}

void Camera::SetOrthographicProjection(float left, float right, float bottom, float top,
                                        float nearPlane, float farPlane)
{
	m_projectionMatrix = math::Ortho(left, right, bottom, top, nearPlane, farPlane);
	// Flip Y for Vulkan's coordinate system
	m_projectionMatrix[1][1] *= -1;
}

void Camera::OnMouseMove(double xpos, double ypos)
{
	if (m_firstMouse)
	{
		m_lastMouseX = xpos;
		m_lastMouseY = ypos;
		m_firstMouse = false;
	}

	if (!m_mouseButtonPressed)
	{
		m_lastMouseX = xpos;
		m_lastMouseY = ypos;
		return;
	}

	float xoffset = static_cast<float>(xpos - m_lastMouseX);
	float yoffset = static_cast<float>(m_lastMouseY - ypos); // Reversed since y-coordinates go from bottom to top

	m_lastMouseX = xpos;
	m_lastMouseY = ypos;

	xoffset *= m_mouseSensitivity;
	yoffset *= m_mouseSensitivity;

	m_yaw += xoffset;
	m_pitch += yoffset;

	// Constrain pitch to avoid flipping
	m_pitch = math::Clamp(m_pitch, -89.0f, 89.0f);

	UpdateCameraVectors();
	UpdateViewMatrix();
}

void Camera::OnMouseButton(int button, int action, int mods)
{
	// Use right mouse button for camera control
	if (button == GLFW_MOUSE_BUTTON_RIGHT)
	{
		if (action == GLFW_PRESS)
		{
			m_mouseButtonPressed = true;
			m_firstMouse = true;
		}
		else if (action == GLFW_RELEASE)
		{
			m_mouseButtonPressed = false;
		}
	}
}

void Camera::OnKey(int key, int action, int mods)
{
	// Track key states for smooth movement
	if (key >= 0 && key < 512)
	{
		if (action == GLFW_PRESS)
			m_keysPressed[key] = true;
		else if (action == GLFW_RELEASE)
			m_keysPressed[key] = false;
	}
}

void Camera::UpdateCameraVectors()
{
	// Calculate the new front vector
	math::vec3 front;
	front.x = math::Cos(math::Radians(m_yaw)) * math::Cos(math::Radians(m_pitch));
	front.y = math::Sin(math::Radians(m_pitch));
	front.z = math::Sin(math::Radians(m_yaw)) * math::Cos(math::Radians(m_pitch));
	m_front = math::Normalize(front);

	// Re-calculate the right and up vectors
	m_right = math::Normalize(math::Cross(m_front, m_worldUp));
	m_up = math::Normalize(math::Cross(m_right, m_front));
}

void Camera::UpdateViewMatrix()
{
	m_viewMatrix = math::LookAt(m_position, m_position + m_front, m_up);
}

} // namespace msplat::app