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
#if !defined(__ANDROID__)
#	include <imgui.h>
#	include <imgui_impl_glfw.h>
#	include <imgui_impl_vulkan.h>
#endif
#include <msplat/engine/rendering/shader_factory.h>

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
class ISplatSortBackend;
}        // namespace engine
}        // namespace msplat

using namespace msplat;

/// Backend type for runtime switching
enum class BackendType
{
	GPU = 0,
	CPU = 1
};

/// Hybrid Splat Renderer Application
/// Supports runtime switching between CPU and GPU sorting backends
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
	void OnScroll(double xoffset, double yoffset) override;
	void OnFramebufferResize(int width, int height) override;

	void SetSplatPath(const char *path)
	{
		m_splatPath = path;
	}

	const container::string &GetSplatPath() const
	{
		return m_splatPath;
	}

	static constexpr const char *GetDefaultAssetPath()
	{
#if defined(__ANDROID__)
		return "models/flowers_1.ply";        // Path inside APK assets
		                                      // return "models/train_7000.ply";
#else
		return "assets/flowers_1.ply";        // Desktop path
		                                      // return "assets/train_7000.ply";
#endif
	}

  private:
	void LoadSplatFile(const char *filepath);
	void CreateTestSplatData();

	app::DeviceManager *m_deviceManager = nullptr;

	app::Camera m_camera;

	container::unique_ptr<engine::Scene>             m_scene;
	container::unique_ptr<engine::ISplatSortBackend> m_backend;
	BackendType                                      m_currentBackendType = BackendType::GPU;

	void SwitchBackend(BackendType newType);

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
	bool              m_sortingEnabled              = true;
	bool              m_verifyNextSort              = false;
	bool              m_checkVerificationResults    = false;
	bool              m_useSimpleVerification       = true;
	bool              m_vsyncBypassMode             = false;        // Skip present (no vsync)
	bool              m_framebufferResized          = false;        // Window resize detected
	bool              m_crossBackendVerifyEnabled   = false;        // Cross-backend verification mode
	bool              m_crossBackendVerifyRequested = false;        // Run verification on next frame
	container::string m_crossBackendVerifyResult;                   // Last verification result
	uint32_t          m_frameCount = 0;

	container::vector<math::vec3> m_testSplatPositions;

	// Splat file path (can be set externally, defaults to assets/splats/flowers_1/point_cloud.ply)
	container::string m_splatPath;

#if defined(__ANDROID__)

	// Touch UI state for Android
	double m_lastTouchX = 0.0;
	double m_lastTouchY = 0.0;
	bool   m_touchDown  = false;

	// Orbit camera state for Android
	math::vec3 m_orbitTarget   = {0.0f, 0.0f, 0.0f};        // Point to orbit around
	float      m_orbitDistance = 5.0f;                      // Distance from target
	float      m_orbitYaw      = 0.0f;                      // Horizontal angle (degrees)
	float      m_orbitPitch    = 0.0f;                      // Vertical angle (degrees)
	float      m_orbitMinDist  = 0.5f;                      // Minimum zoom distance
	float      m_orbitMaxDist  = 100.0f;                    // Maximum zoom distance

	void UpdateOrbitCamera();
#endif

#if !defined(__ANDROID__)
	// ImGui state
	void *m_imguiDescriptorPool = nullptr;
	bool  m_showImGui           = true;

	// FPS history for graph
	static constexpr size_t             FPS_HISTORY_SIZE  = 120;
	std::array<float, FPS_HISTORY_SIZE> m_fpsHistory      = {};
	size_t                              m_fpsHistoryIndex = 0;

	void InitImGui();
	void ShutdownImGui();
	void RenderImGui();
	void RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList);
	void UpdateFpsHistory();
#endif
	void RecreateSwapchain();
	void PerformCrossBackendVerification();
};
