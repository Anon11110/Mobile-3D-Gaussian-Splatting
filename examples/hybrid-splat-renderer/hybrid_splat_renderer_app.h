#pragma once

#include "app/application.h"
#include "app/camera.h"
#include "core/containers/memory.h"
#include "core/containers/string.h"
#include "core/containers/unordered_map.h"
#include "core/containers/vector.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "engine/engine_config.h"
#include "engine/splat/splat_mesh.h"
#include "shaders/shaderio.h"
#include <array>
#include <cstdlib>
#include <deque>
#include <imgui.h>
#include <imgui_impl_vulkan.h>
#include <optional>
#if defined(__ANDROID__)
#	include <android/input.h>
#	include <android/native_window.h>
#	include <imgui_impl_android.h>
#else
#	include <imgui_impl_glfw.h>
#endif
#include "buffer_memory_tracker.h"
#include <msplat/engine/rendering/compute_splat_rasterizer.h>
#include <msplat/engine/rendering/shader_factory.h>
#include <msplat/engine/scene/scene.h>

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
class IRHIQueryPool;
}        // namespace rhi

namespace msplat
{
namespace app
{
class DeviceManager;
}        // namespace app

namespace engine
{
class ISplatSortBackend;
}        // namespace engine
}        // namespace msplat

using namespace msplat;

// Backend type for runtime switching
enum class BackendType
{
	GPU = 0,
	CPU = 1
};

// Rendering pipeline type
enum class RasterizationPipelineType
{
	HardwareRaster = 0,        // Traditional vertex+fragment pipeline
	ComputeRaster  = 1         // Tile-based compute rasterization
};

// Pending frame-boundary operations
// These operations are deferred to the start of OnRender() to ensure they execute
// at a safe point in the frame lifecycle (after fence wait, before rendering).
struct PendingOperations
{
	struct BackendSwitch
	{
		BackendType targetBackend;
	};
	struct SortingToggle
	{
		bool enable;
	};
	struct AsyncComputeToggle
	{
		bool enable;
	};
	struct TransmittanceCullingToggle
	{
		bool enable;
	};
	struct ModelLoad
	{
		container::string path;
	};
	struct MeshRemoval
	{
		engine::SplatMesh::ID meshId;
	};

	std::optional<BackendSwitch>              backendSwitch;
	std::optional<SortingToggle>              sortingToggle;
	std::optional<AsyncComputeToggle>         asyncComputeToggle;
	std::optional<TransmittanceCullingToggle> transmittanceCullingToggle;
	std::optional<ModelLoad>                  modelLoad;
	std::optional<MeshRemoval>                meshRemoval;

	bool HasPending() const
	{
		return backendSwitch || sortingToggle || asyncComputeToggle || transmittanceCullingToggle || modelLoad || meshRemoval;
	}

	void Clear()
	{
		backendSwitch.reset();
		sortingToggle.reset();
		asyncComputeToggle.reset();
		transmittanceCullingToggle.reset();
		modelLoad.reset();
		meshRemoval.reset();
	}
};

// Hybrid Splat Renderer Application
// Supports runtime switching between CPU and GPU sorting backends
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

	void SetImGuiEnabled(bool enabled)
	{
		m_imguiEnabled = enabled;
	}

	bool IsImGuiEnabled() const
	{
		return m_imguiEnabled;
	}

	bool RequiresDisabledPreRotation() const override
	{
#if defined(__ANDROID__)
		// On Android, disable pre-rotation only when ImGui is enabled
		// since ImGui doesn't support pre-rotated rendering
		return m_imguiEnabled;
#else
		return false;
#endif
	}

	static constexpr const char *GetDefaultAssetPath()
	{
#if defined(__ANDROID__)
		return "models/flowers_1.ply";        // Path inside APK assets
#else
		return "assets/flowers_1.ply";        // Desktop path
#endif
	}

  private:
	void LoadSplatFile(const char *filepath);
	void CreateTestSplatData();

	// Dynamic model management
	void OnSceneBuffersChanged(const engine::Scene::GpuData &gpuData, uint32_t newSplatCount);
	void RebindSceneDescriptors(uint32_t newSplatCount);
	void ReinitializeSortBackend(uint32_t newSplatCount);

	app::DeviceManager *m_deviceManager = nullptr;

	app::Camera m_camera;

	container::unique_ptr<engine::Scene>             m_scene;
	container::unique_ptr<engine::ISplatSortBackend> m_backend;
	BackendType                                      m_currentBackendType        = BackendType::GPU;
	RasterizationPipelineType                        m_rasterizationPipelineType = RasterizationPipelineType::HardwareRaster;

	// Compute rasterizer
	container::unique_ptr<engine::ComputeSplatRasterizer> m_computeRasterizer;
	int                                                   m_currentSortMethod      = 0;        // 0=Prescan, 1=IntegratedScan
	int                                                   m_currentShaderVariant   = 0;        // 0=Portable, 1=SubgroupOptimized
	uint32_t                                              m_transmittanceStatsMode = 0;        // 0=off, 1=stats only, 2=stats+heatmap

	void SwitchBackend(BackendType newType);
	void ProcessPendingOperations();
	void ResetAsyncPipelineState();
	void RecreateSHDependentPipelines();

	container::unique_ptr<engine::ShaderFactory> m_shaderFactory;

	static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = engine::k_maxFramesInFlight;

	rhi::BufferHandle                                   m_quadIndexBuffer;
	std::array<rhi::BufferHandle, MAX_FRAMES_IN_FLIGHT> m_frameUboBuffers;
	std::array<void *, MAX_FRAMES_IN_FLIGHT>            m_frameUboDataPtrs = {};
	rhi::BufferHandle                                   m_sortedIndices;

	rhi::ShaderHandle   m_vertexShader;
	rhi::ShaderHandle   m_fragmentShader;
	rhi::PipelineHandle m_renderPipeline;

	// Splat precompute resources
	bool                                                       m_splatPrecomputeEnabled     = true;
	uint32_t                                                   m_splatPrecomputeAsyncWarmup = 0;        // Frames since precompute enabled on async compute
	std::array<rhi::BufferHandle, MAX_FRAMES_IN_FLIGHT>        m_splatPrecomputeBuffers;
	rhi::ShaderHandle                                          m_splatPrecomputeShader;
	rhi::PipelineHandle                                        m_splatPrecomputePipeline;
	rhi::DescriptorSetLayoutHandle                             m_splatPrecomputeDescriptorLayout;
	std::array<rhi::DescriptorSetHandle, MAX_FRAMES_IN_FLIGHT> m_splatPrecomputeDescriptorSets;
	rhi::ShaderHandle                                          m_vertexShaderPreprocessed;
	rhi::PipelineHandle                                        m_renderPipelinePreprocessed;
	rhi::PipelineHandle                                        m_splatTransmCullingPipelinePreprocessed;
	rhi::DescriptorSetLayoutHandle                             m_descriptorSetLayoutPreprocessed;
	std::array<rhi::DescriptorSetHandle, MAX_FRAMES_IN_FLIGHT> m_descriptorSetsPreprocessed;

	// GPU-driven indirect dispatch resources
	std::array<rhi::BufferHandle, MAX_FRAMES_IN_FLIGHT> m_atomicCounterBuffers;
	std::array<rhi::BufferHandle, MAX_FRAMES_IN_FLIGHT> m_indirectArgsBuffers;

	// Transmittance culling resources
	bool                m_transmittanceCullingEnabled = false;
	uint32_t            m_chunkCount                  = 4;
	float               m_saturationThreshold         = 0.01f;
	rhi::ShaderHandle   m_transmCullingFragmentShader;
	rhi::ShaderHandle   m_stencilUpdateFragmentShader;
	rhi::PipelineHandle m_stencilUpdatePipeline;
	rhi::PipelineHandle m_splatTransmCullingPipeline;

	// Transmittance culling render targets: per-frame for multi-frame-in-flight
	std::array<rhi::TextureHandle, MAX_FRAMES_IN_FLIGHT>       m_accumTextures;
	std::array<rhi::TextureViewHandle, MAX_FRAMES_IN_FLIGHT>   m_accumTextureViews;
	std::array<rhi::TextureHandle, MAX_FRAMES_IN_FLIGHT>       m_depthStencilTextures;
	std::array<rhi::TextureViewHandle, MAX_FRAMES_IN_FLIGHT>   m_depthStencilViews;
	rhi::DescriptorSetLayoutHandle                             m_stencilUpdateDescriptorLayout;
	std::array<rhi::DescriptorSetHandle, MAX_FRAMES_IN_FLIGHT> m_stencilUpdateDescriptorSets;

	// Composite pass resources
	rhi::ShaderHandle                                          m_fullscreenVertexShader;
	rhi::ShaderHandle                                          m_compositeFragmentShader;
	rhi::PipelineHandle                                        m_compositePipeline;
	rhi::DescriptorSetLayoutHandle                             m_compositeDescriptorSetLayout;
	std::array<rhi::DescriptorSetHandle, MAX_FRAMES_IN_FLIGHT> m_compositeDescriptorSets;
	rhi::SamplerHandle                                         m_compositeSampler;

	rhi::DescriptorSetLayoutHandle                             m_descriptorSetLayout;
	std::array<rhi::DescriptorSetHandle, MAX_FRAMES_IN_FLIGHT> m_descriptorSets;

	std::array<rhi::SemaphoreHandle, MAX_FRAMES_IN_FLIGHT>   m_imageAvailableSemaphores;
	container::vector<rhi::SemaphoreHandle>                  m_renderFinishedSemaphores;
	std::array<rhi::FenceHandle, MAX_FRAMES_IN_FLIGHT>       m_inFlightFences;
	std::array<rhi::CommandListHandle, MAX_FRAMES_IN_FLIGHT> m_commandLists;

	// Async compute resources
	std::array<rhi::CommandListHandle, MAX_FRAMES_IN_FLIGHT> m_asyncComputeCmdLists;
	std::array<rhi::FenceHandle, MAX_FRAMES_IN_FLIGHT>       m_asyncComputeFences;
	std::array<rhi::SemaphoreHandle, MAX_FRAMES_IN_FLIGHT>   m_asyncComputeSemaphores;
	std::array<rhi::SemaphoreHandle, MAX_FRAMES_IN_FLIGHT>   m_graphicsToComputeSemaphores;        // QFOT sync
	rhi::CommandListHandle                                   m_asyncCleanupCmdList;                // For QFOT cleanup on toggle async compute only
	uint32_t                                                 m_asyncPipelineFrameIndex = 0;
	bool                                                     m_asyncWarmupComplete     = false;

	timer::Timer      m_applicationTimer;
	timer::FPSCounter m_fpsCounter;
	bool              m_sortingEnabled              = true;
	bool              m_asyncComputeEnabled         = false;
	bool              m_verifyNextSort              = false;
	bool              m_checkVerificationResults    = false;
	bool              m_useSimpleVerification       = true;
	bool              m_vsyncBypassMode             = false;        // Skip present (no vsync)
	bool              m_framebufferResized          = false;        // Window resize detected
	bool              m_crossBackendVerifyEnabled   = false;        // Cross-backend verification mode
	bool              m_crossBackendVerifyRequested = false;        // Run verification on next frame
	container::string m_crossBackendVerifyResult;                   // Last verification result
	PendingOperations m_pendingOps;                                 // Deferred frame-boundary operations
	uint32_t          m_frameCount   = 0;
	uint32_t          m_currentFrame = 0;        // Index into per-frame-in-flight arrays

	uint32_t m_currentShDegree = 3;        // Current SH degree for pipeline specialization

	container::vector<math::vec3> m_testSplatPositions;

	container::string m_splatPath;

	// Predefined models for easy selection
	struct PredefinedModel
	{
		const char *name;
		const char *path;
	};
#if defined(__ANDROID__)
	static constexpr PredefinedModel k_predefinedModels[] = {
	    {"Flowers", "models/flowers_1.ply"},
	    {"Train (30K)", "models/train_30000.ply"},
	    {"Garden (7K)", "models/garden-7k.splat"},
	    {"Bicycle (7K)", "models/bicycle_7000.ply"},
	    {"Kitchen (7K)", "models/kitchen-7k.splat"},
	};
#else
	static constexpr PredefinedModel k_predefinedModels[] = {
	    {"Flowers", "assets/flowers_1.ply"},
	    {"Train (30K)", "assets/train_30000.ply"},
	    {"Garden (7K)", "assets/garden-7k.splat"},
	    {"Bicycle (7K)", "assets/bicycle_7000.ply"},
	    {"Kitchen (7K)", "assets/kitchen-7k.splat"},
	};
#endif
	static constexpr int k_predefinedModelCount = sizeof(k_predefinedModels) / sizeof(k_predefinedModels[0]);

	// Dynamic model management state
	container::vector<engine::SplatMesh::ID> m_loadedMeshIds;
	int                                      m_selectedModelIndex = 0;

	// Per-mesh transform state for ImGui controls
	struct MeshTransformState
	{
		math::vec3 position = {0.0f, 0.0f, 0.0f};
		math::vec3 rotation = {0.0f, 0.0f, 0.0f};        // Euler angles in degrees
		float      scale    = 1.0f;
	};
	container::unordered_map<engine::SplatMesh::ID, MeshTransformState> m_meshTransforms;

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

	// ImGui state
	void *m_imguiDescriptorPool = nullptr;
	bool  m_imguiEnabled        = true;        // Whether ImGui is initialized (set before OnInit)
	bool  m_showImGui           = true;        // Whether ImGui is visible (runtime toggle)

	// FPS history for graph
	static constexpr size_t             FPS_HISTORY_SIZE  = 120;
	std::array<float, FPS_HISTORY_SIZE> m_fpsHistory      = {};
	size_t                              m_fpsHistoryIndex = 0;

	// Profiling infrastructure (GPU timing + memory, disabled by default)
	bool m_profilingEnabled         = false;
	bool m_profilingJustEnabled     = false;        // Skip queries for rest of frame when enabled mid-frame
	bool m_pipelineStatsQueryActive = false;

	// Timestamp indices within a frame
	// Hardware rasterization pipeline: indices 0-5
	// Compute rasterization pipeline: indices 6-15
	static constexpr uint32_t TIMESTAMP_HW_PRECOMPUTE_BEGIN      = 0;
	static constexpr uint32_t TIMESTAMP_HW_PRECOMPUTE_END        = 1;
	static constexpr uint32_t TIMESTAMP_HW_SORT_BEGIN            = 2;
	static constexpr uint32_t TIMESTAMP_HW_SORT_END              = 3;
	static constexpr uint32_t TIMESTAMP_HW_RENDER_BEGIN          = 4;
	static constexpr uint32_t TIMESTAMP_HW_RENDER_END            = 5;
	static constexpr uint32_t TIMESTAMP_COMPUTE_PREPROCESS_BEGIN = 6;
	static constexpr uint32_t TIMESTAMP_COMPUTE_PREPROCESS_END   = 7;
	static constexpr uint32_t TIMESTAMP_COMPUTE_SORT_BEGIN       = 8;
	static constexpr uint32_t TIMESTAMP_COMPUTE_SORT_END         = 9;
	static constexpr uint32_t TIMESTAMP_COMPUTE_RANGES_BEGIN     = 10;
	static constexpr uint32_t TIMESTAMP_COMPUTE_RANGES_END       = 11;
	static constexpr uint32_t TIMESTAMP_COMPUTE_RASTER_BEGIN     = 12;
	static constexpr uint32_t TIMESTAMP_COMPUTE_RASTER_END       = 13;
	static constexpr uint32_t TIMESTAMP_COMPUTE_RENDER_BEGIN     = 14;        // Total compute rendering time
	static constexpr uint32_t TIMESTAMP_COMPUTE_RENDER_END       = 15;        // Total compute rendering time
	static constexpr uint32_t TIMESTAMPS_PER_FRAME               = 16;

	uint32_t                               m_gpuProfilingFrameLatency = 0;        // Set from swapchain image count
	rhi::QueryPoolHandle                   m_timestampQueryPool;
	rhi::QueryPoolHandle                   m_pipelineStatsQueryPool;
	rhi::QueryPoolHandle                   m_asyncPrecomputeQueryPool;
	double                                 m_timestampPeriod     = 1.0;          // nanoseconds per tick
	uint32_t                               m_profilingFrameIndex = 0;            // Rolling frame index for query slots
	std::vector<RasterizationPipelineType> m_frameRasterizationPipelines;        // Track which pipeline was used per frame slot
	std::vector<bool>                      m_frameSortAsyncEnabled;              // Track if async compute was enabled per frame slot
	std::vector<bool>                      m_frameSplatPrecomputeEnabled;        // Track if splat precompute was enabled per frame slot

	double   m_lastAsyncPrecomputeTimeMs       = 0.0;
	uint32_t m_asyncPrecomputeTimingWriteCount = 0;        // Tracks how many slots have valid timestamps

	// Buffered GPU timing results
	struct GpuTimingResults
	{
		// Hardware rasterization pipeline timings
		double   precomputeTimeMs    = 0.0;        // Splat precompute + PrepareIndirectArgs
		double   sortTimeMs          = 0.0;
		double   renderTimeMs        = 0.0;
		uint64_t vertexInvocations   = 0;
		uint64_t fragmentInvocations = 0;

		// Compute rasterization pipeline timings
		double preprocessTimeMs  = 0.0;
		double computeSortTimeMs = 0.0;
		double rangesTimeMs      = 0.0;
		double rasterTimeMs      = 0.0;

		bool valid = false;
	};
	std::deque<GpuTimingResults> m_gpuTimingHistory;
	GpuTimingResults             m_currentGpuTiming;
	GpuTimingResults             m_averagedGpuTiming;

	void InitGpuProfiling();
	void ShutdownGpuProfiling();
	void BeginGpuFrame(rhi::IRHICommandList *cmdList);
	void RecordPrecomputeTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordSortTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordRenderTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordComputePreprocessTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordComputeSortTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordComputeRangesTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void RecordComputeRasterTimestamp(rhi::IRHICommandList *cmdList, bool begin);
	void BeginPipelineStatsQuery(rhi::IRHICommandList *cmdList);
	void EndPipelineStatsQuery(rhi::IRHICommandList *cmdList);
	void ReadGpuTimingResults();
	void ReadAsyncPrecomputeTiming(uint32_t slotIndex);

	// Buffer memory tracking
	BufferMemoryTracker m_memoryTracker;
	void                CollectAndLogBufferMemory();

	void InitImGui();
	void ShutdownImGui();
	void RenderImGui();
	void RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList);
	void UpdateFpsHistory();

#if defined(__ANDROID__)
	// Android-specific ImGui state
	ANativeWindow *m_imguiWindow = nullptr;

  public:
	bool HandleImGuiInput(AInputEvent *event);
	void SetImGuiWindow(ANativeWindow *window)
	{
		m_imguiWindow = window;
	}

  private:
#endif
	void RecreateSwapchain();
	void PerformCrossBackendVerification();

	// Transmittance culling helpers
	void CreateTransmCullingResources();
	void ResizeTransmCullingResources(uint32_t width, uint32_t height);
	void RenderTransmittanceCulling(rhi::IRHICommandList *cmdList, uint32_t splatCount,
	                                uint32_t width, uint32_t height, uint32_t imageIndex,
	                                uint32_t frameIndex, rhi::IRHIBuffer *indirectArgsBuffer = nullptr);
};
