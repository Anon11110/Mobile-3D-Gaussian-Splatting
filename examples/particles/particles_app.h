#pragma once

#include "app/application.h"
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
	bool onInit(app::DeviceManager *deviceManager) override;
	void onUpdate(float deltaTime) override;
	void onRender() override;
	void onShutdown() override;
	void onKey(int key, int action, int mods) override;
	void onMouseButton(int button, int action, int mods) override;
	void onMouseMove(double xpos, double ypos) override;

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

	// Particle buffers (double buffered for compute)
	container::unique_ptr<rhi::IRHIBuffer> m_particleBufferA;
	container::unique_ptr<rhi::IRHIBuffer> m_particleBufferB;
	container::unique_ptr<rhi::IRHIBuffer> m_paramsBuffer;
	void                                  *m_paramsDataPtr = nullptr;        // Persistently mapped

	// MVP uniform buffer for rendering
	container::unique_ptr<rhi::IRHIBuffer> m_mvpBuffer;
	void                                  *m_mvpDataPtr = nullptr;        // Persistently mapped

	// Shader factory and shaders
	container::unique_ptr<engine::ShaderFactory> m_shaderFactory;
	container::shared_ptr<rhi::IRHIShader>       m_computeShader;
	container::shared_ptr<rhi::IRHIShader>       m_vertexShader;
	container::shared_ptr<rhi::IRHIShader>       m_fragmentShader;

	// Compute pipeline resources
	container::unique_ptr<rhi::IRHIDescriptorSetLayout> m_computeDescriptorSetLayout;
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_computeDescriptorSetA;        // A->B
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_computeDescriptorSetB;        // B->A
	container::unique_ptr<rhi::IRHIPipeline>            m_computePipeline;

	// Graphics pipeline resources
	container::unique_ptr<rhi::IRHIDescriptorSetLayout> m_graphicsDescriptorSetLayout;
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_graphicsDescriptorSet;
	container::unique_ptr<rhi::IRHIPipeline>            m_graphicsPipeline;

	// Sphere rendering resources
	container::unique_ptr<rhi::IRHIBuffer>              m_sphereVertexBuffer;
	container::unique_ptr<rhi::IRHIBuffer>              m_sphereIndexBuffer;
	container::vector<uint16_t>                         m_sphereIndices;
	container::unique_ptr<rhi::IRHIBuffer>              m_debugUboBuffer1;        // Separate buffer for sphere 1
	container::unique_ptr<rhi::IRHIBuffer>              m_debugUboBuffer2;        // Separate buffer for sphere 2
	void                                               *m_debugUboDataPtr1 = nullptr;
	void                                               *m_debugUboDataPtr2 = nullptr;
	container::shared_ptr<rhi::IRHIShader>              m_debugVertexShader;
	container::shared_ptr<rhi::IRHIShader>              m_debugFragmentShader;
	container::unique_ptr<rhi::IRHIDescriptorSetLayout> m_debugDescriptorSetLayout;
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_debugDescriptorSet1;        // Descriptor set for sphere 1
	container::unique_ptr<rhi::IRHIDescriptorSet>       m_debugDescriptorSet2;        // Descriptor set for sphere 2
	container::unique_ptr<rhi::IRHIPipeline>            m_debugPipeline;

	// Synchronization objects (per frame in flight)
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_imageAvailableSemaphores;
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_computeFinishedSemaphores;
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_graphicsReleasedSemaphores;
	container::vector<container::unique_ptr<rhi::IRHIFence>>     m_inFlightFences;

	// Render finished semaphores (per swapchain image)
	container::vector<container::unique_ptr<rhi::IRHISemaphore>> m_renderFinishedSemaphores;

	// Command lists (per frame in flight)
	container::vector<container::unique_ptr<rhi::IRHICommandList>> m_computeCommandLists;
	container::vector<container::unique_ptr<rhi::IRHICommandList>> m_graphicsPreCommandLists;
	container::vector<container::unique_ptr<rhi::IRHICommandList>> m_graphicsCommandLists;

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
