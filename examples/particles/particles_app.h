#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/containers/vector.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "engine/rendering/shader_factory.h"

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

constexpr uint32_t PARTICLE_COUNT       = 1000000;
constexpr uint32_t WORKGROUP_SIZE       = 64;
constexpr int      MAX_FRAMES_IN_FLIGHT = 2;
constexpr float    FIXED_TIMESTEP       = 1.0f / 60.0f;

/**
 * @class ParticlesApp
 * @brief GPU particle simulation application with compute shaders.
 *
 * This application demonstrates GPU-based particle physics simulation
 * using compute shaders and double buffering for optimal performance.
 */
class ParticlesApp : public app::IApplication
{
  public:
	ParticlesApp()           = default;
	~ParticlesApp() override = default;

	// IApplication interface
	bool OnInit(app::DeviceManager *deviceManager) override;
	void OnUpdate(float deltaTime) override;
	void OnRender() override;
	void OnShutdown() override;
	void OnKey(int key, int action, int mods) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnMouseMove(double xpos, double ypos) override;

  private:
	// Particle structure (matches shader layout)
	struct Particle
	{
		math::vec3 position;
		float      padding1;
		math::vec3 velocity;
		float      padding2;
	};

	// Simulation parameters (matches shader layout)
	struct alignas(16) SimulationParams
	{
		float      deltaTime;
		float      time;
		math::vec2 bounds;
		// Emission sphere 1
		math::vec3 sphere1Pos;
		float      sphereRadius;
		math::vec3 sphere1Vel;
		// Emission sphere 2
		math::vec3 sphere2Pos;
		math::vec3 sphere2Vel;
	};

	// UBO for debug sphere rendering
	struct DebugUBO
	{
		math::mat4 mvp;
	};

	// Device manager reference
	app::DeviceManager *m_deviceManager = nullptr;

	// Camera
	app::Camera m_camera;

	// Particle buffers (double buffered for compute)
	rhi::BufferHandle m_particleBufferA;
	rhi::BufferHandle m_particleBufferB;
	rhi::BufferHandle m_paramsBuffer;
	void             *m_paramsDataPtr = nullptr;        // Persistently mapped

	// MVP uniform buffer for rendering
	rhi::BufferHandle m_mvpBuffer;
	void             *m_mvpDataPtr = nullptr;        // Persistently mapped

	// Shader factory and shaders
	container::unique_ptr<engine::ShaderFactory> m_shaderFactory;
	rhi::ShaderHandle                            m_computeShader;
	rhi::ShaderHandle                            m_vertexShader;
	rhi::ShaderHandle                            m_fragmentShader;

	// Compute pipeline resources
	rhi::DescriptorSetLayoutHandle m_computeDescriptorSetLayout;
	rhi::DescriptorSetHandle       m_computeDescriptorSetA;        // A->B
	rhi::DescriptorSetHandle       m_computeDescriptorSetB;        // B->A
	rhi::PipelineHandle            m_computePipeline;

	// Graphics pipeline resources
	rhi::DescriptorSetLayoutHandle m_graphicsDescriptorSetLayout;
	rhi::DescriptorSetHandle       m_graphicsDescriptorSet;
	rhi::PipelineHandle            m_graphicsPipeline;

	// Sphere rendering resources
	rhi::BufferHandle              m_sphereVertexBuffer;
	rhi::BufferHandle              m_sphereIndexBuffer;
	container::vector<uint16_t>    m_sphereIndices;
	rhi::BufferHandle              m_debugUboBuffer1;        // Separate buffer for sphere 1
	rhi::BufferHandle              m_debugUboBuffer2;        // Separate buffer for sphere 2
	void                          *m_debugUboDataPtr1 = nullptr;
	void                          *m_debugUboDataPtr2 = nullptr;
	rhi::ShaderHandle              m_debugVertexShader;
	rhi::ShaderHandle              m_debugFragmentShader;
	rhi::DescriptorSetLayoutHandle m_debugDescriptorSetLayout;
	rhi::DescriptorSetHandle       m_debugDescriptorSet1;        // Descriptor set for sphere 1
	rhi::DescriptorSetHandle       m_debugDescriptorSet2;        // Descriptor set for sphere 2
	rhi::PipelineHandle            m_debugPipeline;

	// Synchronization objects (per frame in flight)
	container::vector<rhi::SemaphoreHandle> m_imageAvailableSemaphores;
	container::vector<rhi::SemaphoreHandle> m_computeFinishedSemaphores;
	container::vector<rhi::SemaphoreHandle> m_graphicsReleasedSemaphores;
	container::vector<rhi::FenceHandle>     m_inFlightFences;

	// Render finished semaphores (per swapchain image)
	container::vector<rhi::SemaphoreHandle> m_renderFinishedSemaphores;

	// Command lists (per frame in flight)
	container::vector<rhi::CommandListHandle> m_computeCommandLists;
	container::vector<rhi::CommandListHandle> m_graphicsPreCommandLists;
	container::vector<rhi::CommandListHandle> m_graphicsCommandLists;

	// Swapchain state tracking
	container::vector<bool> m_imageFirstUse;

	// Application state
	timer::Timer      m_applicationTimer;
	timer::FPSCounter m_fpsCounter;
	float             m_lastTime     = 0.0f;
	float             m_accumulator  = 0.0f;        // Physics accumulator for fixed timestep
	bool              m_useBufferA   = true;        // true = A->B, false = B->A
	uint32_t          m_currentFrame = 0;
	bool              m_firstFrame   = true;        // Track first frame for initial transitions

	// Sphere visualization toggle
	bool m_enableDebugSpheres = false;

	// Emission sphere state (for bouncing simulation)
	math::vec3 m_sphere1Pos   = math::vec3(-2.0f, -0.5f, 0.0f);
	math::vec3 m_sphere1Vel   = math::vec3(1.5f, 0.8f, 1.2f);
	math::vec3 m_sphere2Pos   = math::vec3(2.0f, -0.5f, 0.0f);
	math::vec3 m_sphere2Vel   = math::vec3(-1.2f, 1.0f, -0.9f);
	float      m_sphereRadius = 0.5f;

	// Wall boundaries (adjustable parameters)
	float m_wallBoundsX = 3.2f;        // X-axis boundary (±m_wallBoundsX)
	float m_wallBoundsY = 2.0f;        // Y-axis boundary (±m_wallBoundsY)
	float m_wallBoundsZ = 3.0f;        // Z-axis boundary (±m_wallBoundsZ)
};
