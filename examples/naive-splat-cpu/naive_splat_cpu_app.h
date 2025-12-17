#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/timer.h"
#include "engine/rendering/shader_factory.h"
#include "engine/scene/scene.h"
#include "rhi/rhi.h"
#include "shaders/shaderio.h"

// Use msplat namespace to avoid repetitive prefixes
using namespace msplat;

class NaiveSplatCpuApp : public app::IApplication
{
  public:
	NaiveSplatCpuApp()           = default;
	~NaiveSplatCpuApp() override = default;

	// IApplication interface
	bool OnInit(app::DeviceManager *deviceManager) override;
	void OnUpdate(float deltaTime) override;
	void OnRender() override;
	void OnShutdown() override;
	void OnKey(int key, int action, int mods) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnMouseMove(double xpos, double ypos) override;

  private:
	// Device manager reference
	app::DeviceManager *m_deviceManager = nullptr;

	// Camera
	app::Camera m_camera;

	// Scene and rendering resources
	container::unique_ptr<engine::Scene>         m_scene;
	container::unique_ptr<engine::ShaderFactory> m_shaderFactory;

	// Quad index buffer for indexed procedural rendering
	rhi::BufferHandle m_quadIndexBuffer;

	// Shaders
	rhi::ShaderHandle m_vertexShader;
	rhi::ShaderHandle m_fragmentShader;

	// Pipeline
	rhi::DescriptorSetLayoutHandle m_descriptorSetLayout;
	rhi::DescriptorSetHandle       m_descriptorSet;
	rhi::PipelineHandle            m_pipeline;

	// Uniform buffer for matrices etc.
	rhi::BufferHandle m_frameUboBuffer;
	void             *m_frameUboDataPtr = nullptr;

	// Command lists (one per swapchain image)
	container::vector<rhi::CommandListHandle> m_commandLists;

	// Synchronization
	container::vector<rhi::SemaphoreHandle> m_imageAvailableSemaphores;
	container::vector<rhi::SemaphoreHandle> m_renderFinishedSemaphores;
	container::vector<rhi::FenceHandle>     m_inFlightFences;
	uint32_t                                m_currentFrame = 0;

	// Timers
	timer::Timer      m_applicationTimer;
	timer::FPSCounter m_fpsCounter;
};