#include "hybrid_splat_renderer_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#if !defined(__ANDROID__)
#	include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <msplat/core/profiling/memory_profiler.h>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/splat_sort_backend.h>
#include <msplat/engine/splat/splat_loader.h>
#include <thread>
#include <vulkan/vulkan.h>

#if defined(__ANDROID__)
extern "C" const char *Android_ResolveAssetPath(const char *assetPath);
#endif

HybridSplatRendererApp::HybridSplatRendererApp()
{
	m_fpsHistory.fill(0.0f);
}
HybridSplatRendererApp::~HybridSplatRendererApp()                                             = default;
HybridSplatRendererApp::HybridSplatRendererApp(HybridSplatRendererApp &&) noexcept            = default;
HybridSplatRendererApp &HybridSplatRendererApp::operator=(HybridSplatRendererApp &&) noexcept = default;

bool HybridSplatRendererApp::OnInit(app::DeviceManager *deviceManager)
{
	LOG_INFO("=== HybridSplatRendererApp Initialization ===");

	m_deviceManager = deviceManager;

	rhi::IRHIDevice *device = deviceManager->GetDevice();
	if (!device)
	{
		LOG_ERROR("Failed to get RHI device");
		return false;
	}

	rhi::IRHISwapchain *swapchain = deviceManager->GetSwapchain();

	const auto &vfs = deviceManager->GetVFS();
	m_shaderFactory = container::make_unique<engine::ShaderFactory>(device, vfs);
	m_scene         = container::make_unique<engine::Scene>(device);

	m_scene->SetBufferChangeCallback([this](const engine::Scene::GpuData &gpuData, uint32_t newSplatCount) {
		OnSceneBuffersChanged(gpuData, newSplatCount);
	});

	m_camera.SetPosition(math::vec3(0.0f, 0.0f, 3.0f));
	m_camera.SetTarget(math::vec3(0.0f, 0.0f, 0.0f));

#if defined(__ANDROID__)
	m_orbitTarget   = math::vec3(0.0f, 0.0f, 0.0f);
	m_orbitDistance = math::Length(m_camera.GetPosition() - m_orbitTarget);
#endif

	int width, height;
	deviceManager->GetPlatformAdapter()->GetFramebufferSize(&width, &height);
	float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

	m_camera.SetPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);
	m_camera.SetMovementSpeed(5.0f);
	m_camera.SetMouseSensitivity(0.1f);

	// For comprehensive verification, always use test data
	if (!m_useSimpleVerification)
	{
		LOG_INFO("Comprehensive verification mode enabled - using test data");
		CreateTestSplatData();
	}
	else if (m_splatPath.empty())
	{
#if defined(__ANDROID__)
		// Android path should be set by android_main.cpp after extracting from APK assets
		// If empty here, extraction failed and fall back to test data
		LOG_INFO("No splat path set (extraction may have failed), creating test data...");
		CreateTestSplatData();
#else
		m_splatPath = GetDefaultAssetPath();
#endif
	}

	if (!m_splatPath.empty() && m_useSimpleVerification)
	{
		LOG_INFO("Loading splat file: {}", m_splatPath);
		LoadSplatFile(m_splatPath.c_str());
	}

	// Create app-owned sorted indices buffer
	if (m_scene->GetTotalSplatCount() > 0)
	{
		rhi::BufferDesc indicesDesc{};
		indicesDesc.size  = m_scene->GetTotalSplatCount() * sizeof(uint32_t);
		indicesDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
		m_sortedIndices   = device->CreateBuffer(indicesDesc);

		// Create and initialize GPU backend with CPU fallback
		m_backend            = container::make_unique<engine::GpuSplatSortBackend>();
		m_currentBackendType = BackendType::GPU;

		if (!m_backend->Initialize(device, m_scene.get(), m_sortedIndices, m_scene->GetTotalSplatCount(), vfs))
		{
			LOG_WARNING("GPU backend failed to initialize, attempting CPU fallback");

			// Try CPU backend as fallback
			m_backend = container::make_unique<engine::CpuSplatSortBackend>();
			if (!m_backend->Initialize(device, m_scene.get(), m_sortedIndices, m_scene->GetTotalSplatCount(), vfs))
			{
				LOG_ERROR("CPU backend also failed to initialize - sorting unavailable");
				m_backend.reset();
			}
			else
			{
				m_currentBackendType = BackendType::CPU;
				LOG_INFO("Fell back to CPU backend for {} splats", m_scene->GetTotalSplatCount());
			}
		}
		else
		{
			LOG_INFO("Initialized GPU sort backend for {} splats", m_scene->GetTotalSplatCount());
		}

		// Set test positions for comprehensive verification
		if (!m_useSimpleVerification && !m_testSplatPositions.empty() && m_backend)
		{
			m_backend->SetTestPositions(&m_testSplatPositions);
			LOG_INFO("Test positions set for comprehensive verification ({} positions)", m_testSplatPositions.size());
		}
	}

	// Create quad index buffer for instanced rendering
	// Uses triangle strip topology: 0, 1, 2, 3 forms two triangles
	if (m_scene->GetTotalSplatCount() > 0)
	{
		container::vector<uint32_t> quadIndices = {0, 1, 2, 3};

		rhi::BufferDesc ibDesc{};
		ibDesc.size        = quadIndices.size() * sizeof(uint32_t);
		ibDesc.usage       = rhi::BufferUsage::INDEX;
		ibDesc.indexType   = rhi::IndexType::UINT32;
		ibDesc.initialData = quadIndices.data();
		m_quadIndexBuffer  = device->CreateBuffer(ibDesc);
	}

	// Load splat rendering shaders
	m_vertexShader   = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.vert.spv", rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.frag.spv", rhi::ShaderStage::FRAGMENT);
	LOG_INFO("Render shaders loaded: vertex={}, fragment={}", m_vertexShader != nullptr, m_fragmentShader != nullptr);

	// Create UBO
	rhi::BufferDesc uboDesc{};
	uboDesc.size                      = sizeof(FrameUBO);
	uboDesc.usage                     = rhi::BufferUsage::UNIFORM;
	uboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	uboDesc.hints.persistently_mapped = true;
	m_frameUboBuffer                  = device->CreateBuffer(uboDesc);
	m_frameUboDataPtr                 = m_frameUboBuffer->Map();

	// Create descriptor set layout
	rhi::DescriptorSetLayoutDesc layoutDesc{};
	layoutDesc.bindings = {
	    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::ALL_GRAPHICS},        // UBO
	    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Positions
	    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Covariances3D
	    {3, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Colors
	    {4, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // SH Rest
	    {5, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Sorted Indices
	    {6, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Mesh Indices
	    {7, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},              // Model Matrices
	};
	m_descriptorSetLayout = device->CreateDescriptorSetLayout(layoutDesc);

	// Create descriptor set
	m_descriptorSet = device->CreateDescriptorSet(m_descriptorSetLayout.Get());

	// Binding 0: UBO
	rhi::BufferBinding uboBinding{};
	uboBinding.buffer = m_frameUboBuffer.Get();
	uboBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_descriptorSet->BindBuffer(0, uboBinding);

	// Binding 1: Positions
	const auto &gpuData = m_scene->GetGpuData();
	if (gpuData.positions)
	{
		rhi::BufferBinding positionsBinding{};
		positionsBinding.buffer = gpuData.positions.Get();
		positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(1, positionsBinding);
	}

	// Binding 2: Covariances3D
	if (gpuData.covariances3D)
	{
		rhi::BufferBinding cov3DBinding{};
		cov3DBinding.buffer = gpuData.covariances3D.Get();
		cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(2, cov3DBinding);
	}

	// Binding 3: Colors
	if (gpuData.colors)
	{
		rhi::BufferBinding colorsBinding{};
		colorsBinding.buffer = gpuData.colors.Get();
		colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(3, colorsBinding);
	}

	// Binding 4: SH Rest
	if (gpuData.shRest)
	{
		rhi::BufferBinding shBinding{};
		shBinding.buffer = gpuData.shRest.Get();
		shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(4, shBinding);
	}

	// Binding 5: Sorted indices (app-owned buffer, written to by backend)
	if (m_sortedIndices)
	{
		rhi::BufferBinding indicesBinding{};
		indicesBinding.buffer = m_sortedIndices.Get();
		indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(5, indicesBinding);
	}

	// Binding 6: Per-splat Mesh Indices for model matrix lookup
	if (gpuData.meshIndices)
	{
		rhi::BufferBinding meshIndicesBinding{};
		meshIndicesBinding.buffer = gpuData.meshIndices.Get();
		meshIndicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(6, meshIndicesBinding);
	}

	// Binding 7: Per-mesh Model Matrices
	if (gpuData.modelMatrices)
	{
		rhi::BufferBinding modelMatricesBinding{};
		modelMatricesBinding.buffer = gpuData.modelMatrices.Get();
		modelMatricesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(7, modelMatricesBinding);
	}

	// Create graphics pipeline
	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertexShader                = m_vertexShader.Get();
	pipelineDesc.fragmentShader              = m_fragmentShader.Get();
	pipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_STRIP;
	pipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable         = true;
	pipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].srcAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].dstAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.targetSignature.colorFormats                 = {swapchain->GetBackBuffer(0)->GetFormat()};
	pipelineDesc.descriptorSetLayouts                         = {m_descriptorSetLayout.Get()};
	m_renderPipeline                                          = device->CreateGraphicsPipeline(pipelineDesc);

	uint32_t imageCount = deviceManager->GetSwapchain()->GetImageCount();
	LOG_INFO("Swapchain image count: {}", imageCount);

	m_imageAvailableSemaphores.resize(imageCount);
	m_renderFinishedSemaphores.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		m_imageAvailableSemaphores[i] = device->CreateSemaphore();
		m_renderFinishedSemaphores[i] = device->CreateSemaphore();
	}

	m_inFlightFence = device->CreateFence(true);

	m_commandLists.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		m_commandLists[i] = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	}

	m_applicationTimer.start();
	m_fpsCounter.reset();

	// Initialize GPU profiling
	InitGpuProfiling();

	// Initialize compute rasterizer
	if (m_scene->GetTotalSplatCount() > 0)
	{
		int fbWidth, fbHeight;
		deviceManager->GetPlatformAdapter()->GetFramebufferSize(&fbWidth, &fbHeight);

		m_computeRasterizer = container::make_unique<engine::ComputeSplatRasterizer>(device, vfs);
		m_computeRasterizer->Initialize(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight), m_scene->GetTotalSplatCount());
		LOG_INFO("Compute rasterizer initialized for {} splats ({}x{})", m_scene->GetTotalSplatCount(), fbWidth, fbHeight);
	}

	if (m_imguiEnabled)
	{
		InitImGui();
	}

	// Log initialization summary
	int initWidth, initHeight;
	deviceManager->GetPlatformAdapter()->GetFramebufferSize(&initWidth, &initHeight);
	LOG_INFO("Window size: {}x{}", initWidth, initHeight);
	LOG_INFO("Backend: {} ({})",
	         m_backend ? m_backend->GetName() : "None",
	         m_backend ? m_backend->GetMethodName() : "N/A");
	LOG_INFO("Splats loaded: {}", m_scene ? m_scene->GetTotalSplatCount() : 0);
	LOG_INFO("ImGui: {}", m_imguiEnabled ? "enabled" : "disabled");
#if defined(__ANDROID__)
	LOG_INFO("Pre-rotation: {}", m_imguiEnabled ? "disabled (ImGui compatibility)" : "enabled (better performance)");
#endif
	LOG_INFO("=== Initialization Complete ===");

	// Log initial buffer memory breakdown
	CollectAndLogBufferMemory();

	return true;
}

void HybridSplatRendererApp::SwitchBackend(BackendType newType)
{
	if (newType == m_currentBackendType)
	{
		return;        // Already using this backend
	}

	rhi::IRHIDevice *device = m_deviceManager->GetDevice();

	// Wait for any in-flight GPU work
	device->WaitIdle();

	// Destroy current backend
	m_backend.reset();

	// Create new backend
	if (newType == BackendType::GPU)
	{
		m_backend = container::make_unique<engine::GpuSplatSortBackend>();
	}
	else
	{
		m_backend = container::make_unique<engine::CpuSplatSortBackend>();
	}

	const auto &vfs = m_deviceManager->GetVFS();
	if (!m_backend->Initialize(device, m_scene.get(), m_sortedIndices, m_scene->GetTotalSplatCount(), vfs))
	{
		LOG_ERROR("Failed to initialize {} backend, reverting to previous",
		          newType == BackendType::GPU ? "GPU" : "CPU");

		// Revert to previous backend
		if (newType == BackendType::GPU)
		{
			m_backend = container::make_unique<engine::CpuSplatSortBackend>();
		}
		else
		{
			m_backend = container::make_unique<engine::GpuSplatSortBackend>();
		}
		m_backend->Initialize(device, m_scene.get(), m_sortedIndices, m_scene->GetTotalSplatCount(), vfs);
		return;
	}

	m_backend->SetAsyncCompute(m_asyncComputeEnabled);

	m_currentBackendType = newType;
	LOG_INFO("Switched to {} backend ({})", m_backend->GetName(), m_backend->GetMethodName());
}

void HybridSplatRendererApp::ProcessPendingOperations()
{
	// Backend switch
	if (m_pendingOps.backendSwitch)
	{
		SwitchBackend(m_pendingOps.backendSwitch->targetBackend);
		m_pendingOps.backendSwitch.reset();

		// Reset profiling frame index to avoid reading stale timestamps from the old backend.
		// Different backends write different timestamps, which could cause hangs with WAIT flag.
		if (m_profilingEnabled)
		{
			m_profilingFrameIndex = 0;
			m_currentGpuTiming    = {};
		}
	}

	// Async compute toggle
	if (m_pendingOps.asyncComputeToggle && m_backend)
	{
		m_asyncComputeEnabled = m_pendingOps.asyncComputeToggle->enable;
		m_backend->SetAsyncCompute(m_asyncComputeEnabled);
		m_pendingOps.asyncComputeToggle.reset();

		// Reset profiling frame index to avoid reading stale timestamps from the old mode.
		// When switching modes, the timestamps from N frames ago were written under a different
		// mode, which would cause hangs with WAIT flag.
		if (m_profilingEnabled)
		{
			m_profilingFrameIndex = 0;
			m_currentGpuTiming    = {};
		}

		LOG_INFO("Async compute {}", m_asyncComputeEnabled ? "enabled" : "disabled");
	}

	// Model load
	if (m_pendingOps.modelLoad)
	{
		LOG_INFO("Loading deferred model: {}", m_pendingOps.modelLoad->path);

#if defined(__ANDROID__)
		// Resolve APK asset path to extracted filesystem path
		const char *resolvedPath = Android_ResolveAssetPath(m_pendingOps.modelLoad->path.c_str());
		if (resolvedPath)
		{
			LoadSplatFile(resolvedPath);
		}
		else
		{
			LOG_ERROR("Failed to resolve Android asset path: {}", m_pendingOps.modelLoad->path);
		}
#else
		LoadSplatFile(m_pendingOps.modelLoad->path.c_str());
#endif
		m_pendingOps.modelLoad.reset();
	}

	// Mesh removal
	if (m_pendingOps.meshRemoval)
	{
		engine::SplatMesh::ID meshId = m_pendingOps.meshRemoval->meshId;
		LOG_INFO("Removing deferred mesh: {}", meshId);

		if (m_scene->RemoveMesh(meshId))
		{
			auto it = std::find(m_loadedMeshIds.begin(), m_loadedMeshIds.end(), meshId);
			if (it != m_loadedMeshIds.end())
			{
				if (it != m_loadedMeshIds.end() - 1)
				{
					std::swap(*it, m_loadedMeshIds.back());
				}
				m_loadedMeshIds.pop_back();
			}

			m_meshTransforms.erase(meshId);
		}
		m_pendingOps.meshRemoval.reset();
	}
}

void HybridSplatRendererApp::OnUpdate(float deltaTime)
{
	m_camera.Update(deltaTime, m_deviceManager->GetWindow());
}

void HybridSplatRendererApp::OnRender()
{
	if (!m_deviceManager || !m_scene)
	{
		return;
	}

	rhi::IRHIDevice    *device    = m_deviceManager->GetDevice();
	rhi::IRHISwapchain *swapchain = m_deviceManager->GetSwapchain();

	// Check if window is minimized before waiting on fence
	rhi::IRHITextureView *renderBackbufferView = swapchain->GetBackBufferView(0);
	uint32_t              width                = renderBackbufferView->GetWidth();
	uint32_t              height               = renderBackbufferView->GetHeight();
	if (width == 0 || height == 0)
	{
		return;
	}

	// Wait for previous frame
	m_inFlightFence->Wait(UINT64_MAX);

	m_fpsCounter.frame();

	// Handle framebuffer resize
	if (m_framebufferResized)
	{
		m_framebufferResized = false;
		RecreateSwapchain();
		return;
	}

	// Process deferred operations at frame boundary
	ProcessPendingOperations();

	// Acquire next image (skip in vsync bypass mode)
	uint32_t acquireSemIndex = m_frameCount % m_imageAvailableSemaphores.size();
	uint32_t imageIndex      = 0;

	if (!m_vsyncBypassMode)
	{
		rhi::SwapchainStatus status = swapchain->AcquireNextImage(
		    imageIndex,
		    m_imageAvailableSemaphores[acquireSemIndex].Get());

		if (status == rhi::SwapchainStatus::OUT_OF_DATE ||
		    status == rhi::SwapchainStatus::SUBOPTIMAL)
		{
			RecreateSwapchain();
			return;
		}
	}
	else
	{
		imageIndex = 0;
	}

	// Reset fence AFTER all early returns to prevents blocking forever
	// if we return early above without submitting any commands
	m_inFlightFence->Reset();

	// Query pre-transform from swapchain and build pre-rotation matrix for mobile rotation handling
	rhi::SurfaceTransform preTransform = swapchain->GetPreTransform();

	math::mat4 preRotationMatrix = math::Identity();
	switch (preTransform)
	{
		case rhi::SurfaceTransform::ROTATE_90:
			preRotationMatrix = math::RotateZ(math::HALF_PI);
			break;
		case rhi::SurfaceTransform::ROTATE_180:
			preRotationMatrix = math::RotateZ(math::PI);
			break;
		case rhi::SurfaceTransform::ROTATE_270:
			preRotationMatrix = math::RotateZ(-math::HALF_PI);
			break;
		default:
			break;
	}

	FrameUBO ubo{};
	ubo.view       = m_camera.GetViewMatrix();
	ubo.projection = m_camera.GetProjectionMatrix();
	ubo.projection[1][1] *= -1.0f;        // Flip the Y[1][1] component to match Vulkan's NDC
	ubo.focal = {ubo.projection[0][0] * width * 0.5f,
	             ubo.projection[1][1] * height * 0.5f};

	ubo.projection = preRotationMatrix * ubo.projection;

	ubo.cameraPos          = math::vec4(m_camera.GetPosition(), 1.0f);
	ubo.viewport           = {static_cast<float>(width), static_cast<float>(height)};
	ubo.splatScale         = 1.0f;                 // Default scale factor
	ubo.alphaCullThreshold = 1.0f / 255.0f;        // Default alpha cutoff
	ubo.maxSplatRadius     = 2048.0f;              // Maximum splat radius in pixels
	ubo.enableSplatFilter  = 1;                    // Enable EWA filtering
	ubo.inverseFocalAdj    = 1.0f;                 // No FOV adjustment by default

	// Screen rotation handling
	// basisViewport and screenRotation must match the rotated clip space
	// screenRotation stores mat2 as vec4: (col0.x, col0.y, col1.x, col1.y)
	switch (preTransform)
	{
		case rhi::SurfaceTransform::ROTATE_90:
			// 90° CCW: basisViewport swapped, rotation applied
			ubo.basisViewport  = {1.0f / height, 1.0f / width};
			ubo.screenRotation = {0.0f, 1.0f, -1.0f, 0.0f};        // mat2(0, 1, -1, 0)
			break;
		case rhi::SurfaceTransform::ROTATE_180:
			// 180°: basisViewport unchanged, negate both axes
			ubo.basisViewport  = {1.0f / width, 1.0f / height};
			ubo.screenRotation = {-1.0f, 0.0f, 0.0f, -1.0f};        // mat2(-1, 0, 0, -1)
			break;
		case rhi::SurfaceTransform::ROTATE_270:
			// 270° CCW (90° CW): basisViewport swapped, rotation applied
			ubo.basisViewport  = {1.0f / height, 1.0f / width};
			ubo.screenRotation = {0.0f, -1.0f, 1.0f, 0.0f};        // mat2(0, -1, 1, 0)
			break;
		default:
			// No rotation
			ubo.basisViewport  = {1.0f / width, 1.0f / height};
			ubo.screenRotation = {1.0f, 0.0f, 0.0f, 1.0f};        // Identity matrix
			break;
	}
	memcpy(m_frameUboDataPtr, &ubo, sizeof(FrameUBO));

	rhi::IRHICommandList *cmdList = m_commandLists[imageIndex].Get();
	cmdList->Begin();

	// Reset GPU profiling queries for this frame
	BeginGpuFrame(cmdList);

	if (m_backend && m_sortingEnabled)
	{
		// Check verification results from previous frame if pending
		if (m_checkVerificationResults)
		{
			LOG_INFO("Checking sorting verification results...");

			bool sortingCorrect;
			if (m_useSimpleVerification)
			{
				sortingCorrect = m_backend->VerifySort();
			}
			else
			{
				sortingCorrect = m_backend->RunComprehensiveVerification();
			}

			if (sortingCorrect)
			{
				LOG_INFO("Sorting verification completed successfully");
			}
			else
			{
				LOG_ERROR("Sorting verification failed - check logs for details");
			}
			m_checkVerificationResults = false;
		}

		const bool backendIsAsync = m_backend && m_backend->IsAsyncComputeEnabled();
		if (m_currentBackendType == BackendType::GPU && !backendIsAsync)
		{
			// GPU backend, single queue mode: record sort commands directly into graphics command list
			RecordSortTimestamp(cmdList, true);        // sort_begin
			m_backend->Update(m_camera, cmdList);
			RecordSortTimestamp(cmdList, false);        // sort_end
		}
		else if (m_currentBackendType == BackendType::GPU && backendIsAsync)
		{
			// GPU backend, async compute mode: sorter manages its own timing on compute queue
			m_backend->Update(m_camera);
		}
		else
		{
			// CPU backend
			m_backend->Update(m_camera);
		}

		// Prepare verification if requested
		if (m_verifyNextSort)
		{
			if (auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get()))
			{
				// GPU backend: PrepareVerification on graphics command list, check results next frame
				LOG_INFO("Preparing GPU sorting verification...");
				gpuBackend->PrepareVerification(cmdList);
				m_checkVerificationResults = true;
			}
			else
			{
				// CPU backend: Verify directly (no GPU preparation needed)
				LOG_INFO("Verifying CPU sorting...");

				bool sortingCorrect;
				if (m_useSimpleVerification)
				{
					sortingCorrect = m_backend->VerifySort();
				}
				else
				{
					sortingCorrect = m_backend->RunComprehensiveVerification();
				}

				if (sortingCorrect)
				{
					LOG_INFO("CPU sorting verification completed successfully");
				}
				else
				{
					LOG_ERROR("CPU sorting verification failed - check logs for details");
				}
			}
			m_verifyNextSort = false;
		}

		// Handle cross-backend verification if requested
		if (m_crossBackendVerifyRequested && m_crossBackendVerifyEnabled)
		{
			PerformCrossBackendVerification();
			m_crossBackendVerifyRequested = false;
		}
	}

	// Test compute rasterizer preprocess pass if enabled
	if (m_computeRasterizer && m_computeRasterizerEnabled)
	{
		m_computeRasterizer->RecordPreprocess(cmdList, *m_scene, ubo);
	}

	// Log FPS periodically
	if (m_frameCount % 60 == 0 && m_fpsCounter.shouldUpdate())
	{
		LOG_INFO("Frame FPS: {:.2f} | Sort: {} | Splats: {}",
		         m_fpsCounter.getFPS(),
		         m_sortingEnabled ? (m_backend ? m_backend->GetMethodName() : "N/A") : "Disabled",
		         m_scene->GetTotalSplatCount());
		m_fpsCounter.reset();
	}

	// Acquire sorted indices buffer from compute queue (GPU backend async compute only)
	// During warmup: use the buffer directly (WaitIdle already synced)
	// During pipelined: use previous frame's buffer with semaphore sync
	rhi::IRHIBuffer    *sortedIndicesForRendering = m_sortedIndices.Get();
	rhi::IRHISemaphore *computeSemaphore          = m_backend ? m_backend->GetComputeSemaphore() : nullptr;

	if (m_backend && m_sortingEnabled && m_backend->IsAsyncComputeEnabled())
	{
		sortedIndicesForRendering = m_backend->GetSortedIndicesBuffer();

		// Only do QFOT acquire if pipeline is warmed up and using semaphore sync
		// During warmup: buffer sync is via WaitIdle, no QFOT needed
		// During pipelined: semaphore wait + QFOT acquire
		// And rebind descriptor set with the correct buffer for current frame
		if (computeSemaphore)
		{
			if (auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
			    gpuBackend && gpuBackend->IsPipelineWarmedUp())
			{
				rhi::BufferTransition acquireTransition{};
				acquireTransition.buffer = sortedIndicesForRendering;
				acquireTransition.before = rhi::ResourceState::ShaderReadWrite;
				acquireTransition.after  = rhi::ResourceState::GeneralRead;
				cmdList->AcquireFromQueue(rhi::QueueType::COMPUTE, {&acquireTransition, 1}, {});
			}
		}

		rhi::BufferBinding indicesBinding{};
		indicesBinding.buffer = sortedIndicesForRendering;
		indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(5, indicesBinding);
	}
	else if (m_backend && m_sortingEnabled)
	{
		rhi::BufferBinding indicesBinding{};
		indicesBinding.buffer = m_sortedIndices.Get();
		indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(5, indicesBinding);
	}

	rhi::IRHITexture *backBuffer = swapchain->GetBackBuffer(imageIndex);

	// Transition to render target
	rhi::TextureTransition renderTransition = {};
	renderTransition.texture                = backBuffer;
	renderTransition.before                 = rhi::ResourceState::Undefined;
	renderTransition.after                  = rhi::ResourceState::RenderTarget;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    {&renderTransition, 1},
	    {});

	// Begin rendering
	rhi::RenderingInfo renderingInfo{};
	renderingInfo.colorAttachments.resize(1);
	renderingInfo.colorAttachments[0].view                = swapchain->GetBackBufferView(imageIndex);
	renderingInfo.colorAttachments[0].loadOp              = rhi::LoadOp::CLEAR;
	renderingInfo.colorAttachments[0].storeOp             = rhi::StoreOp::STORE;
	renderingInfo.colorAttachments[0].clearValue.color[0] = 0.0f;
	renderingInfo.colorAttachments[0].clearValue.color[1] = 0.0f;
	renderingInfo.colorAttachments[0].clearValue.color[2] = 0.0f;
	renderingInfo.colorAttachments[0].clearValue.color[3] = 1.0f;
	renderingInfo.renderAreaWidth                         = width;
	renderingInfo.renderAreaHeight                        = height;

	// Record render pass begin timestamp
	RecordRenderTimestamp(cmdList, true);

	cmdList->BeginRendering(renderingInfo);
	cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
	cmdList->SetScissor(0, 0, width, height);

	// Begin pipeline statistics query
	BeginPipelineStatsQuery(cmdList);

	// Draw the splats using indexed procedural vertex generation if scene is not empty
	uint32_t splatCount = m_scene->GetTotalSplatCount();
	if (splatCount > 0 && m_renderPipeline && m_descriptorSet && m_quadIndexBuffer)
	{
		cmdList->SetPipeline(m_renderPipeline.Get());
		cmdList->BindDescriptorSet(0, m_descriptorSet.Get());
		cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

		// Draw using instanced rendering: 4 indices per strip, one instance per splat
		uint32_t indexCount = 4;
		cmdList->DrawIndexedInstanced(indexCount, splatCount, 0, 0, 0);
	}

	// End pipeline statistics query
	EndPipelineStatsQuery(cmdList);

	if (m_imguiEnabled && m_showImGui)
	{
		UpdateFpsHistory();
		RenderImGui();
		RenderImGuiToCommandBuffer(cmdList);
	}

	cmdList->EndRendering();

	// Record render pass end timestamp
	RecordRenderTimestamp(cmdList, false);

	// Transition to present
	rhi::TextureTransition presentTransition = {};
	presentTransition.texture                = backBuffer;
	presentTransition.before                 = rhi::ResourceState::RenderTarget;
	presentTransition.after                  = rhi::ResourceState::Present;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    {&presentTransition, 1},
	    {});

	cmdList->End();

	// Submit command list
	rhi::SubmitInfo submitInfo = {};
	submitInfo.signalFence     = m_inFlightFence.Get();

	// Use explicit arrays to avoid std::span brace-initialization issues on Android release builds
	rhi::SemaphoreWaitInfo waitInfoArray[2];
	rhi::IRHISemaphore    *signalSemArray[1];
	uint32_t               numWaitSemaphores = 0;

	// Wait for compute sort to complete (GPU backend only)
	if (computeSemaphore && m_sortingEnabled)
	{
		waitInfoArray[numWaitSemaphores].semaphore = computeSemaphore;
		waitInfoArray[numWaitSemaphores].waitStage = rhi::StageMask::VertexInput;
		numWaitSemaphores++;
	}

	if (!m_vsyncBypassMode)
	{
		// Normal mode: wait for image available and signal when rendering is done
		waitInfoArray[numWaitSemaphores].semaphore = m_imageAvailableSemaphores[acquireSemIndex].Get();
		waitInfoArray[numWaitSemaphores].waitStage = rhi::StageMask::RenderTarget;
		numWaitSemaphores++;

		signalSemArray[0] = m_renderFinishedSemaphores[imageIndex].Get();

		submitInfo.waitSemaphores   = std::span<const rhi::SemaphoreWaitInfo>(waitInfoArray, numWaitSemaphores);
		submitInfo.signalSemaphores = std::span<rhi::IRHISemaphore *const>(signalSemArray, 1);
	}
	else if (numWaitSemaphores > 0)
	{
		// Vsync bypass but still need to wait for compute
		submitInfo.waitSemaphores = std::span<const rhi::SemaphoreWaitInfo>(waitInfoArray, numWaitSemaphores);
	}

	rhi::IRHICommandList *cmdListArray[1] = {cmdList};
	device->SubmitCommandLists(std::span<rhi::IRHICommandList *const>(cmdListArray, 1), rhi::QueueType::GRAPHICS, submitInfo);

	// Present (skip in vsync bypass mode)
	if (!m_vsyncBypassMode)
	{
		swapchain->Present(imageIndex, m_renderFinishedSemaphores[imageIndex].Get());
	}

	device->RetireCompletedFrame();

	// Read GPU timing results from N frames ago after GPU work is complete
	ReadGpuTimingResults();

	m_profilingFrameIndex++;

	m_frameCount++;
}

void HybridSplatRendererApp::OnShutdown()
{
	LOG_INFO("=== HybridSplatRendererApp Shutdown ===");
	LOG_INFO("Total frames rendered: {}", m_frameCount);

	if (m_deviceManager)
	{
		rhi::IRHIDevice *device = m_deviceManager->GetDevice();
		device->WaitIdle();

		if (m_imguiEnabled)
		{
			ShutdownImGui();
		}

		ShutdownGpuProfiling();

		if (m_frameUboDataPtr)
		{
			m_frameUboBuffer->Unmap();
			m_frameUboDataPtr = nullptr;
		}
	}

	m_renderPipeline      = nullptr;
	m_descriptorSet       = nullptr;
	m_descriptorSetLayout = nullptr;

	m_vertexShader   = nullptr;
	m_fragmentShader = nullptr;

	m_frameUboBuffer  = nullptr;
	m_quadIndexBuffer = nullptr;
	m_sortedIndices   = nullptr;

	m_computeRasterizer.reset();
	m_backend.reset();
	m_scene.reset();
	m_shaderFactory.reset();

	m_imageAvailableSemaphores.clear();
	m_renderFinishedSemaphores.clear();
	m_inFlightFence = nullptr;
	m_commandLists.clear();

	LOG_INFO("=== Shutdown Complete ===");
}

void HybridSplatRendererApp::OnKey(int key, int action, int mods)
{
	m_camera.OnKey(key, action, mods);

#if !defined(__ANDROID__)
	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			case GLFW_KEY_SPACE:
				// Toggle sorting on/off for performance comparison
				m_sortingEnabled = !m_sortingEnabled;
				LOG_INFO("Sorting {}", m_sortingEnabled ? "enabled" : "disabled");
				break;

			case GLFW_KEY_V:
				// Verify sorting on next frame
				m_verifyNextSort = true;
				LOG_INFO("Will verify sorting on next frame using {} verification",
				         m_useSimpleVerification ? "SIMPLE" : "COMPREHENSIVE");
				break;

			case GLFW_KEY_M:
				// Toggle sorting method between prescan and integrated scan
				if (m_backend)
				{
					// 0 = Prescan, 1 = IntegratedScan
					if (m_backend->GetSortMethod() == 1)
					{
						m_currentSortMethod = 0;
						m_backend->SetSortMethod(m_currentSortMethod);
						LOG_INFO("Switched to Prescan radix sort method");
					}
					else
					{
						m_currentSortMethod = 1;
						m_backend->SetSortMethod(m_currentSortMethod);
						LOG_INFO("Switched to Integrated Scan radix sort method");
					}
				}
				break;

			case GLFW_KEY_T:
				// Toggle verification mode
				m_useSimpleVerification = !m_useSimpleVerification;
				LOG_INFO("Verification mode switched to: {}",
				         m_useSimpleVerification ? "SIMPLE (sort order only)" : "COMPREHENSIVE (all steps)");
				break;

			case GLFW_KEY_B:
				// Toggle vsync bypass mode (skip present to remove vsync limit)
				m_vsyncBypassMode = !m_vsyncBypassMode;
				LOG_INFO("Vsync bypass {}", m_vsyncBypassMode ? "enabled" : "disabled");
				m_fpsCounter.reset();
				break;

			case GLFW_KEY_C:
			{
				// Switch between CPU and GPU backends in the next frame
				BackendType targetBackend  = (m_currentBackendType == BackendType::GPU) ? BackendType::CPU : BackendType::GPU;
				m_pendingOps.backendSwitch = PendingOperations::BackendSwitch{targetBackend};
				LOG_INFO("Backend switch to {} scheduled", targetBackend == BackendType::GPU ? "GPU" : "CPU");
				break;
			}

			case GLFW_KEY_H:
				// Toggle ImGui visibility (only if ImGui is enabled)
				if (m_imguiEnabled)
				{
					m_showImGui = !m_showImGui;
					LOG_INFO("ImGui {}", m_showImGui ? "shown" : "hidden");
				}
				break;

			case GLFW_KEY_X:
				// Toggle cross-backend verification mode
				if (m_currentBackendType == BackendType::GPU)
				{
					m_crossBackendVerifyEnabled = !m_crossBackendVerifyEnabled;
					LOG_INFO("Cross-backend verification {}", m_crossBackendVerifyEnabled ? "enabled" : "disabled");
					if (m_crossBackendVerifyEnabled)
					{
						m_crossBackendVerifyRequested = true;
					}
				}
				else
				{
					LOG_WARNING("Cross-backend verification only available when GPU backend is active");
				}
				break;

			case GLFW_KEY_ESCAPE:
				// Exit application
				glfwSetWindowShouldClose(m_deviceManager->GetWindow(), GLFW_TRUE);
				break;
		}
	}
#endif
}

void HybridSplatRendererApp::OnMouseButton(int button, int action, int mods)
{
#if defined(__ANDROID__)
	// On Android, orbit camera is handled directly in OnMouseMove
	if (button == 0)        // Primary touch
	{
		if (action == 1)        // Touch down
		{
			m_touchDown = true;
		}
		else if (action == 0)        // Touch up
		{
			m_touchDown = false;
		}
	}
#else
	m_camera.OnMouseButton(button, action, mods);
#endif
}

void HybridSplatRendererApp::OnMouseMove(double xpos, double ypos)
{
#if defined(__ANDROID__)
	// Android orbit camera: single finger drag rotates around target
	if (m_touchDown)
	{
		// Calculate delta from last position
		float deltaX = static_cast<float>(xpos - m_lastTouchX);
		float deltaY = static_cast<float>(ypos - m_lastTouchY);

		// Update orbit angles (sensitivity adjusted for touch)
		const float sensitivity = 0.2f;
		m_orbitYaw -= deltaX * sensitivity;          // Horizontal drag changes yaw
		m_orbitPitch -= deltaY * sensitivity;        // Vertical drag changes pitch

		// Clamp pitch to avoid flipping
		m_orbitPitch = math::Clamp(m_orbitPitch, -89.0f, 89.0f);

		// Update camera position
		UpdateOrbitCamera();
	}

	m_lastTouchX = xpos;
	m_lastTouchY = ypos;
#else
	m_camera.OnMouseMove(xpos, ypos);
#endif
}

void HybridSplatRendererApp::OnScroll(double xoffset, double yoffset)
{
#if defined(__ANDROID__)
	// Android pinch zoom: change orbit distance
	// yoffset > 0 means zoom in (pinch out), yoffset < 0 means zoom out (pinch in)
	const float zoomSpeed = m_orbitDistance * 0.5f;        // Relative zoom speed
	m_orbitDistance -= static_cast<float>(yoffset) * zoomSpeed;

	// Clamp distance
	m_orbitDistance = math::Clamp(m_orbitDistance, m_orbitMinDist, m_orbitMaxDist);

	UpdateOrbitCamera();
#else
	// Desktop use scroll for zoom
	(void) xoffset;
	(void) yoffset;
#endif
}

#if defined(__ANDROID__)
void HybridSplatRendererApp::UpdateOrbitCamera()
{
	// Convert spherical coordinates to Cartesian
	// yaw = 0 means looking from +Z, pitch = 0 means horizontal
	float yawRad   = math::Radians(m_orbitYaw);
	float pitchRad = math::Radians(m_orbitPitch);

	// Calculate camera position relative to target
	float cosP = std::cos(pitchRad);
	float sinP = std::sin(pitchRad);
	float cosY = std::cos(yawRad);
	float sinY = std::sin(yawRad);

	math::vec3 offset;
	offset.x = m_orbitDistance * cosP * sinY;
	offset.y = m_orbitDistance * sinP;
	offset.z = m_orbitDistance * cosP * cosY;

	// Set camera position and look at target
	m_camera.SetPosition(m_orbitTarget + offset);
	m_camera.SetTarget(m_orbitTarget);
}
#endif

void HybridSplatRendererApp::OnFramebufferResize(int width, int height)
{
	(void) width;
	(void) height;
	m_framebufferResized = true;
}

void HybridSplatRendererApp::LoadSplatFile(const char *filepath)
{
	engine::SplatLoader loader;
	auto                futureData = loader.Load(filepath);

	auto splatData = futureData.get();

	if (splatData && !splatData->empty())
	{
		// Convert coordinate system by flipping Z axis
		for (uint32_t i = 0; i < splatData->numSplats; ++i)
		{
			splatData->posZ[i] = -splatData->posZ[i];
			// Negate X and Y components of quaternion to maintain correct rotations after Z flip
			splatData->rotY[i] = -splatData->rotY[i];
			splatData->rotZ[i] = -splatData->rotZ[i];
		}

		engine::SplatMesh::ID meshId = m_scene->AddMesh(splatData, math::Identity());

		if (meshId != engine::SplatMesh::ID(-1))
		{
			m_loadedMeshIds.push_back(meshId);
			LOG_INFO("Loaded mesh {} with {} splats from {}", meshId, splatData->numSplats, filepath);
		}

		// Only do initial setup if this is the first load or loading into empty scene
		bool wasUploaded = m_scene->IsAttributeDataUploaded();
		if (!wasUploaded)
		{
			m_scene->AllocateGpuBuffers();
			rhi::FenceHandle uploadFence = m_scene->UploadAttributeData();
			if (uploadFence)
			{
				uploadFence->Wait(UINT64_MAX);
			}

			if (m_descriptorSet)
			{
				uint32_t newSplatCount = m_scene->GetTotalSplatCount();
				RebindSceneDescriptors(newSplatCount);
				ReinitializeSortBackend(newSplatCount);
			}
		}
	}
	else
	{
		LOG_WARNING("Failed to load splat file {}, creating test data", filepath);
		CreateTestSplatData();
	}
}

void HybridSplatRendererApp::CreateTestSplatData()
{
	// Camera is at (0, 0, 5) looking down -Z axis (towards origin)
	const uint32_t testSplatCount = 500000;
	auto           testData       = container::make_shared<engine::SplatSoA>();
	testData->Resize(testSplatCount, 0);

	LOG_INFO("Creating {} random test splats for comprehensive sorting verification", testSplatCount);
	LOG_INFO("Camera position: (0, 0, 5), looking towards origin (down -Z axis)");

	std::srand(114514);

	// Store positions for verification later
	m_testSplatPositions.clear();
	m_testSplatPositions.reserve(testSplatCount);

	// Define the range for random Z positions
	const float minZ   = -4995.0f;
	const float maxZ   = 5004.0f;
	const float zRange = maxZ - minZ;

	uint32_t behindCamera    = 0;
	uint32_t atCamera        = 0;
	uint32_t inFrontOfCamera = 0;
	float    nearestZ        = maxZ;
	float    farthestZ       = minZ;
	uint32_t nearestIdx      = 0;
	uint32_t farthestIdx     = 0;

	// Dead zone around camera Z where floating-point precision issues cause
	const float cameraZ      = 5.0f;
	const float deadZoneHalf = 0.1f;        // Avoid Z in range [4.9, 5.1]

	for (uint32_t i = 0; i < testSplatCount; ++i)
	{
		float randomValue = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		float z           = minZ + randomValue * zRange;

		// Push random values out of the dead zone around camera Z
		if (z > (cameraZ - deadZoneHalf) && z < (cameraZ + deadZoneHalf))
		{
			z = (z < cameraZ) ? (cameraZ - deadZoneHalf) : (cameraZ + deadZoneHalf);
		}

		// Add some specific test cases to ensure edge cases are covered
		// Note: Avoid z = cameraZ exactly as depth=0 causes FP precision issues
		if (i == 0)
			z = minZ;        // Minimum Z (farthest behind camera)
		if (i == 1)
			z = maxZ;        // Maximum Z (farthest in front)
		if (i == 2)
			z = cameraZ - deadZoneHalf;        // Edge of dead zone (behind)
		if (i == 3)
			z = cameraZ + deadZoneHalf;        // Edge of dead zone (in front)
		if (i == 4)
			z = cameraZ - 1.0f;        // 1 unit behind camera
		if (i == 5)
			z = cameraZ + 1.0f;        // 1 unit in front of camera
		if (i == 6)
			z = -1000.0f;        // Moderately far behind camera
		if (i == 7)
			z = 1000.0f;        // Moderately far in front of camera

		testData->posX[i] = 0.0f;
		testData->posY[i] = 0.0f;
		testData->posZ[i] = z;

		// Store position for later verification
		m_testSplatPositions.push_back({testData->posX[i], testData->posY[i], testData->posZ[i]});

		// Calculate expected view space depth for statistics
		float viewZ = z - 5.0f;        // Camera at Z=5
		float depth = -viewZ;

		if (z < 5.0f)
			behindCamera++;
		else if (z > 5.0f)
			inFrontOfCamera++;
		else
			atCamera++;

		if (z < nearestZ)
		{
			nearestZ   = z;
			nearestIdx = i;
		}
		if (z > farthestZ)
		{
			farthestZ   = z;
			farthestIdx = i;
		}

		// Log first few random positions
		if (i < 10)
		{
			LOG_INFO("  Splat[{}]: World Z={:.2f}, View Z={:.2f}, Depth={:.2f}",
			         i, z, viewZ, depth);
		}
		else if (i == 10)
		{
			LOG_INFO("  ... (omitting remaining entries) ...");
		}

		float scale         = 0.05f;
		testData->scaleX[i] = scale;
		testData->scaleY[i] = scale;
		testData->scaleZ[i] = scale;

		testData->rotX[i] = 0;
		testData->rotY[i] = 0;
		testData->rotZ[i] = 0;
		testData->rotW[i] = 1;

		testData->fDc0[i] = (i % 3 == 0) ? 1.0f : 0.0f;
		testData->fDc1[i] = (i % 3 == 1) ? 1.0f : 0.0f;
		testData->fDc2[i] = (i % 3 == 2) ? 1.0f : 0.0f;

		testData->opacity[i] = 0.8f;
	}

	LOG_INFO("");
	LOG_INFO("Random splat distribution statistics:");
	LOG_INFO("  Total splats: {}", testSplatCount);
	LOG_INFO("  Behind camera (Z < 5): {} splats", behindCamera);
	LOG_INFO("  At camera (Z = 5): {} splats", atCamera);
	LOG_INFO("  In front of camera (Z > 5): {} splats", inFrontOfCamera);
	LOG_INFO("");
	LOG_INFO("  Nearest splat: Splat[{}] at Z={:.2f} (depth={:.2f})",
	         nearestIdx, nearestZ, -(nearestZ - 5.0f));
	LOG_INFO("  Farthest splat: Splat[{}] at Z={:.2f} (depth={:.2f})",
	         farthestIdx, farthestZ, -(farthestZ - 5.0f));

	m_scene->AddMesh(testData, math::Identity());
	m_scene->AllocateGpuBuffers();

	rhi::FenceHandle uploadFence = m_scene->UploadAttributeData();
	uploadFence->Wait(UINT64_MAX);

	LOG_INFO("Created random test scene with {} splats", testSplatCount);
}

void HybridSplatRendererApp::OnSceneBuffersChanged(const engine::Scene::GpuData &gpuData, uint32_t newSplatCount)
{
	LOG_INFO("Scene buffers changed: {} splats", newSplatCount);

	RebindSceneDescriptors(newSplatCount);
	ReinitializeSortBackend(newSplatCount);
}

void HybridSplatRendererApp::RebindSceneDescriptors(uint32_t newSplatCount)
{
	rhi::IRHIDevice *device = m_deviceManager->GetDevice();

	device->WaitIdle();

	// Recreate app-owned sorted indices buffer with new size
	if (newSplatCount > 0)
	{
		rhi::BufferDesc indicesDesc{};
		indicesDesc.size  = newSplatCount * sizeof(uint32_t);
		indicesDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
		m_sortedIndices   = device->CreateBuffer(indicesDesc);
	}
	else
	{
		m_sortedIndices = nullptr;
	}

	// Rebind scene buffers to descriptor set
	const auto &gpuData = m_scene->GetGpuData();
	if (gpuData.positions)
	{
		rhi::BufferBinding positionsBinding{};
		positionsBinding.buffer = gpuData.positions.Get();
		positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(1, positionsBinding);
	}

	if (gpuData.covariances3D)
	{
		rhi::BufferBinding cov3DBinding{};
		cov3DBinding.buffer = gpuData.covariances3D.Get();
		cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(2, cov3DBinding);
	}

	if (gpuData.colors)
	{
		rhi::BufferBinding colorsBinding{};
		colorsBinding.buffer = gpuData.colors.Get();
		colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(3, colorsBinding);
	}

	if (gpuData.shRest)
	{
		rhi::BufferBinding shBinding{};
		shBinding.buffer = gpuData.shRest.Get();
		shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(4, shBinding);
	}

	if (m_sortedIndices)
	{
		rhi::BufferBinding indicesBinding{};
		indicesBinding.buffer = m_sortedIndices.Get();
		indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(5, indicesBinding);
	}

	if (gpuData.meshIndices)
	{
		rhi::BufferBinding meshIndicesBinding{};
		meshIndicesBinding.buffer = gpuData.meshIndices.Get();
		meshIndicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(6, meshIndicesBinding);
	}

	if (gpuData.modelMatrices)
	{
		rhi::BufferBinding modelMatricesBinding{};
		modelMatricesBinding.buffer = gpuData.modelMatrices.Get();
		modelMatricesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(7, modelMatricesBinding);
	}

	LOG_INFO("Rebound scene descriptors to new buffers");
}

void HybridSplatRendererApp::ReinitializeSortBackend(uint32_t newSplatCount)
{
	if (newSplatCount == 0)
	{
		m_backend.reset();
		LOG_INFO("Scene empty, sort backend cleared");
		return;
	}

	rhi::IRHIDevice *device = m_deviceManager->GetDevice();
	const auto      &vfs    = m_deviceManager->GetVFS();

	// Destroy old backend
	m_backend.reset();

	// Recreate backend with new scene state
	if (m_currentBackendType == BackendType::GPU)
	{
		m_backend = container::make_unique<engine::GpuSplatSortBackend>();
		if (!m_backend->Initialize(device, m_scene.get(), m_sortedIndices, newSplatCount, vfs))
		{
			LOG_WARNING("GPU backend failed to reinitialize, falling back to CPU");
			m_backend            = container::make_unique<engine::CpuSplatSortBackend>();
			m_currentBackendType = BackendType::CPU;
			m_backend->Initialize(device, m_scene.get(), m_sortedIndices, newSplatCount, vfs);
		}
		else
		{
			// Restore sort method and async compute settings for GPU backend
			m_backend->SetSortMethod(m_currentSortMethod);
			m_backend->SetAsyncCompute(m_asyncComputeEnabled);
		}
	}
	else
	{
		m_backend = container::make_unique<engine::CpuSplatSortBackend>();
		m_backend->Initialize(device, m_scene.get(), m_sortedIndices, newSplatCount, vfs);
	}

	LOG_INFO("Reinitialized {} sort backend for {} splats",
	         m_currentBackendType == BackendType::GPU ? "GPU" : "CPU",
	         newSplatCount);

	// Reinitialize compute rasterizer with new splat count
	if (m_computeRasterizer)
	{
		int fbWidth, fbHeight;
		m_deviceManager->GetPlatformAdapter()->GetFramebufferSize(&fbWidth, &fbHeight);

		// Recreate with new max splat count
		m_computeRasterizer = container::make_unique<engine::ComputeSplatRasterizer>(device, vfs);
		m_computeRasterizer->Initialize(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight), newSplatCount);
		LOG_INFO("Compute rasterizer reinitialized for {} splats", newSplatCount);
	}
}

void HybridSplatRendererApp::RecreateSwapchain()
{
	rhi::IRHIDevice    *device    = m_deviceManager->GetDevice();
	rhi::IRHISwapchain *swapchain = m_deviceManager->GetSwapchain();

	// Wait for all GPU work to complete
	device->WaitIdle();

#if !defined(__ANDROID__)
	// Get new window dimensions
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);

	// Wait if window is minimized
	while (width == 0 || height == 0)
	{
		glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
		glfwWaitEvents();
	}

	// Resize swapchain
	swapchain->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
#else
	// On Android, swapchain is already resized by the platform
	rhi::IRHITextureView *resizeBackbufferView = swapchain->GetBackBufferView(0);
	uint32_t              width                = resizeBackbufferView->GetWidth();
	uint32_t              height               = resizeBackbufferView->GetHeight();
#endif

	// Update camera aspect ratio
	float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
	m_camera.SetPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);

	// Resize compute rasterizer if initialized
	if (m_computeRasterizer && m_computeRasterizer->IsInitialized())
	{
		m_computeRasterizer->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	}

	LOG_INFO("Swapchain resized: {}x{}", width, height);
}

void HybridSplatRendererApp::InitGpuProfiling()
{
	if (!m_deviceManager)
	{
		return;
	}

	rhi::IRHIDevice    *device    = m_deviceManager->GetDevice();
	rhi::IRHISwapchain *swapchain = m_deviceManager->GetSwapchain();

	m_gpuProfilingFrameLatency = swapchain->GetImageCount();
	m_timestampPeriod          = device->GetTimestampPeriod();        // nanoseconds

	rhi::QueryPoolDesc timestampDesc{};
	timestampDesc.queryType  = rhi::QueryType::TIMESTAMP;
	timestampDesc.queryCount = TIMESTAMPS_PER_FRAME * m_gpuProfilingFrameLatency;
	m_timestampQueryPool     = device->CreateQueryPool(timestampDesc);

	if (!m_timestampQueryPool)
	{
		LOG_WARNING("Failed to create timestamp query pool, GPU timing disabled");
	}

	rhi::QueryPoolDesc statsDesc{};
	statsDesc.queryType       = rhi::QueryType::PIPELINE_STATISTICS;
	statsDesc.queryCount      = m_gpuProfilingFrameLatency;
	statsDesc.statisticsFlags = rhi::PipelineStatisticFlags::FRAGMENT_SHADER_INVOCATIONS;
	m_pipelineStatsQueryPool  = device->CreateQueryPool(statsDesc);

	if (!m_pipelineStatsQueryPool)
	{
		LOG_WARNING("Failed to create pipeline stats query pool, fragment stats disabled");
	}

	LOG_INFO("GPU Profiling initialized: {} timestamp queries, {} stats queries",
	         timestampDesc.queryCount, statsDesc.queryCount);
}

void HybridSplatRendererApp::ShutdownGpuProfiling()
{
	m_timestampQueryPool     = nullptr;
	m_pipelineStatsQueryPool = nullptr;
	m_gpuTimingHistory.clear();
}

void HybridSplatRendererApp::BeginGpuFrame(rhi::IRHICommandList *cmdList)
{
	if (!m_profilingEnabled || !m_timestampQueryPool)
	{
		return;
	}

	// This frame's queries will be properly reset, clear the mid-frame enable flag
	m_profilingJustEnabled = false;

	// Calculate query indices for this frame
	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;

	// Reset timestamp and pipeline stats queries for this frame
	cmdList->ResetQueryPool(m_timestampQueryPool.Get(), timestampOffset, TIMESTAMPS_PER_FRAME);
	if (m_pipelineStatsQueryPool)
	{
		cmdList->ResetQueryPool(m_pipelineStatsQueryPool.Get(), frameSlot, 1);
	}
}

void HybridSplatRendererApp::RecordSortTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_SORT_BEGIN : TIMESTAMP_SORT_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::RecordRenderTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_RENDER_BEGIN : TIMESTAMP_RENDER_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::RenderTarget);
}

void HybridSplatRendererApp::BeginPipelineStatsQuery(rhi::IRHICommandList *cmdList)
{
	if (!m_profilingEnabled || !m_pipelineStatsQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	cmdList->BeginQuery(m_pipelineStatsQueryPool.Get(), frameSlot);
}

void HybridSplatRendererApp::EndPipelineStatsQuery(rhi::IRHICommandList *cmdList)
{
	if (!m_profilingEnabled || !m_pipelineStatsQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	cmdList->EndQuery(m_pipelineStatsQueryPool.Get(), frameSlot);
}

void HybridSplatRendererApp::ReadGpuTimingResults()
{
	if (!m_profilingEnabled || !m_timestampQueryPool || !m_deviceManager)
	{
		return;
	}

	// Only read results after enough frames have passed
	if (m_profilingFrameIndex < m_gpuProfilingFrameLatency)
	{
		return;
	}

	rhi::IRHIDevice *device = m_deviceManager->GetDevice();

	// Read from the oldest frame slot (N frames ago)
	uint32_t readFrameIndex  = (m_profilingFrameIndex - m_gpuProfilingFrameLatency) % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = readFrameIndex * TIMESTAMPS_PER_FRAME;

	GpuTimingResults results{};

	// Determine if sort timestamps were written to the graphics queue.
	// Sort timestamps are NOT written when: Sorting is disabled and GPU backend in async compute mode
	bool backendInAsyncMode    = m_backend && m_backend->IsAsyncComputeEnabled();
	bool sortTimestampsWritten = m_sortingEnabled && m_currentBackendType == BackendType::GPU && !backendInAsyncMode;

	if (!sortTimestampsWritten)
	{
		// Sorting disabled, async compute mode, or CPU backend only read render timestamps
		uint64_t renderTimestamps[2];
		bool     renderTimestampsValid = device->GetQueryPoolResults(
            m_timestampQueryPool.Get(),
            timestampOffset + TIMESTAMP_RENDER_BEGIN,
            2,        // Only render_begin and render_end
            renderTimestamps,
            sizeof(renderTimestamps),
            sizeof(uint64_t),
            rhi::QueryResultFlags::NONE);

		results.valid = renderTimestampsValid;

		if (renderTimestampsValid)
		{
			double renderTicks   = static_cast<double>(renderTimestamps[1] - renderTimestamps[0]);
			results.renderTimeMs = (renderTicks * m_timestampPeriod) / 1000000.0;

			if (m_backend && m_sortingEnabled)
			{
				auto metrics       = m_backend->GetMetrics();
				results.sortTimeMs = metrics.sortDurationMs;
			}
		}
	}
	else
	{
		// GPU backend, single queue mode with sorting enabled: read all 4 timestamps
		// Use non-blocking instead of WAIT to avoid hanging on queries that were never written.
		// This can occur when switching modes/backends while frames are still in flight, because older
		// frames may have recorded a different query layout
		uint64_t timestamps[TIMESTAMPS_PER_FRAME];
		bool     timestampsValid = device->GetQueryPoolResults(
            m_timestampQueryPool.Get(),
            timestampOffset,
            TIMESTAMPS_PER_FRAME,
            timestamps,
            sizeof(timestamps),
            sizeof(uint64_t),
            rhi::QueryResultFlags::NONE);

		results.valid = timestampsValid;

		if (timestampsValid)
		{
			// timestamps: [0]=sort_begin, [1]=sort_end, [2]=render_begin, [3]=render_end
			double renderTicks   = static_cast<double>(timestamps[TIMESTAMP_RENDER_END] - timestamps[TIMESTAMP_RENDER_BEGIN]);
			results.renderTimeMs = (renderTicks * m_timestampPeriod) / 1000000.0;

			double sortTicks   = static_cast<double>(timestamps[TIMESTAMP_SORT_END] - timestamps[TIMESTAMP_SORT_BEGIN]);
			results.sortTimeMs = (sortTicks * m_timestampPeriod) / 1000000.0;
		}
		else
		{
			results.sortTimeMs = 0.0;
		}
	}

	// Read pipeline statistics if available
	if (m_pipelineStatsQueryPool)
	{
		uint64_t fragmentInvocations;
		bool     statsValid = device->GetQueryPoolResults(
            m_pipelineStatsQueryPool.Get(),
            readFrameIndex,
            1,
            &fragmentInvocations,
            sizeof(fragmentInvocations),
            sizeof(uint64_t),
            rhi::QueryResultFlags::NONE);

		if (statsValid)
		{
			results.fragmentInvocations = fragmentInvocations;
		}
	}

	if (results.valid)
	{
		m_currentGpuTiming = results;
	}
}

void HybridSplatRendererApp::CollectAndLogBufferMemory()
{
	m_memoryTracker.Clear();

	// Scene buffers (all device-local except sortedIndices which is DynamicUpload for CPU sort)
	if (m_scene)
	{
		const auto &gpuData = m_scene->GetGpuData();
		m_memoryTracker.AddBuffer("positions", gpuData.positions, "Scene", MemoryType::DeviceLocal);
		m_memoryTracker.AddBuffer("covariances3D", gpuData.covariances3D, "Scene", MemoryType::DeviceLocal);
		m_memoryTracker.AddBuffer("colors", gpuData.colors, "Scene", MemoryType::DeviceLocal);
		m_memoryTracker.AddBuffer("shRest", gpuData.shRest, "Scene", MemoryType::DeviceLocal);
		m_memoryTracker.AddBuffer("sortedIndices (scene)", gpuData.sortedIndices, "Scene", MemoryType::HostVisible);
	}

	// App buffers
	m_memoryTracker.AddBuffer("m_sortedIndices", m_sortedIndices, "App", MemoryType::DeviceLocal);
	m_memoryTracker.AddBuffer("m_quadIndexBuffer", m_quadIndexBuffer, "App", MemoryType::DeviceLocal);
	m_memoryTracker.AddBuffer("m_frameUboBuffer", m_frameUboBuffer, "App", MemoryType::HostVisible);

	// GPU Sorter buffers (if GPU backend is active)
	if (m_currentBackendType == BackendType::GPU && m_backend)
	{
		auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
		if (gpuBackend && gpuBackend->GetSorter())
		{
			auto buffers = gpuBackend->GetSorterBufferInfo();
			m_memoryTracker.AddBuffer("splatDepths", buffers.splatDepths, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("splatIndicesOriginal", buffers.splatIndicesOriginal, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortKeysA", buffers.sortKeysA, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortKeysB", buffers.sortKeysB, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortIndicesA", buffers.sortIndicesA, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortIndicesB", buffers.sortIndicesB, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortIndicesB_Alt", buffers.sortIndicesB_Alt, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("histograms", buffers.histograms, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("blockSums", buffers.blockSums, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("cameraUBO", buffers.cameraUBO, "Sorter", MemoryType::HostVisible);
		}
	}

	// Swapchain images
	if (m_deviceManager)
	{
		auto *swapchain = m_deviceManager->GetSwapchain();
		if (swapchain)
		{
			uint32_t imageCount = swapchain->GetImageCount();
			auto    *backBuffer = swapchain->GetBackBuffer(0);
			if (backBuffer)
			{
				m_memoryTracker.AddTexture("Swapchain Images", backBuffer, "Textures", imageCount, MemoryType::DeviceLocal);
			}
		}
	}

	// CPU host memory
	if (m_scene)
	{
		auto cpuMem = m_scene->GetCpuMemoryInfo();
		m_memoryTracker.AddCpuMemory("SplatSoA data", cpuMem.splatDataBytes, "Scene");
		m_memoryTracker.AddCpuMemory("splatPositions", cpuMem.splatPositionsBytes, "Scene");
		m_memoryTracker.AddCpuMemory("lastSortedIndices", cpuMem.sortedIndicesBytes, "Scene");
		m_memoryTracker.AddCpuMemory("CpuSplatSorter", cpuMem.cpuSorterBytes, "Sorter");
	}

	// Test verification positions (if populated)
	if (!m_testSplatPositions.empty())
	{
		m_memoryTracker.AddCpuMemory("m_testSplatPositions",
		                             m_testSplatPositions.capacity() * sizeof(math::vec3), "App");
	}

	// Shader bytecode cache
	if (m_shaderFactory)
	{
		m_memoryTracker.AddCpuMemory("Shader bytecode cache",
		                             m_shaderFactory->getBytecodeMemoryUsage(), "App");
	}

	// Get VMA-reported totals for gap analysis
	if (m_deviceManager)
	{
		auto *device = m_deviceManager->GetDevice();
		if (device)
		{
			auto vmaStats = device->GetMemoryStats();
			m_memoryTracker.SetVmaStats(vmaStats.deviceLocalUsage, vmaStats.hostVisibleUsage);
		}
	}

	// Get OS-reported CPU memory for gap analysis
	auto cpuStats = profiling::GetProcessMemoryStats();
	m_memoryTracker.SetCpuStats(cpuStats.workingSetBytes, cpuStats.privateBytes);

	m_memoryTracker.LogMemoryReport();
}

void HybridSplatRendererApp::PerformCrossBackendVerification()
{
	if (m_currentBackendType != BackendType::GPU || !m_backend || !m_scene)
	{
		m_crossBackendVerifyResult = "Verification requires GPU backend";
		return;
	}

	rhi::IRHIDevice *device     = m_deviceManager->GetDevice();
	uint32_t         splatCount = m_scene->GetTotalSplatCount();

	LOG_INFO("=== Cross-Backend Verification ===");
	LOG_INFO("Comparing GPU sort with CPU reference for {} splats", splatCount);

	// Step 1: Create staging buffer and copy GPU sorted indices
	rhi::BufferDesc stagingDesc{};
	stagingDesc.size          = splatCount * sizeof(uint32_t);
	stagingDesc.usage         = rhi::BufferUsage::TRANSFER_DST;        // Buffer receives data via copy
	stagingDesc.resourceUsage = rhi::ResourceUsage::Readback;
	auto stagingBuffer        = device->CreateBuffer(stagingDesc);

	// Copy GPU buffer to staging
	auto cmdList = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	cmdList->Begin();

	rhi::BufferCopy copyRegion{};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size      = splatCount * sizeof(uint32_t);

	std::array<rhi::BufferCopy, 1> regions = {copyRegion};
	cmdList->CopyBuffer(m_sortedIndices.Get(), stagingBuffer.Get(), regions);

	cmdList->End();

	std::array<rhi::IRHICommandList *, 1> cmdLists = {cmdList.Get()};
	device->SubmitCommandLists(cmdLists, rhi::QueueType::GRAPHICS);
	device->WaitIdle();

	// Map and read GPU indices
	container::vector<uint32_t> gpuIndices(splatCount);
	void                       *mappedData = stagingBuffer->Map();
	if (mappedData)
	{
		memcpy(gpuIndices.data(), mappedData, splatCount * sizeof(uint32_t));
		stagingBuffer->Unmap();
	}
	else
	{
		m_crossBackendVerifyResult = "Failed to map staging buffer";
		LOG_ERROR("{}", m_crossBackendVerifyResult);
		return;
	}

	// Step 2: Run CPU sort with same camera
	auto cpuBackend = container::make_unique<engine::CpuSplatSortBackend>();
	if (!cpuBackend->Initialize(device, m_scene.get(), m_sortedIndices, splatCount))
	{
		m_crossBackendVerifyResult = "Failed to initialize CPU backend for verification";
		LOG_ERROR("{}", m_crossBackendVerifyResult);
		return;
	}

	// Trigger CPU sort
	cpuBackend->Update(m_camera);

	// Wait for CPU sort to complete (poll until done)
	int maxWaitMs = 5000;
	int waitedMs  = 0;
	while (!cpuBackend->IsSortComplete() && waitedMs < maxWaitMs)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		waitedMs += 10;
	}

	if (!cpuBackend->IsSortComplete())
	{
		m_crossBackendVerifyResult = "CPU sort timed out";
		LOG_ERROR("{}", m_crossBackendVerifyResult);
		return;
	}

	// Get CPU sorted indices from Scene's CPU sorter
	auto cpuSortedIndices = m_scene->GetCpuSortedIndices();

	// Step 3: Compare results
	uint32_t mismatches    = 0;
	uint32_t firstMismatch = UINT32_MAX;
	for (uint32_t i = 0; i < splatCount && i < cpuSortedIndices.size(); ++i)
	{
		if (gpuIndices[i] != cpuSortedIndices[i])
		{
			if (mismatches < 10)
			{
				LOG_WARNING("  Mismatch at [{}]: GPU={}, CPU={}", i, gpuIndices[i], cpuSortedIndices[i]);
			}
			if (firstMismatch == UINT32_MAX)
			{
				firstMismatch = i;
			}
			mismatches++;
		}
	}

	// Report results
	if (mismatches == 0)
	{
		m_crossBackendVerifyResult = "PASSED - GPU and CPU results match";
		LOG_INFO("Cross-backend verification PASSED");
	}
	else
	{
		m_crossBackendVerifyResult =
		    "FAILED - " + std::to_string(mismatches) + " mismatches (first at " + std::to_string(firstMismatch) + ")";
		// Use LOG_WARNING instead of LOG_ERROR to avoid aborting - mismatches may be due to sort stability differences
		LOG_WARNING("Cross-backend verification FAILED: {} mismatches out of {} indices", mismatches, splatCount);
	}

	LOG_INFO("=== Verification Complete ===");
}

void HybridSplatRendererApp::InitImGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();

#if defined(__ANDROID__)
	// Android-specific ImGui configuration
	// Disable keyboard navigation on Android
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	// Scale up UI for mobile screens
	io.FontGlobalScale = 3.5f;
	ImGuiStyle &style  = ImGui::GetStyle();
	style.ScaleAllSizes(2.0f);
	style.TouchExtraPadding = ImVec2(10.0f, 10.0f);

	rhi::IRHISwapchain *swapchain        = m_deviceManager->GetSwapchain();
	uint32_t            backbufferWidth  = swapchain->GetBackBufferView(0)->GetWidth();
	uint32_t            backbufferHeight = swapchain->GetBackBufferView(0)->GetHeight();

	io.DisplaySize = ImVec2(static_cast<float>(backbufferWidth), static_cast<float>(backbufferHeight));

	LOG_INFO("ImGui init: backbuffer={}x{}", backbufferWidth, backbufferHeight);

	ImGui_ImplAndroid_Init(m_imguiWindow);
#else
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	io.FontGlobalScale = 1.3f;

	ImGui_ImplGlfw_InitForVulkan(m_deviceManager->GetWindow(), true);
#endif

	// Get RHI device
	rhi::IRHIDevice *device = m_deviceManager->GetDevice();

	// Create ImGui descriptor pool
	VkDescriptorPoolSize pool_sizes[] = {
	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType                      = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags                      = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets                    = 1000;
	pool_info.poolSizeCount              = std::size(pool_sizes);
	pool_info.pPoolSizes                 = pool_sizes;

	VkDescriptorPool imguiPool;
	VkDevice         vkDevice = static_cast<VkDevice>(device->GetNativeDevice());
	VkResult         result   = vkCreateDescriptorPool(vkDevice, &pool_info, nullptr, &imguiPool);
	if (result != VK_SUCCESS)
	{
		LOG_ERROR("Failed to create ImGui descriptor pool");
		return;
	}

	// Store the descriptor pool for cleanup
	m_imguiDescriptorPool = static_cast<void *>(imguiPool);

	// Setup ImGui Vulkan backend
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance                  = static_cast<VkInstance>(device->GetNativeInstance());
	init_info.PhysicalDevice            = static_cast<VkPhysicalDevice>(device->GetNativePhysicalDevice());
	init_info.Device                    = vkDevice;
	init_info.QueueFamily               = device->GetGraphicsQueueFamily();
	init_info.Queue                     = static_cast<VkQueue>(device->GetNativeGraphicsQueue());
	init_info.DescriptorPool            = imguiPool;
	init_info.MinImageCount             = 2;
	init_info.ImageCount                = m_deviceManager->GetSwapchain()->GetImageCount();
	init_info.UseDynamicRendering       = true;

	// Set up color attachment format for dynamic rendering (new ImGui API: fields in PipelineInfoMain)
	VkFormat colorFormat                                                           = VK_FORMAT_R8G8B8A8_UNORM;
	init_info.PipelineInfoMain.MSAASamples                                         = VK_SAMPLE_COUNT_1_BIT;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo                         = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount    = 1;
	init_info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;

	ImGui_ImplVulkan_Init(&init_info);

	LOG_INFO("ImGui initialized successfully");
}

void HybridSplatRendererApp::ShutdownImGui()
{
	// Cleanup ImGui
	ImGui_ImplVulkan_Shutdown();
#if defined(__ANDROID__)
	ImGui_ImplAndroid_Shutdown();
#else
	ImGui_ImplGlfw_Shutdown();
#endif
	ImGui::DestroyContext();

	// Destroy descriptor pool
	if (m_imguiDescriptorPool && m_deviceManager)
	{
		rhi::IRHIDevice *device   = m_deviceManager->GetDevice();
		VkDevice         vkDevice = static_cast<VkDevice>(device->GetNativeDevice());
		VkDescriptorPool pool     = static_cast<VkDescriptorPool>(m_imguiDescriptorPool);
		vkDestroyDescriptorPool(vkDevice, pool, nullptr);
		m_imguiDescriptorPool = nullptr;
	}
}

void HybridSplatRendererApp::UpdateFpsHistory()
{
	float currentFps                = static_cast<float>(m_fpsCounter.getFPS());
	m_fpsHistory[m_fpsHistoryIndex] = currentFps;
	m_fpsHistoryIndex               = (m_fpsHistoryIndex + 1) % FPS_HISTORY_SIZE;
}

void HybridSplatRendererApp::RenderImGui()
{
	ImGui_ImplVulkan_NewFrame();

#if defined(__ANDROID__)
	ImGui_ImplAndroid_NewFrame();
#else
	ImGui_ImplGlfw_NewFrame();
#endif
	ImGui::NewFrame();

#if defined(__ANDROID__)
	// Scale up UI for mobile screens
	ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(900, 1200), ImGuiCond_FirstUseEver);
#else
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
#endif

	if (ImGui::Begin("Hybrid Splat Renderer Controls", &m_showImGui))
	{
		ImGui::Text("Performance");
		ImGui::Separator();

		// FPS display
		float currentFps = static_cast<float>(m_fpsCounter.getFPS());
		ImGui::Text("FPS: %.1f", currentFps);

		// FPS graph
		ImGui::PlotLines("FPS History",
		                 m_fpsHistory.data(),
		                 static_cast<int>(FPS_HISTORY_SIZE),
		                 static_cast<int>(m_fpsHistoryIndex),
		                 nullptr,
		                 0.0f,
		                 120.0f,
		                 ImVec2(0, 80));

		// GPU Profiling section
		{
			ImGui::Spacing();
			ImGui::Separator();
			bool wasEnabled = m_profilingEnabled;
			ImGui::Checkbox("Profiling", &m_profilingEnabled);
			if (m_profilingEnabled && !wasEnabled)
			{
				// Reset frame index when profiling is enabled to avoid reading stale queries
				m_profilingFrameIndex  = 0;
				m_currentGpuTiming     = {};
				m_profilingJustEnabled = true;        // Skip query writes for rest of this frame

				CollectAndLogBufferMemory();
			}
			ImGui::Separator();

			if (m_profilingEnabled)
			{
				if (m_currentBackendType == BackendType::GPU && m_currentGpuTiming.valid)
				{
					// GPU Timing

					double sortTime = m_sortingEnabled ? m_currentGpuTiming.sortTimeMs : 0.0;
					ImGui::Text("Sort Pass:   %.3f ms", sortTime);
					ImGui::Text("Render Pass: %.3f ms", m_currentGpuTiming.renderTimeMs);
					ImGui::Text("Total:       %.3f ms", sortTime + m_currentGpuTiming.renderTimeMs);
				}

				// CPU sorter backend timing
				if (m_currentBackendType == BackendType::CPU && m_currentGpuTiming.valid)
				{
					float totalTime  = 0.0f;
					float sortTime   = 0.0f;
					float uploadTime = 0.0f;

					if (m_sortingEnabled && m_backend)
					{
						auto metrics = m_backend->GetMetrics();
						sortTime     = metrics.sortDurationMs;
						uploadTime   = metrics.uploadDurationMs;
						totalTime += sortTime + uploadTime;
					}

					ImGui::Text("Sort Time:   %.2f ms", sortTime);
					if (uploadTime > 0.0f)
					{
						ImGui::Text("Upload Time: %.2f ms", uploadTime);
					}

					totalTime += static_cast<float>(m_currentGpuTiming.renderTimeMs);
					ImGui::Text("Render Pass: %.3f ms", m_currentGpuTiming.renderTimeMs);
					ImGui::Text("Total:       %.3f ms", totalTime);
				}

				// Fragment shader invocations
				if (m_pipelineStatsQueryPool && m_scene)
				{
					uint64_t fragmentInvocations = m_currentGpuTiming.fragmentInvocations;
					uint32_t splatCount          = m_scene->GetTotalSplatCount();
					uint32_t pixelCount          = m_deviceManager->GetSwapchain()->GetBackBufferView(0)->GetWidth() *
					                      m_deviceManager->GetSwapchain()->GetBackBufferView(0)->GetHeight();

					float shadingLoad       = (pixelCount > 0) ? static_cast<float>(fragmentInvocations) / static_cast<float>(pixelCount) : 0.0f;
					float fragmentsPerSplat = (splatCount > 0) ? static_cast<float>(fragmentInvocations) / static_cast<float>(splatCount) : 0.0f;

					ImGui::Text("Shading Load: %.2fx", shadingLoad);
					ImGui::SameLine();
					ImGui::TextDisabled("(?)");
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("Fragment Invocations: %llu", static_cast<unsigned long long>(fragmentInvocations));
						ImGui::Text("Shading Load: Shader runs / Screen Pixels");
						ImGui::Text("Includes discarded fragments (~1.27x inflation from quad corners)");
						ImGui::Text("Fragments/Splat: %.1f", fragmentsPerSplat);
						ImGui::EndTooltip();
					}
				}

				// Memory Usage
				ImGui::Spacing();
				rhi::IRHIDevice *device = m_deviceManager->GetDevice();
				auto             gpuMem = device->GetMemoryStats();
				auto             cpuMem = profiling::GetProcessMemoryStats();

				float gpuUsagePercent = (gpuMem.deviceLocalBudget > 0) ? 100.0f * static_cast<float>(gpuMem.deviceLocalUsage) /
				                                                             static_cast<float>(gpuMem.deviceLocalBudget) :
				                                                         0.0f;

				ImGui::Text("GPU VRAM:    %.1f / %.1f MB (%.0f%%)",
				            gpuMem.deviceLocalUsage / 1048576.0,
				            gpuMem.deviceLocalBudget / 1048576.0,
				            gpuUsagePercent);
				ImGui::Text("GPU Shared:  %.1f / %.1f MB",
				            gpuMem.hostVisibleUsage / 1048576.0,
				            gpuMem.hostVisibleBudget / 1048576.0);
				ImGui::Text("CPU RSS:     %.1f MB", cpuMem.workingSetBytes / 1048576.0);
				ImGui::Text("CPU Private: %.1f MB", cpuMem.privateBytes / 1048576.0);
			}
		}

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Rendering Controls");
		ImGui::Separator();

		// Sorting enabled toggle
		if (ImGui::Checkbox("Enable Sorting", &m_sortingEnabled))
		{
			LOG_INFO("Sorting {}", m_sortingEnabled ? "enabled" : "disabled");
		}

		// Backend selection
		ImGui::Text("Sort Backend:");
		int         backendIndex = static_cast<int>(m_currentBackendType);
		const char *backends[]   = {"GPU", "CPU"};
		if (ImGui::Combo("##Backend", &backendIndex, backends, 2))
		{
			m_pendingOps.backendSwitch = PendingOperations::BackendSwitch{static_cast<BackendType>(backendIndex)};
		}

		// Backend-specific info
		if (m_backend)
		{
			ImGui::Text("Method: %s", m_backend->GetMethodName());

			// Sort method and shader variant switching only for GPU backend
			if (m_currentBackendType == BackendType::GPU)
			{
				int         currentMethod = m_backend->GetSortMethod();
				const char *methods[]     = {"Prescan Radix Sort", "Integrated Scan Radix Sort"};
				if (ImGui::Combo("##SortMethod", &currentMethod, methods, 2))
				{
					m_currentSortMethod = currentMethod;
					m_backend->SetSortMethod(m_currentSortMethod);
					LOG_INFO("Switched to {} radix sort method", methods[currentMethod]);
				}

				// Shader variant combo
				int         currentVariant = m_backend->GetShaderVariant();
				const char *variants[]     = {"Portable", "SubgroupOptimized"};
				if (ImGui::Combo("##ShaderVariant", &currentVariant, variants, 2))
				{
					m_backend->SetShaderVariant(currentVariant);
					LOG_INFO("Switched to {} shader variant", variants[currentVariant]);
				}
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::Text("Portable: Works on all GPUs");
					ImGui::Text("SubgroupOptimized: Requires reliable subgroup ops (Fail on Qualcomm Adreno)");
					ImGui::EndTooltip();
				}

				// Async compute toggle
				bool asyncEnabled = m_asyncComputeEnabled;
				if (ImGui::Checkbox("Async Compute", &asyncEnabled))
				{
					m_pendingOps.asyncComputeToggle = PendingOperations::AsyncComputeToggle{asyncEnabled};
					LOG_INFO("Async compute toggle scheduled: {}", asyncEnabled ? "enable" : "disable");
				}
			}
		}

		// Compute Rasterizer section
		if (m_computeRasterizer && m_computeRasterizer->IsInitialized())
		{
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Text("Compute Rasterizer (Experimental)");
			ImGui::Separator();

			if (ImGui::Checkbox("Enable Tile-Based Preprocess", &m_computeRasterizerEnabled))
			{
				LOG_INFO("Compute rasterizer preprocess {}", m_computeRasterizerEnabled ? "enabled" : "disabled");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Experimental tile-based compute pipeline");
				ImGui::Text("Currently only runs preprocess pass for verification");
				ImGui::EndTooltip();
			}

			if (m_computeRasterizerEnabled)
			{
				uint32_t tileInstanceCount = m_computeRasterizer->ReadTileInstanceCount();
				auto     stats             = m_computeRasterizer->GetStatistics();
				auto     tileConfig        = m_computeRasterizer->GetTileConfig();

				ImGui::Text("Tile Instances: %u", tileInstanceCount);
				ImGui::Text("Tiles: %u x %u = %u", tileConfig.tilesX, tileConfig.tilesY, tileConfig.totalTiles);

				if (stats.activeSplats > 0)
				{
					float avgTiles = static_cast<float>(tileInstanceCount) / static_cast<float>(stats.activeSplats);
					ImGui::Text("Avg Tiles/Splat: %.2f", avgTiles);
				}
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		// Model Management section
		ImGui::Text("Model Management");
		ImGui::Separator();

		// Model selection dropdown
		ImGui::Text("Select Model:");
		ImGui::SetNextItemWidth(-1);

		// Build combo items from predefined models
		if (ImGui::BeginCombo("##ModelSelect", k_predefinedModels[m_selectedModelIndex].name))
		{
			for (int i = 0; i < k_predefinedModelCount; ++i)
			{
				bool isSelected = (m_selectedModelIndex == i);
				if (ImGui::Selectable(k_predefinedModels[i].name, isSelected))
				{
					m_selectedModelIndex = i;
				}
				if (isSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		// Show the path for reference
		ImGui::TextDisabled("Path: %s", k_predefinedModels[m_selectedModelIndex].path);

		// Load button, defers loading to frame boundary
		if (ImGui::Button("Load Model"))
		{
			const char *modelPath = k_predefinedModels[m_selectedModelIndex].path;
			LOG_INFO("Queuing model load: {} ({})", k_predefinedModels[m_selectedModelIndex].name, modelPath);
			m_pendingOps.modelLoad = PendingOperations::ModelLoad{modelPath};
		}

		// List loaded meshes with remove buttons and transform controls
		if (!m_loadedMeshIds.empty())
		{
			ImGui::Spacing();
			ImGui::Text("Loaded Meshes:");
			for (size_t i = 0; i < m_loadedMeshIds.size(); ++i)
			{
				engine::SplatMesh::ID meshId = m_loadedMeshIds[i];
				const auto           *range  = m_scene->GetMeshGpuRange(meshId);

				if (m_meshTransforms.find(meshId) == m_meshTransforms.end())
				{
					m_meshTransforms[meshId] = MeshTransformState{};
				}
				MeshTransformState &transform = m_meshTransforms[meshId];

				char label[64];
				if (range)
				{
					snprintf(label, sizeof(label), "Mesh %u (%u splats)", meshId, range->splatCount);
				}
				else
				{
					snprintf(label, sizeof(label), "Mesh %u", meshId);
				}

				char treeLabel[64];
				snprintf(treeLabel, sizeof(treeLabel), "%s##tree%u", label, meshId);
				if (ImGui::TreeNode(treeLabel))
				{
					// Remove button
					char buttonLabel[32];
					snprintf(buttonLabel, sizeof(buttonLabel), "Remove##%u", meshId);
					if (ImGui::SmallButton(buttonLabel))
					{
						// Defer removal to frame boundary
						LOG_INFO("Queuing mesh {} for removal", meshId);
						m_pendingOps.meshRemoval = PendingOperations::MeshRemoval{meshId};
					}

					// Transform controls
					bool transformChanged = false;

					// Position sliders
					char posLabel[32];
					snprintf(posLabel, sizeof(posLabel), "Position##%u", meshId);
					if (ImGui::DragFloat3(posLabel, &transform.position.x, 0.1f, -100.0f, 100.0f, "%.2f"))
					{
						transformChanged = true;
					}

					// Rotation sliders
					char rotLabel[32];
					snprintf(rotLabel, sizeof(rotLabel), "Rotation##%u", meshId);
					if (ImGui::DragFloat3(rotLabel, &transform.rotation.x, 1.0f, -180.0f, 180.0f, "%.1f"))
					{
						transformChanged = true;
					}

					// Scale slider
					char scaleLabel[32];
					snprintf(scaleLabel, sizeof(scaleLabel), "Scale##%u", meshId);
					if (ImGui::DragFloat(scaleLabel, &transform.scale, 0.01f, 0.01f, 10.0f, "%.3f"))
					{
						transformChanged = true;
					}

					// Reset button
					char resetLabel[32];
					snprintf(resetLabel, sizeof(resetLabel), "Reset Transform##%u", meshId);
					if (ImGui::SmallButton(resetLabel))
					{
						transform.position = math::vec3(0.0f, 0.0f, 0.0f);
						transform.rotation = math::vec3(0.0f, 0.0f, 0.0f);
						transform.scale    = 1.0f;
						transformChanged   = true;
					}

					// Apply transform to scene
					if (transformChanged)
					{
						float radX = math::Radians(transform.rotation.x);
						float radY = math::Radians(transform.rotation.y);
						float radZ = math::Radians(transform.rotation.z);

						math::mat4 scaleMat     = math::Scale(math::vec3(transform.scale, transform.scale, transform.scale));
						math::mat4 rotX         = math::RotateX(radX);
						math::mat4 rotY         = math::RotateY(radY);
						math::mat4 rotZ         = math::RotateZ(radZ);
						math::mat4 translateMat = math::Translate(transform.position);
						math::mat4 modelMatrix  = translateMat * rotZ * rotY * rotX * scaleMat;

						m_scene->UpdateMeshTransform(meshId, modelMatrix);
					}

					ImGui::TreePop();
				}
			}
		}

		ImGui::Spacing();
		ImGui::Separator();

		ImGui::Text("Scene Information");
		ImGui::Separator();

		// Scene stats
		if (m_scene)
		{
			ImGui::Text("Total Splats: %u", m_scene->GetTotalSplatCount());
			ImGui::Text("Loaded Meshes: %zu", m_loadedMeshIds.size());
		}
		ImGui::Text("Frame Count: %u", m_frameCount);

		ImGui::Spacing();

		// Verification controls
		ImGui::Text("Verification");
		if (ImGui::Button("Verify Sorting Result"))
		{
			m_verifyNextSort = true;
			LOG_INFO("Will verify sorting on next frame");
		}

		// Cross-backend verification (only available with GPU backend)
		if (m_currentBackendType == BackendType::GPU)
		{
			ImGui::SameLine();
			if (ImGui::Checkbox("Cross-Backend", &m_crossBackendVerifyEnabled))
			{
				if (m_crossBackendVerifyEnabled)
				{
					m_crossBackendVerifyRequested = true;
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Compare GPU sort results with CPU reference");
			}
			if (!m_crossBackendVerifyResult.empty())
			{
				ImGui::Text("Result: %s", m_crossBackendVerifyResult.c_str());
			}
		}

		ImGui::Spacing();

		// Application info
		ImGui::Separator();
		ImGui::Text("Controls Help");
		ImGui::Separator();
#if defined(__ANDROID__)
		ImGui::BulletText("Single finger drag: Rotate camera");
		ImGui::BulletText("Pinch: Zoom in/out");
		ImGui::BulletText("Back button: Exit application");
#else
		ImGui::BulletText("WASD: Move camera");
		ImGui::BulletText("Mouse: Look around");
		ImGui::BulletText("ESC: Exit application");
		ImGui::BulletText("H: Toggle this UI");
		ImGui::BulletText("SPACE: Toggle sorting");
		ImGui::BulletText("C: Switch backend (CPU/GPU)");
		ImGui::BulletText("M: Switch GPU sort method");
		ImGui::BulletText("V: Verify sorting");
		ImGui::BulletText("X: Toggle cross-backend verify");
		ImGui::BulletText("B: Toggle vsync bypass");
#endif
	}
	ImGui::End();

	ImGui::Render();
}

void HybridSplatRendererApp::RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList)
{
	VkCommandBuffer vkCmdBuf = static_cast<VkCommandBuffer>(cmdList->GetNativeCommandBuffer());
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuf);
}

#if defined(__ANDROID__)
bool HybridSplatRendererApp::HandleImGuiInput(AInputEvent *event)
{
	if (!m_imguiEnabled)
	{
		return false;
	}
	ImGui_ImplAndroid_HandleInputEvent(event);
	return ImGui::GetIO().WantCaptureMouse;
}
#endif
