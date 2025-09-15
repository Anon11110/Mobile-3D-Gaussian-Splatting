#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/containers/vector.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "engine/shader_factory.h"

namespace rhi
{
class IRHIBuffer;
class IRHIShader;
class IRHIDescriptorSetLayout;
class IRHIDescriptorSet;
class IRHIPipeline;
class IRHISemaphore;
class IRHIFence;
class IRHICommandList;
}        // namespace rhi

namespace msplat
{
namespace app
{
class DeviceManager;
}        // namespace app
}        // namespace msplat

// Use msplat namespace to avoid repetitive prefixes
using namespace msplat;

/**
 * @class TriangleApp
 * @brief A simple triangle rendering application.
 *
 * This application demonstrates basic Vulkan rendering with an animated triangle.
 */
class TriangleApp : public app::IApplication
{
  public:
	TriangleApp()           = default;
	~TriangleApp() override = default;

	// IApplication interface
	bool OnInit(app::DeviceManager *deviceManager) override;
	void OnUpdate(float deltaTime) override;
	void OnRender() override;
	void OnShutdown() override;
	void OnKey(int key, int action, int mods) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnMouseMove(double xpos, double ypos) override;

  private:
	// Vertex structure
	struct Vertex
	{
		math::vec3 position;
		math::vec3 color;
	};

	// Uniform buffer structure
	struct UniformBufferObject
	{
		math::mat4 mvp;
		float      time;
		float      padding[3];
	};

	// Device manager reference
	app::DeviceManager *m_deviceManager = nullptr;

	// Camera
	app::Camera m_camera;

	// Resources
	container::unique_ptr<rhi::IRHIBuffer>              m_vertexBuffer;
	container::unique_ptr<rhi::IRHIBuffer>              m_uniformBuffer;
	container::unique_ptr<engine::ShaderFactory>        m_shaderFactory;
	container::shared_ptr<rhi::IRHIShader>              m_vertexShader;
	container::shared_ptr<rhi::IRHIShader>              m_fragmentShader;
	container::unique_ptr<rhi::IRHIDescriptorSetLayout> m_descriptorSetLayout;
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_descriptorSet;
	container::unique_ptr<rhi::IRHIPipeline>            m_pipeline;

	// Synchronization objects (per swapchain image)
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_imageAvailableSemaphores;
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_renderFinishedSemaphores;
	container::unique_ptr<rhi::IRHIFence>                        m_inFlightFence;

	// Command lists (per swapchain image)
	container::vector<container::unique_ptr<rhi::IRHICommandList>> m_commandLists;

	// Swapchain state tracking
	container::vector<bool> m_imageFirstUse;

	// Application state
	timer::Timer      m_applicationTimer;
	timer::FPSCounter m_fpsCounter;
	void             *m_uniformDataPtr = nullptr;        // Persistently mapped uniform buffer
};