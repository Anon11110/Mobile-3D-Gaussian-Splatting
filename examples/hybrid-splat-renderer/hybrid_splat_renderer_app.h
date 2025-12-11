#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/containers/vector.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "shaders/shaderio.h"
#include <array>
#include <cstdlib>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <msplat/engine/shader_factory.h>

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

namespace engine
{
class Scene;
class GpuSplatSorter;
}        // namespace engine
}        // namespace msplat

using namespace msplat;

/// Hybrid Splat Renderer Application
/// Phase 1: GPU sorting baseline (foundation for CPU+GPU hybrid in later phases)
class HybridSplatRendererApp : public app::IApplication
{
  public:
	HybridSplatRendererApp();
	~HybridSplatRendererApp() override;

	HybridSplatRendererApp(HybridSplatRendererApp &&) noexcept;
	HybridSplatRendererApp &operator=(HybridSplatRendererApp &&) noexcept;
	HybridSplatRendererApp(const HybridSplatRendererApp &)            = delete;
	HybridSplatRendererApp &operator=(const HybridSplatRendererApp &) = delete;

	bool OnInit(app::DeviceManager *deviceManager) override;
	void OnUpdate(float deltaTime) override;
	void OnRender() override;
	void OnShutdown() override;
	void OnKey(int key, int action, int mods) override;
	void OnMouseButton(int button, int action, int mods) override;
	void OnMouseMove(double xpos, double ypos) override;

  private:
	void LoadSplatFile(const char *filepath);
	void CreateTestSplatData();

	app::DeviceManager *m_deviceManager = nullptr;

	app::Camera m_camera;

	container::unique_ptr<engine::Scene>          m_scene;
	container::unique_ptr<engine::GpuSplatSorter> m_sorter;

	container::unique_ptr<engine::ShaderFactory> m_shaderFactory;

	rhi::BufferHandle m_quadIndexBuffer;
	rhi::BufferHandle m_frameUboBuffer;
	void             *m_frameUboDataPtr = nullptr;
	rhi::BufferHandle m_sortedIndices;

	rhi::ShaderHandle   m_vertexShader;
	rhi::ShaderHandle   m_fragmentShader;
	rhi::PipelineHandle m_renderPipeline;

	rhi::DescriptorSetLayoutHandle m_descriptorSetLayout;
	rhi::DescriptorSetHandle       m_descriptorSet;

	container::vector<rhi::SemaphoreHandle> m_imageAvailableSemaphores;
	container::vector<rhi::SemaphoreHandle> m_renderFinishedSemaphores;
	rhi::FenceHandle                        m_inFlightFence;

	container::vector<rhi::CommandListHandle> m_commandLists;

	timer::Timer      m_applicationTimer;
	timer::FPSCounter m_fpsCounter;
	bool              m_sortingEnabled           = true;
	bool              m_verifyNextSort           = false;
	bool              m_checkVerificationResults = false;
	bool              m_useSimpleVerification    = true;
	bool              m_benchmarkMode            = false;        // Skip present for benchmarking
	uint32_t          m_frameCount               = 0;

	container::vector<math::vec3> m_testSplatPositions;

	// ImGui state
	void *m_imguiDescriptorPool = nullptr;
	bool  m_showImGui           = true;

	// FPS history for graph
	static constexpr size_t             FPS_HISTORY_SIZE = 120;
	std::array<float, FPS_HISTORY_SIZE> m_fpsHistory     = {};
	size_t                              m_fpsHistoryIndex = 0;

	void InitImGui();
	void ShutdownImGui();
	void RenderImGui();
	void RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList);
	void UpdateFpsHistory();
};
