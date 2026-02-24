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

	m_currentShDegree = m_scene->GetMaxShDegree();

	// Create app-owned sorted indices buffer
	if (m_scene->GetTotalSplatCount() > 0)
	{
		rhi::BufferDesc indicesDesc{};
		indicesDesc.size  = m_scene->GetTotalSplatCount() * sizeof(uint32_t);
		indicesDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
#ifdef ENABLE_SORT_VERIFICATION
		indicesDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
#endif
		m_sortedIndices = device->CreateBuffer(indicesDesc);

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
			m_backend->SetSortMethod(m_currentSortMethod);
			m_backend->SetShaderVariant(m_currentShaderVariant);
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
	m_vertexShader   = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster_vs", rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster_fs", rhi::ShaderStage::FRAGMENT);
	LOG_INFO("Render shaders loaded: vertex={}, fragment={}", m_vertexShader != nullptr, m_fragmentShader != nullptr);

	// Create per-frame UBO buffers
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		rhi::BufferDesc uboDesc{};
		uboDesc.size                      = sizeof(FrameUBO);
		uboDesc.usage                     = rhi::BufferUsage::UNIFORM;
		uboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
		uboDesc.hints.persistently_mapped = true;
		m_frameUboBuffers[i]              = device->CreateBuffer(uboDesc);
		m_frameUboDataPtrs[i]             = m_frameUboBuffers[i]->Map();
	}

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

	// Create per-frame descriptor sets
	const auto &gpuData = m_scene->GetGpuData();
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		m_descriptorSets[i] = device->CreateDescriptorSet(m_descriptorSetLayout.Get());

		// Binding 0: UBO (per-frame)
		rhi::BufferBinding uboBinding{};
		uboBinding.buffer = m_frameUboBuffers[i].Get();
		uboBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
		m_descriptorSets[i]->BindBuffer(0, uboBinding);

		// Binding 1: Positions (shared)
		if (gpuData.positions)
		{
			rhi::BufferBinding positionsBinding{};
			positionsBinding.buffer = gpuData.positions.Get();
			positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(1, positionsBinding);
		}

		// Binding 2: Covariances3D (shared)
		if (gpuData.covariances3D)
		{
			rhi::BufferBinding cov3DBinding{};
			cov3DBinding.buffer = gpuData.covariances3D.Get();
			cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(2, cov3DBinding);
		}

		// Binding 3: Colors (shared)
		if (gpuData.colors)
		{
			rhi::BufferBinding colorsBinding{};
			colorsBinding.buffer = gpuData.colors.Get();
			colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(3, colorsBinding);
		}

		// Binding 4: SH Rest (shared)
		if (gpuData.shRest)
		{
			rhi::BufferBinding shBinding{};
			shBinding.buffer = gpuData.shRest.Get();
			shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(4, shBinding);
		}

		// Binding 5: Sorted indices (app-owned buffer, written to by backend)
		if (m_sortedIndices)
		{
			rhi::BufferBinding indicesBinding{};
			indicesBinding.buffer = m_sortedIndices.Get();
			indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(5, indicesBinding);
		}

		// Binding 6: Per-splat Mesh Indices for model matrix lookup (shared)
		if (gpuData.meshIndices)
		{
			rhi::BufferBinding meshIndicesBinding{};
			meshIndicesBinding.buffer = gpuData.meshIndices.Get();
			meshIndicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(6, meshIndicesBinding);
		}

		// Binding 7: Per-mesh Model Matrices (shared)
		if (gpuData.modelMatrices)
		{
			rhi::BufferBinding modelMatricesBinding{};
			modelMatricesBinding.buffer = gpuData.modelMatrices.Get();
			modelMatricesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(7, modelMatricesBinding);
		}
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
	pipelineDesc.vertexSpecialization                         = rhi::MakeSpecConstantU32(0, m_currentShDegree);
	m_renderPipeline                                          = device->CreateGraphicsPipeline(pipelineDesc);

	// Splat precompute Resources
	if (m_scene->GetTotalSplatCount() > 0 && m_currentBackendType == BackendType::GPU)
	{
		// Create per-frame preprocessed splats buffers
		rhi::BufferDesc hwBufDesc{};
		hwBufDesc.size  = m_scene->GetTotalSplatCount() * sizeof(HWRasterSplat);
		hwBufDesc.usage = rhi::BufferUsage::STORAGE;
		for (uint32_t k = 0; k < MAX_FRAMES_IN_FLIGHT; ++k)
		{
			m_splatPrecomputeBuffers[k] = device->CreateBuffer(hwBufDesc);
		}

		// Create per-frame atomic counter buffers
		rhi::BufferDesc atomicDesc{};
		atomicDesc.size  = sizeof(uint32_t) * 2;        // [0] = visible count, [4] = workgroup completion counter
		atomicDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
		for (uint32_t k = 0; k < MAX_FRAMES_IN_FLIGHT; ++k)
		{
			m_atomicCounterBuffers[k] = device->CreateBuffer(atomicDesc);
		}

		// Create per-frame indirect args buffers
		rhi::BufferDesc indirectDesc{};
		indirectDesc.size  = 320;        // SortParams(32) + 4 DispatchIndirect(48) + DrawIndexedIndirect(20) + 8 chunk DrawCmds(160) + padding
		indirectDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::INDIRECT;
		for (uint32_t k = 0; k < MAX_FRAMES_IN_FLIGHT; ++k)
		{
			m_indirectArgsBuffers[k] = device->CreateBuffer(indirectDesc);
		}

		// Load preprocess shader
		m_splatPrecomputeShader = m_shaderFactory->getOrCreateShader(
		    "shaders/compiled/splat_precompute_cs", rhi::ShaderStage::COMPUTE);
		m_vertexShaderPreprocessed = m_shaderFactory->getOrCreateShader(
		    "shaders/compiled/splat_raster_preprocessed_vs", rhi::ShaderStage::VERTEX);

		LOG_INFO("Splat precompute shaders loaded: compute={}, vertex={}",
		         m_splatPrecomputeShader != nullptr, m_vertexShaderPreprocessed != nullptr);

		// Create splat precompute descriptor set layout
		rhi::DescriptorSetLayoutDesc splatPrecomputeLayoutDesc{};
		splatPrecomputeLayoutDesc.bindings = {
		    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Frame UBO (view/proj matrices)
		    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Splat positions
		    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Covariance 3D packed
		    {3, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Colors half-precision
		    {4, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // SH rest coefficients interleaved
		    {5, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Mesh indices
		    {6, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Model matrices
		    {7, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Preprocessed splats output
		    {8, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Atomic counter for stream compaction
		    {9, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},         // Sort pairs output (sorter's sortPairsB)
		    {10, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},        // Indirect args buffer
		};
		m_splatPrecomputeDescriptorLayout = device->CreateDescriptorSetLayout(splatPrecomputeLayoutDesc);

		// Create splat precompute pipeline
		if (m_splatPrecomputeShader)
		{
			rhi::ComputePipelineDesc splatPrecomputePipelineDesc{};
			splatPrecomputePipelineDesc.computeShader        = m_splatPrecomputeShader.Get();
			splatPrecomputePipelineDesc.descriptorSetLayouts = {m_splatPrecomputeDescriptorLayout.Get()};

			rhi::PushConstantRange pcRange{};
			pcRange.stageFlags                             = rhi::ShaderStageFlags::COMPUTE;
			pcRange.offset                                 = 0;
			pcRange.size                                   = sizeof(uint32_t) * 4;        // numElements + sortAscending + chunkCount + totalWorkgroups
			splatPrecomputePipelineDesc.pushConstantRanges = {pcRange};
			splatPrecomputePipelineDesc.specialization     = rhi::MakeSpecConstantU32(0, m_currentShDegree);

			m_splatPrecomputePipeline = device->CreateComputePipeline(splatPrecomputePipelineDesc);
		}

		// Get sorter's sortPairsB for precompute binding
		auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
		auto *sorter     = gpuBackend ? gpuBackend->GetSorter() : nullptr;

		// Create per-frame preprocess compute descriptor sets
		if (m_splatPrecomputeDescriptorLayout)
		{
			for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
			{
				m_splatPrecomputeDescriptorSets[i] = device->CreateDescriptorSet(m_splatPrecomputeDescriptorLayout.Get());

				// Binding 0: UBO (per-frame)
				rhi::BufferBinding uboBinding{};
				uboBinding.buffer = m_frameUboBuffers[i].Get();
				uboBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(0, uboBinding);

				// Bindings 1-6: Scene data (shared)
				if (gpuData.positions)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.positions.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(1, b);
				}
				if (gpuData.covariances3D)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.covariances3D.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(2, b);
				}
				if (gpuData.colors)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.colors.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(3, b);
				}
				if (gpuData.shRest)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.shRest.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(4, b);
				}
				if (gpuData.meshIndices)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.meshIndices.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(5, b);
				}
				if (gpuData.modelMatrices)
				{
					rhi::BufferBinding b{};
					b.buffer = gpuData.modelMatrices.Get();
					b.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_splatPrecomputeDescriptorSets[i]->BindBuffer(6, b);
				}

				// Binding 7: Preprocessed splats output (per-frame)
				rhi::BufferBinding prepBinding{};
				prepBinding.buffer = m_splatPrecomputeBuffers[i].Get();
				prepBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(7, prepBinding);

				// Binding 8: Atomic counter (per-frame)
				rhi::BufferBinding atomicBinding{};
				atomicBinding.buffer = m_atomicCounterBuffers[i].Get();
				atomicBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(8, atomicBinding);

				// Binding 9: Sort pairs output (sorter's sortPairsB, shared)
				if (sorter)
				{
					auto bufInfo = sorter->GetBufferInfo();
					if (bufInfo.sortPairsB)
					{
						rhi::BufferBinding sortPairsBinding{};
						sortPairsBinding.buffer = bufInfo.sortPairsB.Get();
						sortPairsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
						m_splatPrecomputeDescriptorSets[i]->BindBuffer(9, sortPairsBinding);
					}
				}

				// Binding 10: Indirect args buffer (per-frame)
				rhi::BufferBinding indirectBinding{};
				indirectBinding.buffer = m_indirectArgsBuffers[i].Get();
				indirectBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(10, indirectBinding);
			}
		}

		// Create preprocessed render descriptor set layout
		rhi::DescriptorSetLayoutDesc prepRenderLayoutDesc{};
		prepRenderLayoutDesc.bindings = {
		    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::ALL_GRAPHICS},
		    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},
		    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},
		};
		m_descriptorSetLayoutPreprocessed = device->CreateDescriptorSetLayout(prepRenderLayoutDesc);

		// Create per-frame preprocessed render descriptor sets
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			m_descriptorSetsPreprocessed[i] = device->CreateDescriptorSet(m_descriptorSetLayoutPreprocessed.Get());

			// Binding 0: UBO (per-frame)
			rhi::BufferBinding uboB{};
			uboB.buffer = m_frameUboBuffers[i].Get();
			uboB.type   = rhi::DescriptorType::UNIFORM_BUFFER;
			m_descriptorSetsPreprocessed[i]->BindBuffer(0, uboB);

			// Binding 1: Sorted indices (shared)
			if (m_sortedIndices)
			{
				rhi::BufferBinding indB{};
				indB.buffer = m_sortedIndices.Get();
				indB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[i]->BindBuffer(1, indB);
			}

			// Binding 2: Preprocessed splats (per-frame)
			if (m_splatPrecomputeBuffers[i])
			{
				rhi::BufferBinding prepB{};
				prepB.buffer = m_splatPrecomputeBuffers[i].Get();
				prepB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[i]->BindBuffer(2, prepB);
			}
		}

		// Create preprocessed render pipeline
		if (m_vertexShaderPreprocessed && m_fragmentShader)
		{
			rhi::GraphicsPipelineDesc prepPipelineDesc = pipelineDesc;
			prepPipelineDesc.vertexShader              = m_vertexShaderPreprocessed.Get();
			prepPipelineDesc.descriptorSetLayouts      = {m_descriptorSetLayoutPreprocessed.Get()};
			m_renderPipelinePreprocessed               = device->CreateGraphicsPipeline(prepPipelineDesc);
		}

		// Initialize sorter for indirect dispatch
		if (sorter)
		{
			container::vector<rhi::IRHIBuffer *> indirectPtrs;
			for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
				indirectPtrs.push_back(m_indirectArgsBuffers[i].Get());
			sorter->Initialize(0, {}, 0, {indirectPtrs.data(), indirectPtrs.size()});
		}

		LOG_INFO("Splat precompute initialized: pipeline={}, renderPipeline={}",
		         m_splatPrecomputePipeline != nullptr, m_renderPipelinePreprocessed != nullptr);
	}

	// Create transmittance culling resources
	CreateTransmCullingResources();

	uint32_t imageCount = deviceManager->GetSwapchain()->GetImageCount();
	LOG_INFO("Swapchain image count: {}", imageCount);

	// Image-available semaphores: per frame-in-flight
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		m_imageAvailableSemaphores[i] = device->CreateSemaphore();
	}

	// Render-finished semaphores: per swapchain image
	m_renderFinishedSemaphores.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		m_renderFinishedSemaphores[i] = device->CreateSemaphore();
	}

	// Per-frame-in-flight resources: fences and command lists
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		m_inFlightFences[i] = device->CreateFence(true);
		m_commandLists[i]   = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	}

	// Async compute resources
	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		m_asyncComputeCmdLists[i]        = device->CreateCommandList(rhi::QueueType::COMPUTE);
		m_asyncComputeFences[i]          = device->CreateFence(true);
		m_asyncComputeSemaphores[i]      = device->CreateSemaphore();
		m_graphicsToComputeSemaphores[i] = device->CreateSemaphore();
	}
	m_asyncCleanupCmdList = device->CreateCommandList(rhi::QueueType::GRAPHICS);

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

		m_computeRasterizer->SetProfilingCallbacks(
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputePreprocessTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeSortTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeRangesTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeRasterTimestamp(cmd, begin); });

		m_computeRasterizer->SetSortMethod(m_currentSortMethod);
		m_computeRasterizer->SetShaderVariant(m_currentShaderVariant);

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

void HybridSplatRendererApp::ResetAsyncPipelineState()
{
	if (!m_asyncComputeEnabled)
	{
		return;
	}

	rhi::IRHIDevice *device = m_deviceManager->GetDevice();
	device->WaitIdle();

	m_asyncPipelineFrameIndex         = 0;
	m_asyncWarmupComplete             = false;
	m_splatPrecomputeAsyncWarmup      = 0;
	m_asyncPrecomputeTimingWriteCount = 0;

	for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		m_asyncComputeFences[i]          = device->CreateFence(true);
		m_asyncComputeSemaphores[i]      = device->CreateSemaphore();
		m_graphicsToComputeSemaphores[i] = device->CreateSemaphore();
	}
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

	// Restore sort settings
	m_backend->SetSortMethod(m_currentSortMethod);
	m_backend->SetShaderVariant(m_currentShaderVariant);

	m_currentBackendType = newType;

	// Splat precompute requires GPU backend
	if (newType != BackendType::GPU)
	{
		m_splatPrecomputeEnabled = false;
	}
	else
	{
		// Reinitialize indirect dispatch and rebind precompute sortPairsB for new sorter
		auto *gpuBackendSwitch = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
		auto *sorterSwitch     = gpuBackendSwitch ? gpuBackendSwitch->GetSorter() : nullptr;
		if (sorterSwitch)
		{
			if (m_indirectArgsBuffers[0])
			{
				container::vector<rhi::IRHIBuffer *> indirectPtrs;
				for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
					indirectPtrs.push_back(m_indirectArgsBuffers[i].Get());
				sorterSwitch->Initialize(0, {}, 0, {indirectPtrs.data(), indirectPtrs.size()});
			}

			if (m_splatPrecomputeDescriptorSets[0])
			{
				auto bufInfo = sorterSwitch->GetBufferInfo();
				if (bufInfo.sortPairsB)
				{
					for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
					{
						rhi::BufferBinding sortPairsBinding{};
						sortPairsBinding.buffer = bufInfo.sortPairsB.Get();
						sortPairsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
						m_splatPrecomputeDescriptorSets[i]->BindBuffer(9, sortPairsBinding);
					}
				}
			}
		}
	}

	ResetAsyncPipelineState();

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

	// Sorting toggle
	if (m_pendingOps.sortingToggle)
	{
		m_sortingEnabled = m_pendingOps.sortingToggle->enable;
		ResetAsyncPipelineState();
		m_pendingOps.sortingToggle.reset();
		LOG_INFO("Sorting {}", m_sortingEnabled ? "enabled" : "disabled");
	}

	// Async compute toggle
	if (m_pendingOps.asyncComputeToggle)
	{
		bool newEnabled = m_pendingOps.asyncComputeToggle->enable;

		if (newEnabled != m_asyncComputeEnabled)
		{
			rhi::IRHIDevice *dev = m_deviceManager->GetDevice();
			dev->WaitIdle();

			// When switching from async, acquire outstanding QFOT-released buffers
			if (!newEnabled && m_asyncPipelineFrameIndex >= MAX_FRAMES_IN_FLIGHT + 1)
			{
				auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
				auto *sorter     = gpuBackend ? gpuBackend->GetSorter() : nullptr;

				if (sorter)
				{
					rhi::IRHICommandList *gfxCmd = m_asyncCleanupCmdList.Get();
					gfxCmd->Begin();

					uint32_t buffersToAcquire = std::min(m_asyncPipelineFrameIndex - MAX_FRAMES_IN_FLIGHT,
					                                     MAX_FRAMES_IN_FLIGHT - 1);
					for (uint32_t i = 1; i <= buffersToAcquire; ++i)
					{
						uint32_t              bufIdx = (m_asyncPipelineFrameIndex - i) % MAX_FRAMES_IN_FLIGHT;
						rhi::BufferTransition t{};
						t.buffer = sorter->GetOutputBuffer(bufIdx).Get();
						t.before = rhi::ResourceState::ShaderReadWrite;
						t.after  = rhi::ResourceState::GeneralRead;
						gfxCmd->AcquireFromQueue(rhi::QueueType::COMPUTE, {&t, 1}, {});
					}

					gfxCmd->End();

					std::array<rhi::IRHICommandList *, 1> gfxCmdLists = {gfxCmd};
					dev->SubmitCommandLists(gfxCmdLists, rhi::QueueType::GRAPHICS);
					dev->WaitIdle();
				}
			}

			m_asyncComputeEnabled = newEnabled;
			ResetAsyncPipelineState();
		}

		m_pendingOps.asyncComputeToggle.reset();

		// Reset profiling frame index to avoid reading stale timestamps from the old mode.
		if (m_profilingEnabled)
		{
			m_profilingFrameIndex = 0;
			m_currentGpuTiming    = {};
		}

		LOG_INFO("Async compute {}", m_asyncComputeEnabled ? "enabled" : "disabled");
	}

	// Transmittance culling toggle
	if (m_pendingOps.transmittanceCullingToggle)
	{
		m_transmittanceCullingEnabled = m_pendingOps.transmittanceCullingToggle->enable;
		m_pendingOps.transmittanceCullingToggle.reset();

		LOG_INFO("Transmittance culling {}", m_transmittanceCullingEnabled ? "enabled" : "disabled");
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

	// Wait for this frame slot to be free
	m_inFlightFences[m_currentFrame]->Wait(UINT64_MAX);

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
	uint32_t acquireSemIndex = m_currentFrame;
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
	m_inFlightFences[m_currentFrame]->Reset();

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
	memcpy(m_frameUboDataPtrs[m_currentFrame], &ubo, sizeof(FrameUBO));

	rhi::IRHICommandList *cmdList = m_commandLists[m_currentFrame].Get();
	cmdList->Begin();

	// Reset GPU profiling queries for this frame
	BeginGpuFrame(cmdList);

	// For hardware rasterization sorting only, compute rasterizer has its own tile sorting
	bool             useComputeRasterization = m_computeRasterizer && m_rasterizationPipelineType == RasterizationPipelineType::ComputeRaster && m_computeRasterizer->IsInitialized();
	const bool       useSplatPrecompute      = m_splatPrecomputeEnabled && m_splatPrecomputePipeline && m_currentBackendType == BackendType::GPU;
	rhi::IRHIBuffer *indirectArgsForDraw     = nullptr;
	if (m_backend && (m_sortingEnabled || useSplatPrecompute) && !useComputeRasterization)
	{
		// Check verification results from previous frame if pending
		if (m_checkVerificationResults)
		{
			device->WaitIdle();

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

		// Set sort direction based on rendering mode
		// Hardware rasterization transmittance culling mode needs ascending sort (near-to-far)
		bool needAscendingSort =
		    (m_rasterizationPipelineType == RasterizationPipelineType::HardwareRaster && m_transmittanceCullingEnabled);
		m_backend->SetSortAscending(needAscendingSort);

		if (m_currentBackendType == BackendType::GPU && m_asyncComputeEnabled)
		{
			// Async compute path
			auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
			auto *sorter     = gpuBackend ? gpuBackend->GetSorter() : nullptr;

			if (sorter)
			{
				uint32_t writeIndex = m_asyncPipelineFrameIndex % MAX_FRAMES_IN_FLIGHT;

				// Wait for this pipeline slot's previous compute submission
				m_asyncComputeFences[writeIndex]->Wait(UINT64_MAX);

				// Read async precompute timing from previous use of this slot (after fence ensures completion)
				if (m_asyncPrecomputeTimingWriteCount >= MAX_FRAMES_IN_FLIGHT)
				{
					ReadAsyncPrecomputeTiming(writeIndex);
				}

				m_asyncComputeFences[writeIndex]->Reset();

				rhi::IRHICommandList *computeCmdList = m_asyncComputeCmdLists[writeIndex].Get();

				// Set sort output to this slot's K-buffered output
				rhi::BufferHandle outputBuffer = sorter->GetOutputBuffer(writeIndex);
				sorter->SetOutputBuffer(outputBuffer);
				sorter->SetSortAscending(needAscendingSort);

				// Reverse QFOT transfers buffer ownership from graphics back to compute
				bool needsReverseQFOT = m_asyncPipelineFrameIndex >= 2 * MAX_FRAMES_IN_FLIGHT;
				if (needsReverseQFOT)
				{
					// Ensure the previous graphics frame that read this slot has completed
					uint32_t otherFrameSlot = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
					m_inFlightFences[otherFrameSlot]->Wait(UINT64_MAX);

					bool prepWarmedUp = useSplatPrecompute && m_splatPrecomputeAsyncWarmup >= MAX_FRAMES_IN_FLIGHT;

					// QFOT release on graphics queue: give up ownership of buffers
					rhi::IRHICommandList *gfxCmd = m_asyncCleanupCmdList.Get();
					gfxCmd->Begin();

					rhi::BufferTransition gfxRelease[3] = {};
					uint32_t              numGfxRelease = 0;

					gfxRelease[numGfxRelease].buffer = outputBuffer.Get();
					gfxRelease[numGfxRelease].before = rhi::ResourceState::GeneralRead;
					gfxRelease[numGfxRelease].after  = rhi::ResourceState::ShaderReadWrite;
					numGfxRelease++;

					if (prepWarmedUp)
					{
						gfxRelease[numGfxRelease].buffer = m_splatPrecomputeBuffers[writeIndex].Get();
						gfxRelease[numGfxRelease].before = rhi::ResourceState::GeneralRead;
						gfxRelease[numGfxRelease].after  = rhi::ResourceState::ShaderWrite;
						numGfxRelease++;

						gfxRelease[numGfxRelease].buffer = m_indirectArgsBuffers[writeIndex].Get();
						gfxRelease[numGfxRelease].before = rhi::ResourceState::IndirectArgument;
						gfxRelease[numGfxRelease].after  = rhi::ResourceState::ShaderWrite;
						numGfxRelease++;
					}

					gfxCmd->ReleaseToQueue(rhi::QueueType::COMPUTE, {gfxRelease, numGfxRelease}, {});
					gfxCmd->End();

					rhi::SubmitInfo     gfxSubmitInfo{};
					rhi::IRHISemaphore *gfxSignalSem                  = m_graphicsToComputeSemaphores[writeIndex].Get();
					gfxSubmitInfo.signalSemaphores                    = std::span<rhi::IRHISemaphore *const>(&gfxSignalSem, 1);
					std::array<rhi::IRHICommandList *, 1> gfxCmdLists = {gfxCmd};
					device->SubmitCommandLists(gfxCmdLists, rhi::QueueType::GRAPHICS, gfxSubmitInfo);
				}

				computeCmdList->Begin();

				// QFOT acquire take ownership of buffers from graphics on compute queue
				if (needsReverseQFOT)
				{
					bool prepWarmedUp = useSplatPrecompute && m_splatPrecomputeAsyncWarmup >= MAX_FRAMES_IN_FLIGHT;

					rhi::BufferTransition computeAcquire[3] = {};
					uint32_t              numComputeAcquire = 0;

					computeAcquire[numComputeAcquire].buffer = outputBuffer.Get();
					computeAcquire[numComputeAcquire].before = rhi::ResourceState::GeneralRead;
					computeAcquire[numComputeAcquire].after  = rhi::ResourceState::ShaderReadWrite;
					numComputeAcquire++;

					if (prepWarmedUp)
					{
						computeAcquire[numComputeAcquire].buffer = m_splatPrecomputeBuffers[writeIndex].Get();
						computeAcquire[numComputeAcquire].before = rhi::ResourceState::GeneralRead;
						computeAcquire[numComputeAcquire].after  = rhi::ResourceState::ShaderWrite;
						numComputeAcquire++;

						computeAcquire[numComputeAcquire].buffer = m_indirectArgsBuffers[writeIndex].Get();
						computeAcquire[numComputeAcquire].before = rhi::ResourceState::IndirectArgument;
						computeAcquire[numComputeAcquire].after  = rhi::ResourceState::ShaderWrite;
						numComputeAcquire++;
					}

					computeCmdList->AcquireFromQueue(rhi::QueueType::GRAPHICS, {computeAcquire, numComputeAcquire}, {});
				}

				// Dispatch preprocess if enabled
				if (useSplatPrecompute)
				{
					uint32_t splatCount = m_scene->GetTotalSplatCount();

					// Reset async precompute query pool for this slot
					if (m_asyncPrecomputeQueryPool && m_profilingEnabled)
					{
						computeCmdList->ResetQueryPool(m_asyncPrecomputeQueryPool.Get(), writeIndex * 2, 2);
						computeCmdList->WriteTimestamp(m_asyncPrecomputeQueryPool.Get(), writeIndex * 2, rhi::StageMask::ComputeShader);
					}

					// Clear visible count and workgroup completion counter
					computeCmdList->FillBuffer(m_atomicCounterBuffers[writeIndex].Get(), 0, 8, 0);
					rhi::BufferTransition clearBarrier{};
					clearBarrier.buffer = m_atomicCounterBuffers[writeIndex].Get();
					clearBarrier.before = rhi::ResourceState::CopyDestination;
					clearBarrier.after  = rhi::ResourceState::ShaderReadWrite;
					computeCmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute,
					                        {&clearBarrier, 1}, {}, {});

					// Precompute dispatch: stream compaction + depth key + indirect args
					computeCmdList->SetPipeline(m_splatPrecomputePipeline.Get());
					computeCmdList->BindDescriptorSet(0, m_splatPrecomputeDescriptorSets[writeIndex].Get());

					uint32_t dispatchSize    = std::max((splatCount + 255) / 256, 1u);
					uint32_t precomputePC[4] = {splatCount, needAscendingSort ? 1u : 0u, m_chunkCount, dispatchSize};
					computeCmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
					                              {reinterpret_cast<const std::byte *>(precomputePC), sizeof(precomputePC)});
					computeCmdList->Dispatch(dispatchSize);

					// Barrier: precompute outputs + indirect args -> sort inputs
					rhi::BufferTransition postPrecompute[4] = {};
					postPrecompute[0].buffer                = m_atomicCounterBuffers[writeIndex].Get();
					postPrecompute[0].before                = rhi::ResourceState::ShaderReadWrite;
					postPrecompute[0].after                 = rhi::ResourceState::GeneralRead;
					postPrecompute[1].buffer                = m_splatPrecomputeBuffers[writeIndex].Get();
					postPrecompute[1].before                = rhi::ResourceState::ShaderWrite;
					postPrecompute[1].after                 = rhi::ResourceState::GeneralRead;
					auto sortPairsBuf                       = sorter->GetBufferInfo().sortPairsB;
					postPrecompute[2].buffer                = sortPairsBuf.Get();
					postPrecompute[2].before                = rhi::ResourceState::ShaderReadWrite;
					postPrecompute[2].after                 = rhi::ResourceState::ShaderReadWrite;
					postPrecompute[3].buffer                = m_indirectArgsBuffers[writeIndex].Get();
					postPrecompute[3].before                = rhi::ResourceState::ShaderWrite;
					postPrecompute[3].after                 = rhi::ResourceState::IndirectArgument;
					computeCmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
					                        {postPrecompute, 4}, {}, {});

					// End precompute timestamp
					if (m_asyncPrecomputeQueryPool && m_profilingEnabled)
					{
						computeCmdList->WriteTimestamp(m_asyncPrecomputeQueryPool.Get(), writeIndex * 2 + 1, rhi::StageMask::ComputeShader);
						m_asyncPrecomputeTimingWriteCount++;
					}

					// Sorting
					sorter->SortIndirect(computeCmdList, writeIndex);

					m_splatPrecomputeAsyncWarmup++;
				}
				else
				{
					m_splatPrecomputeAsyncWarmup = 0;
					sorter->Sort(computeCmdList, *m_scene, m_camera);
				}

				// Build QFOT release buffer list
				rhi::BufferTransition releaseBuffers[4] = {};
				uint32_t              numReleaseBuffers = 0;

				releaseBuffers[numReleaseBuffers].buffer = outputBuffer.Get();
				releaseBuffers[numReleaseBuffers].before = rhi::ResourceState::ShaderReadWrite;
				releaseBuffers[numReleaseBuffers].after  = rhi::ResourceState::GeneralRead;
				numReleaseBuffers++;

				if (useSplatPrecompute)
				{
					releaseBuffers[numReleaseBuffers].buffer = m_splatPrecomputeBuffers[writeIndex].Get();
					releaseBuffers[numReleaseBuffers].before = rhi::ResourceState::ShaderWrite;
					releaseBuffers[numReleaseBuffers].after  = rhi::ResourceState::GeneralRead;
					numReleaseBuffers++;

					releaseBuffers[numReleaseBuffers].buffer = m_indirectArgsBuffers[writeIndex].Get();
					releaseBuffers[numReleaseBuffers].before = rhi::ResourceState::IndirectArgument;
					releaseBuffers[numReleaseBuffers].after  = rhi::ResourceState::IndirectArgument;
					numReleaseBuffers++;
				}

				// QFOT release or warmup barrier
				if (m_asyncPipelineFrameIndex >= MAX_FRAMES_IN_FLIGHT)
				{
					computeCmdList->ReleaseToQueue(rhi::QueueType::GRAPHICS,
					                               {releaseBuffers, numReleaseBuffers}, {});
				}
				else
				{
					computeCmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
					                        {releaseBuffers, numReleaseBuffers}, {}, {});
				}

				if (m_asyncPipelineFrameIndex >= MAX_FRAMES_IN_FLIGHT)
				{
					m_asyncWarmupComplete = true;
				}

				computeCmdList->End();

				// Submit
				std::array<rhi::IRHICommandList *, 1> computeCmdLists = {computeCmdList};
				bool                                  isWarmupPhase   = !m_asyncWarmupComplete && (m_asyncPipelineFrameIndex < MAX_FRAMES_IN_FLIGHT);

				if (isWarmupPhase)
				{
					// During warmup phase (frames 0..K-1), use WaitIdle for synchronization
					rhi::SubmitInfo submitInfo{};
					submitInfo.signalFence = m_asyncComputeFences[writeIndex].Get();
					device->SubmitCommandLists(computeCmdLists, rhi::QueueType::COMPUTE, submitInfo);
					device->WaitIdle();
					sorter->ReadTimingResults();

					// Read async precompute timing
					if (useSplatPrecompute)
					{
						ReadAsyncPrecomputeTiming(writeIndex);
					}
				}
				else
				{
					rhi::SubmitInfo submitInfo{};
					submitInfo.signalFence        = m_asyncComputeFences[writeIndex].Get();
					rhi::IRHISemaphore *signalSem = m_asyncComputeSemaphores[writeIndex].Get();
					submitInfo.signalSemaphores   = std::span<rhi::IRHISemaphore *const>(&signalSem, 1);

					// Wait for graphics -> compute QFOT release to complete before compute writes
					rhi::SemaphoreWaitInfo computeWaits[1];
					uint32_t               numComputeWaits = 0;
					if (needsReverseQFOT)
					{
						computeWaits[numComputeWaits].semaphore = m_graphicsToComputeSemaphores[writeIndex].Get();
						computeWaits[numComputeWaits].waitStage = rhi::StageMask::ComputeShader;
						numComputeWaits++;
					}
					submitInfo.waitSemaphores = std::span<const rhi::SemaphoreWaitInfo>(computeWaits, numComputeWaits);

					device->SubmitCommandLists(computeCmdLists, rhi::QueueType::COMPUTE, submitInfo);
					sorter->ReadTimingResultsNonBlocking();
				}

				m_asyncPipelineFrameIndex++;
			}
		}
		else if (useSplatPrecompute && !m_asyncComputeEnabled)
		{
			// Single-queue GPU-driven preprocess + sort on graphics command list
			m_splatPrecomputeAsyncWarmup = 0;

			auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
			auto *sorter     = gpuBackend ? gpuBackend->GetSorter() : nullptr;

			if (sorter)
			{
				uint32_t splatCount = m_scene->GetTotalSplatCount();

				RecordPrecomputeTimestamp(cmdList, true);

				// Clear visible count and workgroup completion counter
				cmdList->FillBuffer(m_atomicCounterBuffers[m_currentFrame].Get(), 0, 8, 0);
				rhi::BufferTransition clearBarrier{};
				clearBarrier.buffer = m_atomicCounterBuffers[m_currentFrame].Get();
				clearBarrier.before = rhi::ResourceState::CopyDestination;
				clearBarrier.after  = rhi::ResourceState::ShaderReadWrite;
				cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute,
				                 {&clearBarrier, 1}, {}, {});

				// Precompute dispatch: stream compaction + depth key + indirect args
				cmdList->SetPipeline(m_splatPrecomputePipeline.Get());
				cmdList->BindDescriptorSet(0, m_splatPrecomputeDescriptorSets[m_currentFrame].Get());

				uint32_t dispatchSize    = std::max((splatCount + 255) / 256, 1u);
				uint32_t precomputePC[4] = {splatCount, needAscendingSort ? 1u : 0u, m_chunkCount, dispatchSize};
				cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
				                       {reinterpret_cast<const std::byte *>(precomputePC), sizeof(precomputePC)});
				cmdList->Dispatch(dispatchSize);

				// Barrier: precompute outputs + indirect args -> sort inputs
				rhi::BufferTransition postPrecompute[4] = {};
				postPrecompute[0].buffer                = m_atomicCounterBuffers[m_currentFrame].Get();
				postPrecompute[0].before                = rhi::ResourceState::ShaderReadWrite;
				postPrecompute[0].after                 = rhi::ResourceState::GeneralRead;
				postPrecompute[1].buffer                = m_splatPrecomputeBuffers[m_currentFrame].Get();
				postPrecompute[1].before                = rhi::ResourceState::ShaderWrite;
				postPrecompute[1].after                 = rhi::ResourceState::GeneralRead;
				auto sortPairsBuf                       = sorter->GetBufferInfo().sortPairsB;
				postPrecompute[2].buffer                = sortPairsBuf.Get();
				postPrecompute[2].before                = rhi::ResourceState::ShaderReadWrite;
				postPrecompute[2].after                 = rhi::ResourceState::ShaderReadWrite;
				postPrecompute[3].buffer                = m_indirectArgsBuffers[m_currentFrame].Get();
				postPrecompute[3].before                = rhi::ResourceState::ShaderWrite;
				postPrecompute[3].after                 = rhi::ResourceState::IndirectArgument;
				cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
				                 {postPrecompute, 4}, {}, {});

				RecordPrecomputeTimestamp(cmdList, false);

				// Sort
				RecordSortTimestamp(cmdList, true);
				sorter->SetSortAscending(needAscendingSort);
				sorter->SetOutputBuffer(m_sortedIndices);
				sorter->SortIndirect(cmdList, m_currentFrame);
				RecordSortTimestamp(cmdList, false);
				indirectArgsForDraw = m_indirectArgsBuffers[m_currentFrame].Get();

				// Barrier: sort + precompute outputs -> graphics reads
				rhi::BufferTransition postBarriers[3] = {};
				postBarriers[0].buffer                = m_sortedIndices.Get();
				postBarriers[0].before                = rhi::ResourceState::ShaderReadWrite;
				postBarriers[0].after                 = rhi::ResourceState::GeneralRead;
				postBarriers[1].buffer                = m_splatPrecomputeBuffers[m_currentFrame].Get();
				postBarriers[1].before                = rhi::ResourceState::GeneralRead;
				postBarriers[1].after                 = rhi::ResourceState::GeneralRead;
				postBarriers[2].buffer                = m_indirectArgsBuffers[m_currentFrame].Get();
				postBarriers[2].before                = rhi::ResourceState::IndirectArgument;
				postBarriers[2].after                 = rhi::ResourceState::IndirectArgument;
				cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Graphics,
				                 {postBarriers, 3}, {}, {});
			}
		}
		else if (m_currentBackendType == BackendType::GPU && !m_asyncComputeEnabled)
		{
			// GPU backend, single queue mode (no preprocess)
			m_splatPrecomputeAsyncWarmup = 0;
			RecordSortTimestamp(cmdList, true);
			m_backend->Update(m_camera, cmdList);
			RecordSortTimestamp(cmdList, false);
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
				if (m_asyncComputeEnabled)
				{
					// Sort buffers are owned by the compute queue. Run a single-queue sort on the
					// graphics command list so internal buffers are accessible for verification copies
					LOG_INFO("Preparing GPU sorting verification (async compute mode)...");
					device->WaitIdle();

					auto *sorter = gpuBackend->GetSorter();
					sorter->SetOutputBuffer(m_sortedIndices);
					sorter->Sort(cmdList, *m_scene, m_camera);

					gpuBackend->PrepareVerification(cmdList);

					ResetAsyncPipelineState();
				}
				else
				{
					LOG_INFO("Preparing GPU sorting verification...");
					gpuBackend->PrepareVerification(cmdList);
				}
				m_checkVerificationResults = true;
			}
			else
			{
				// CPU backend: Verify directly
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

	// Log FPS periodically
	if (m_frameCount % 60 == 0 && m_fpsCounter.shouldUpdate())
	{
		if (m_profilingEnabled && !m_gpuTimingHistory.empty())
		{
			GpuTimingResults avg{};
			for (const auto &sample : m_gpuTimingHistory)
			{
				avg.precomputeTimeMs += sample.precomputeTimeMs;
				avg.sortTimeMs += sample.sortTimeMs;
				avg.renderTimeMs += sample.renderTimeMs;
				avg.preprocessTimeMs += sample.preprocessTimeMs;
				avg.computeSortTimeMs += sample.computeSortTimeMs;
				avg.rangesTimeMs += sample.rangesTimeMs;
				avg.rasterTimeMs += sample.rasterTimeMs;
			}
			double count = static_cast<double>(m_gpuTimingHistory.size());
			avg.precomputeTimeMs /= count;
			avg.sortTimeMs /= count;
			avg.renderTimeMs /= count;
			avg.preprocessTimeMs /= count;
			avg.computeSortTimeMs /= count;
			avg.rangesTimeMs /= count;
			avg.rasterTimeMs /= count;
			avg.valid           = true;
			m_averagedGpuTiming = avg;
			m_gpuTimingHistory.clear();

			if (useComputeRasterization)
			{
				LOG_INFO("Frame FPS: {:.2f} | {} | Splats: {} | Preprocess: {:.3f}ms | Sort: {:.3f}ms | Ranges: {:.3f}ms | Raster: {:.3f}ms | Render: {:.3f}ms",
				         m_fpsCounter.getFPS(),
				         "Compute Raster",
				         m_scene->GetTotalSplatCount(),
				         avg.preprocessTimeMs,
				         avg.computeSortTimeMs,
				         avg.rangesTimeMs,
				         avg.rasterTimeMs,
				         avg.renderTimeMs);
			}
			else if (avg.precomputeTimeMs > 0.0)
			{
				LOG_INFO("Frame FPS: {:.2f} | {} | Splats: {} | Precompute: {:.3f}ms | Sort: {:.3f}ms | Render: {:.3f}ms | Total: {:.3f}ms",
				         m_fpsCounter.getFPS(),
				         m_sortingEnabled ? (m_backend ? m_backend->GetMethodName() : "N/A") : "Disabled",
				         m_scene->GetTotalSplatCount(),
				         avg.precomputeTimeMs,
				         avg.sortTimeMs,
				         avg.renderTimeMs,
				         avg.precomputeTimeMs + avg.sortTimeMs + avg.renderTimeMs);
			}
			else
			{
				LOG_INFO("Frame FPS: {:.2f} | {} | Splats: {} | Sort: {:.3f}ms | Render: {:.3f}ms",
				         m_fpsCounter.getFPS(),
				         m_sortingEnabled ? (m_backend ? m_backend->GetMethodName() : "N/A") : "Disabled",
				         m_scene->GetTotalSplatCount(),
				         avg.sortTimeMs,
				         avg.renderTimeMs);
			}
		}
		else
		{
			LOG_INFO("Frame FPS: {:.2f} | {} | Splats: {}",
			         m_fpsCounter.getFPS(),
			         useComputeRasterization ? "Compute Raster" :
			                                   (m_sortingEnabled ? (m_backend ? m_backend->GetMethodName() : "N/A") : "Disabled"),
			         m_scene->GetTotalSplatCount());
		}
		m_fpsCounter.reset();
	}

	rhi::IRHITexture *backBuffer = swapchain->GetBackBuffer(imageIndex);

	// Compute semaphore for async compute sort (only used in hardware path)
	rhi::IRHISemaphore *computeSemaphore = nullptr;

	if (useComputeRasterization)
	{
		// === Compute rasterization path ===
		m_computeRasterizer->Render(cmdList, *m_scene, ubo);

		// Blit compute output to backbuffer
		{
			rhi::TextureTransition transition = {};
			transition.texture                = backBuffer;
			transition.before                 = rhi::ResourceState::Undefined;
			transition.after                  = rhi::ResourceState::CopyDestination;

			cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Copy,
			                 {}, {&transition, 1}, {});
		}

		rhi::TextureBlit blitRegion = {};
		blitRegion.srcX1            = m_computeRasterizer->GetTileConfig().screenWidth;
		blitRegion.srcY1            = m_computeRasterizer->GetTileConfig().screenHeight;
		blitRegion.srcZ1            = 1;
		blitRegion.dstX1            = width;
		blitRegion.dstY1            = height;
		blitRegion.dstZ1            = 1;

		cmdList->BlitTexture(m_computeRasterizer->GetOutputImage(), backBuffer,
		                     {&blitRegion, 1}, rhi::FilterMode::LINEAR);

		// Transition backbuffer for ImGui render pass
		{
			rhi::TextureTransition transition = {};
			transition.texture                = backBuffer;
			transition.before                 = rhi::ResourceState::CopyDestination;
			transition.after                  = rhi::ResourceState::RenderTarget;

			cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Graphics,
			                 {}, {&transition, 1}, {});
		}

		// Render ImGui overlay
		rhi::RenderingInfo renderingInfo{};
		renderingInfo.colorAttachments.resize(1);
		renderingInfo.colorAttachments[0].view    = swapchain->GetBackBufferView(imageIndex);
		renderingInfo.colorAttachments[0].loadOp  = rhi::LoadOp::LOAD;
		renderingInfo.colorAttachments[0].storeOp = rhi::StoreOp::STORE;
		renderingInfo.renderAreaWidth             = width;
		renderingInfo.renderAreaHeight            = height;

		RecordRenderTimestamp(cmdList, true);

		cmdList->BeginRendering(renderingInfo);
		cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
		cmdList->SetScissor(0, 0, width, height);

		if (m_imguiEnabled && m_showImGui)
		{
			UpdateFpsHistory();
			RenderImGui();
			RenderImGuiToCommandBuffer(cmdList);
		}

		cmdList->EndRendering();
	}
	else
	{
		// === Hardware rasterization path ===
		// Acquire sorted indices buffer from compute queue (GPU backend async compute only)
		rhi::IRHIBuffer *sortedIndicesForRendering = m_sortedIndices.Get();

		if (m_backend && (m_sortingEnabled || useSplatPrecompute) && m_asyncComputeEnabled)
		{
			// K-buffered slot reads from K-1 frames behind write
			uint32_t readIndex  = m_asyncPipelineFrameIndex % MAX_FRAMES_IN_FLIGHT;
			auto    *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
			auto    *sorter     = gpuBackend ? gpuBackend->GetSorter() : nullptr;

			if (m_asyncWarmupComplete && sorter)
			{
				sortedIndicesForRendering = sorter->GetOutputBuffer(readIndex).Get();
			}
			else if (sorter && m_asyncPipelineFrameIndex > 0)
			{
				uint32_t currentIndex     = (m_asyncPipelineFrameIndex - 1) % MAX_FRAMES_IN_FLIGHT;
				sortedIndicesForRendering = sorter->GetOutputBuffer(currentIndex).Get();
			}

			// Need compute semaphore when fully pipelined after 2*K frames
			if (m_asyncPipelineFrameIndex >= 2 * MAX_FRAMES_IN_FLIGHT)
			{
				computeSemaphore = m_asyncComputeSemaphores[readIndex].Get();
			}

			if (computeSemaphore)
			{
				// QFOT acquire sorted indices
				rhi::BufferTransition acquireTransition{};
				acquireTransition.buffer = sortedIndicesForRendering;
				acquireTransition.before = rhi::ResourceState::ShaderReadWrite;
				acquireTransition.after  = rhi::ResourceState::GeneralRead;

				bool prepWarmedUp = useSplatPrecompute && m_splatPrecomputeAsyncWarmup >= MAX_FRAMES_IN_FLIGHT;

				if (prepWarmedUp)
				{
					// Acquire sorted indices + preprocessed splats + indirect args
					rhi::BufferTransition acquireTransitions[3] = {};
					uint32_t              numAcquire            = 0;
					acquireTransitions[numAcquire++]            = acquireTransition;
					acquireTransitions[numAcquire].buffer       = m_splatPrecomputeBuffers[readIndex].Get();
					acquireTransitions[numAcquire].before       = rhi::ResourceState::ShaderWrite;
					acquireTransitions[numAcquire].after        = rhi::ResourceState::GeneralRead;
					numAcquire++;
					acquireTransitions[numAcquire].buffer = m_indirectArgsBuffers[readIndex].Get();
					acquireTransitions[numAcquire].before = rhi::ResourceState::IndirectArgument;
					acquireTransitions[numAcquire].after  = rhi::ResourceState::IndirectArgument;
					numAcquire++;
					cmdList->AcquireFromQueue(rhi::QueueType::COMPUTE, {acquireTransitions, numAcquire}, {});

					// Rebind correct K-slot to render descriptor
					rhi::BufferBinding prepB{};
					prepB.buffer = m_splatPrecomputeBuffers[readIndex].Get();
					prepB.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_descriptorSetsPreprocessed[m_currentFrame]->BindBuffer(2, prepB);
				}
				else
				{
					cmdList->AcquireFromQueue(rhi::QueueType::COMPUTE, {&acquireTransition, 1}, {});
				}
			}

			rhi::BufferBinding indicesBinding{};
			indicesBinding.buffer = sortedIndicesForRendering;
			indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[m_currentFrame]->BindBuffer(5, indicesBinding);

			// Rebind sorted indices to preprocessed descriptor set for async mode
			if (useSplatPrecompute)
			{
				rhi::BufferBinding indB{};
				indB.buffer = sortedIndicesForRendering;
				indB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[m_currentFrame]->BindBuffer(1, indB);

				// Set indirect args buffer for DrawIndexedIndirect
				if (m_asyncPipelineFrameIndex > 0)
				{
					uint32_t indirectReadIndex = m_asyncWarmupComplete ? readIndex : (m_asyncPipelineFrameIndex - 1) % MAX_FRAMES_IN_FLIGHT;
					indirectArgsForDraw        = m_indirectArgsBuffers[indirectReadIndex].Get();
				}
			}
		}
		else if (m_backend && (m_sortingEnabled || useSplatPrecompute))
		{
			rhi::BufferBinding indicesBinding{};
			indicesBinding.buffer = m_sortedIndices.Get();
			indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[m_currentFrame]->BindBuffer(5, indicesBinding);

			// Rebind preprocessed descriptor set to ensure correct bindings after async->sync switch
			if (useSplatPrecompute && m_descriptorSetsPreprocessed[m_currentFrame])
			{
				rhi::BufferBinding indB{};
				indB.buffer = m_sortedIndices.Get();
				indB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[m_currentFrame]->BindBuffer(1, indB);

				rhi::BufferBinding prepB{};
				prepB.buffer = m_splatPrecomputeBuffers[m_currentFrame].Get();
				prepB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[m_currentFrame]->BindBuffer(2, prepB);
			}
		}

		uint32_t splatCount = m_scene->GetTotalSplatCount();

		// Record render pass begin timestamp
		RecordRenderTimestamp(cmdList, true);

		// Begin pipeline statistics query
		BeginPipelineStatsQuery(cmdList);

		if (m_transmittanceCullingEnabled &&
		    m_stencilUpdatePipeline && m_splatTransmCullingPipeline && m_depthStencilTextures[m_currentFrame])
		{
			// === Transmittance culling mode ===
			// Renders splats front-to-back, using interleaved stencil updates to cull saturated pixels early
			RenderTransmittanceCulling(cmdList, splatCount, width, height, imageIndex, m_currentFrame, indirectArgsForDraw);
		}
		else
		{
			// === Normal back-to-front rendering mode ===
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

			cmdList->BeginRendering(renderingInfo);
			cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
			cmdList->SetScissor(0, 0, width, height);

			// Draw the splats using indexed procedural vertex generation if scene is not empty
			if (splatCount > 0 && m_quadIndexBuffer)
			{
				if (useSplatPrecompute && m_renderPipelinePreprocessed && m_descriptorSetsPreprocessed[m_currentFrame])
				{
					cmdList->SetPipeline(m_renderPipelinePreprocessed.Get());
					cmdList->BindDescriptorSet(0, m_descriptorSetsPreprocessed[m_currentFrame].Get());
				}
				else if (m_renderPipeline && m_descriptorSets[m_currentFrame])
				{
					cmdList->SetPipeline(m_renderPipeline.Get());
					cmdList->BindDescriptorSet(0, m_descriptorSets[m_currentFrame].Get());
				}
				cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

				// Draw using indirect when GPU-driven pipeline is active, otherwise direct
				if (indirectArgsForDraw)
				{
					cmdList->DrawIndexedIndirect(indirectArgsForDraw, 80, 1);
				}
				else
				{
					uint32_t indexCount = 4;
					cmdList->DrawIndexedInstanced(indexCount, splatCount, 0, 0, 0);
				}
			}

			if (m_imguiEnabled && m_showImGui)
			{
				UpdateFpsHistory();
				RenderImGui();
				RenderImGuiToCommandBuffer(cmdList);
			}

			cmdList->EndRendering();
		}

		// End pipeline statistics query
		EndPipelineStatsQuery(cmdList);
	}

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
	submitInfo.signalFence     = m_inFlightFences[m_currentFrame].Get();

	// Use explicit arrays to avoid std::span brace-initialization issues on Android release builds
	rhi::SemaphoreWaitInfo waitInfoArray[2];
	rhi::IRHISemaphore    *signalSemArray[1];
	uint32_t               numWaitSemaphores = 0;

	// Wait for compute sort to complete (GPU sorting backend only)
	if (computeSemaphore && (m_sortingEnabled || useSplatPrecompute) && m_asyncComputeEnabled)
	{
		waitInfoArray[numWaitSemaphores].semaphore = computeSemaphore;
		waitInfoArray[numWaitSemaphores].waitStage = useSplatPrecompute ? rhi::StageMask::DrawIndirect : rhi::StageMask::VertexInput;
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
	m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
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

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (m_frameUboDataPtrs[i])
			{
				m_frameUboBuffers[i]->Unmap();
				m_frameUboDataPtrs[i] = nullptr;
			}
		}
	}

	m_renderPipeline = nullptr;
	for (auto &ds : m_descriptorSets)
		ds = nullptr;
	m_descriptorSetLayout = nullptr;

	m_vertexShader   = nullptr;
	m_fragmentShader = nullptr;

	// Splat precompute resources
	m_splatPrecomputePipeline = nullptr;
	for (auto &ds : m_splatPrecomputeDescriptorSets)
		ds = nullptr;
	m_splatPrecomputeDescriptorLayout = nullptr;
	m_splatPrecomputeShader           = nullptr;
	for (auto &buf : m_splatPrecomputeBuffers)
		buf = nullptr;
	m_renderPipelinePreprocessed             = nullptr;
	m_splatTransmCullingPipelinePreprocessed = nullptr;
	m_vertexShaderPreprocessed               = nullptr;
	for (auto &ds : m_descriptorSetsPreprocessed)
		ds = nullptr;
	m_descriptorSetLayoutPreprocessed = nullptr;

	// GPU-driven indirect dispatch resources
	for (auto &buf : m_atomicCounterBuffers)
		buf = nullptr;
	for (auto &buf : m_indirectArgsBuffers)
		buf = nullptr;

	// Async compute resources
	for (auto &cl : m_asyncComputeCmdLists)
		cl = nullptr;
	for (auto &f : m_asyncComputeFences)
		f = nullptr;
	for (auto &s : m_asyncComputeSemaphores)
		s = nullptr;
	for (auto &s : m_graphicsToComputeSemaphores)
		s = nullptr;
	m_asyncCleanupCmdList = nullptr;

	// Transmittance culling resources
	m_transmCullingFragmentShader = nullptr;
	m_stencilUpdateFragmentShader = nullptr;
	m_stencilUpdatePipeline       = nullptr;
	m_splatTransmCullingPipeline  = nullptr;
	for (auto &ds : m_stencilUpdateDescriptorSets)
		ds = nullptr;
	m_stencilUpdateDescriptorLayout = nullptr;
	for (auto &v : m_accumTextureViews)
		v = nullptr;
	for (auto &t : m_accumTextures)
		t = nullptr;
	for (auto &v : m_depthStencilViews)
		v = nullptr;
	for (auto &t : m_depthStencilTextures)
		t = nullptr;

	m_compositePipeline = nullptr;
	for (auto &ds : m_compositeDescriptorSets)
		ds = nullptr;
	m_compositeDescriptorSetLayout = nullptr;
	m_compositeSampler             = nullptr;
	m_compositeFragmentShader      = nullptr;
	m_fullscreenVertexShader       = nullptr;

	for (auto &buf : m_frameUboBuffers)
		buf = nullptr;
	m_quadIndexBuffer = nullptr;
	m_sortedIndices   = nullptr;

	m_computeRasterizer.reset();
	m_backend.reset();
	m_scene.reset();
	m_shaderFactory.reset();

	for (auto &sem : m_imageAvailableSemaphores)
		sem = nullptr;
	m_renderFinishedSemaphores.clear();
	for (auto &fence : m_inFlightFences)
		fence = nullptr;
	for (auto &cmd : m_commandLists)
		cmd = nullptr;

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
				m_pendingOps.sortingToggle = PendingOperations::SortingToggle{!m_sortingEnabled};
				break;

#	ifdef ENABLE_SORT_VERIFICATION
			case GLFW_KEY_V:
				// Verify sorting on next frame
				m_verifyNextSort = true;
				LOG_INFO("Will verify sorting on next frame using {} verification",
				         m_useSimpleVerification ? "SIMPLE" : "COMPREHENSIVE");
				break;
#	endif

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

#	ifdef ENABLE_SORT_VERIFICATION
			case GLFW_KEY_T:
				// Toggle verification mode
				m_useSimpleVerification = !m_useSimpleVerification;
				LOG_INFO("Verification mode switched to: {}",
				         m_useSimpleVerification ? "SIMPLE (sort order only)" : "COMPREHENSIVE (all steps)");
				break;
#	endif

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

#	ifdef ENABLE_SORT_VERIFICATION
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
#	endif

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

			if (m_descriptorSets[0])
			{
				uint32_t newSplatCount = m_scene->GetTotalSplatCount();
				RebindSceneDescriptors(newSplatCount);
				ReinitializeSortBackend(newSplatCount);
			}

			// Check if SH degree changed
			uint32_t newShDegree = m_scene->GetMaxShDegree();
			if (newShDegree != m_currentShDegree)
			{
				LOG_INFO("SH degree changed: {} -> {}, recreating pipelines", m_currentShDegree, newShDegree);
				m_currentShDegree = newShDegree;
				RecreateSHDependentPipelines();
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

	uint32_t newShDegree = m_scene->GetMaxShDegree();
	if (newShDegree != m_currentShDegree)
	{
		LOG_INFO("SH degree changed: {} -> {}, recreating pipelines", m_currentShDegree, newShDegree);
		m_currentShDegree = newShDegree;
		RecreateSHDependentPipelines();
	}
}

void HybridSplatRendererApp::RecreateSHDependentPipelines()
{
	rhi::IRHIDevice    *device    = m_deviceManager->GetDevice();
	rhi::IRHISwapchain *swapchain = m_deviceManager->GetSwapchain();

	// Recreate graphics render pipeline
	if (m_vertexShader && m_fragmentShader)
	{
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
		pipelineDesc.vertexSpecialization                         = rhi::MakeSpecConstantU32(0, m_currentShDegree);
		m_renderPipeline                                          = device->CreateGraphicsPipeline(pipelineDesc);
	}

	// Recreate splat precompute pipeline
	if (m_splatPrecomputeShader && m_splatPrecomputeDescriptorLayout)
	{
		rhi::ComputePipelineDesc splatPrecomputePipelineDesc{};
		splatPrecomputePipelineDesc.computeShader        = m_splatPrecomputeShader.Get();
		splatPrecomputePipelineDesc.descriptorSetLayouts = {m_splatPrecomputeDescriptorLayout.Get()};

		rhi::PushConstantRange pcRange{};
		pcRange.stageFlags                             = rhi::ShaderStageFlags::COMPUTE;
		pcRange.offset                                 = 0;
		pcRange.size                                   = sizeof(uint32_t) * 4;
		splatPrecomputePipelineDesc.pushConstantRanges = {pcRange};
		splatPrecomputePipelineDesc.specialization     = rhi::MakeSpecConstantU32(0, m_currentShDegree);

		m_splatPrecomputePipeline = device->CreateComputePipeline(splatPrecomputePipelineDesc);
	}

	LOG_INFO("Recreated SH-dependent pipelines with SH degree {}", m_currentShDegree);
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

	// Rebind scene buffers to all per-frame descriptor sets
	const auto &gpuData = m_scene->GetGpuData();
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (gpuData.positions)
		{
			rhi::BufferBinding positionsBinding{};
			positionsBinding.buffer = gpuData.positions.Get();
			positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(1, positionsBinding);
		}

		if (gpuData.covariances3D)
		{
			rhi::BufferBinding cov3DBinding{};
			cov3DBinding.buffer = gpuData.covariances3D.Get();
			cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(2, cov3DBinding);
		}

		if (gpuData.colors)
		{
			rhi::BufferBinding colorsBinding{};
			colorsBinding.buffer = gpuData.colors.Get();
			colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(3, colorsBinding);
		}

		if (gpuData.shRest)
		{
			rhi::BufferBinding shBinding{};
			shBinding.buffer = gpuData.shRest.Get();
			shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(4, shBinding);
		}

		if (m_sortedIndices)
		{
			rhi::BufferBinding indicesBinding{};
			indicesBinding.buffer = m_sortedIndices.Get();
			indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(5, indicesBinding);
		}

		if (gpuData.meshIndices)
		{
			rhi::BufferBinding meshIndicesBinding{};
			meshIndicesBinding.buffer = gpuData.meshIndices.Get();
			meshIndicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(6, meshIndicesBinding);
		}

		if (gpuData.modelMatrices)
		{
			rhi::BufferBinding modelMatricesBinding{};
			modelMatricesBinding.buffer = gpuData.modelMatrices.Get();
			modelMatricesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_descriptorSets[i]->BindBuffer(7, modelMatricesBinding);
		}
	}

	// Rebind Splat precompute resources
	if (newSplatCount > 0 && m_splatPrecomputeDescriptorSets[0])
	{
		// Recreate K-buffered preprocessed splats buffers with new size
		rhi::BufferDesc hwBufDesc{};
		hwBufDesc.size  = newSplatCount * sizeof(HWRasterSplat);
		hwBufDesc.usage = rhi::BufferUsage::STORAGE;
		for (uint32_t k = 0; k < MAX_FRAMES_IN_FLIGHT; ++k)
		{
			m_splatPrecomputeBuffers[k] = device->CreateBuffer(hwBufDesc);
		}

		// Rebind scene data to all per-frame preprocess compute descriptor sets
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (gpuData.positions)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.positions.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(1, b);
			}
			if (gpuData.covariances3D)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.covariances3D.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(2, b);
			}
			if (gpuData.colors)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.colors.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(3, b);
			}
			if (gpuData.shRest)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.shRest.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(4, b);
			}
			if (gpuData.meshIndices)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.meshIndices.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(5, b);
			}
			if (gpuData.modelMatrices)
			{
				rhi::BufferBinding b{};
				b.buffer = gpuData.modelMatrices.Get();
				b.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_splatPrecomputeDescriptorSets[i]->BindBuffer(6, b);
			}

			// Binding 7: Preprocessed splats output
			rhi::BufferBinding prepBinding{};
			prepBinding.buffer = m_splatPrecomputeBuffers[i].Get();
			prepBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			m_splatPrecomputeDescriptorSets[i]->BindBuffer(7, prepBinding);
		}

		// Rebind preprocessed render descriptor sets
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
		{
			if (m_descriptorSetsPreprocessed[i])
			{
				if (m_sortedIndices)
				{
					rhi::BufferBinding indB{};
					indB.buffer = m_sortedIndices.Get();
					indB.type   = rhi::DescriptorType::STORAGE_BUFFER;
					m_descriptorSetsPreprocessed[i]->BindBuffer(1, indB);
				}

				rhi::BufferBinding prepB{};
				prepB.buffer = m_splatPrecomputeBuffers[i].Get();
				prepB.type   = rhi::DescriptorType::STORAGE_BUFFER;
				m_descriptorSetsPreprocessed[i]->BindBuffer(2, prepB);
			}
		}
	}
	else if (newSplatCount == 0)
	{
		for (auto &buf : m_splatPrecomputeBuffers)
			buf = nullptr;
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
			// Restore sort method and shader variant for GPU backend
			m_backend->SetSortMethod(m_currentSortMethod);
			m_backend->SetShaderVariant(m_currentShaderVariant);

			// Reinitialize indirect dispatch and rebind precompute sortPairsB
			auto *gpuBackendNew = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
			auto *sorterNew     = gpuBackendNew ? gpuBackendNew->GetSorter() : nullptr;
			if (sorterNew)
			{
				if (m_indirectArgsBuffers[0])
				{
					container::vector<rhi::IRHIBuffer *> indirectPtrs;
					for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
						indirectPtrs.push_back(m_indirectArgsBuffers[i].Get());
					sorterNew->Initialize(0, {}, 0, {indirectPtrs.data(), indirectPtrs.size()});
				}

				if (m_splatPrecomputeDescriptorSets[0])
				{
					auto bufInfo = sorterNew->GetBufferInfo();
					if (bufInfo.sortPairsB)
					{
						for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
						{
							rhi::BufferBinding sortPairsBinding{};
							sortPairsBinding.buffer = bufInfo.sortPairsB.Get();
							sortPairsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
							m_splatPrecomputeDescriptorSets[i]->BindBuffer(9, sortPairsBinding);
						}
					}
				}
			}
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

	ResetAsyncPipelineState();

	// Reinitialize compute rasterizer with new splat count
	if (m_computeRasterizer)
	{
		int fbWidth, fbHeight;
		m_deviceManager->GetPlatformAdapter()->GetFramebufferSize(&fbWidth, &fbHeight);

		// Recreate with new max splat count
		m_computeRasterizer = container::make_unique<engine::ComputeSplatRasterizer>(device, vfs);
		m_computeRasterizer->Initialize(static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight), newSplatCount);

		m_computeRasterizer->SetProfilingCallbacks(
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputePreprocessTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeSortTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeRangesTimestamp(cmd, begin); },
		    [this](rhi::IRHICommandList *cmd, bool begin) { RecordComputeRasterTimestamp(cmd, begin); });

		m_computeRasterizer->SetSortMethod(m_currentSortMethod);
		m_computeRasterizer->SetShaderVariant(m_currentShaderVariant);

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

	// Resize transmittance culling resources
	ResizeTransmCullingResources(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

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

	// Initialize pipeline tracking for each frame slot
	m_frameRasterizationPipelines.resize(m_gpuProfilingFrameLatency, RasterizationPipelineType::HardwareRaster);
	m_frameSortAsyncEnabled.resize(m_gpuProfilingFrameLatency, false);
	m_frameSplatPrecomputeEnabled.resize(m_gpuProfilingFrameLatency, false);

	rhi::QueryPoolDesc timestampDesc{};
	timestampDesc.queryType  = rhi::QueryType::TIMESTAMP;
	timestampDesc.queryCount = TIMESTAMPS_PER_FRAME * m_gpuProfilingFrameLatency;
	m_timestampQueryPool     = device->CreateQueryPool(timestampDesc);

	if (!m_timestampQueryPool)
	{
		LOG_WARNING("Failed to create timestamp query pool, GPU timing disabled");
	}

	// Separate query pool for async compute precompute timing
	rhi::QueryPoolDesc asyncPrecomputeDesc{};
	asyncPrecomputeDesc.queryType  = rhi::QueryType::TIMESTAMP;
	asyncPrecomputeDesc.queryCount = 2 * MAX_FRAMES_IN_FLIGHT;
	m_asyncPrecomputeQueryPool     = device->CreateQueryPool(asyncPrecomputeDesc);

	if (!m_asyncPrecomputeQueryPool)
	{
		LOG_WARNING("Failed to create async precompute query pool");
	}

	rhi::QueryPoolDesc statsDesc{};
	statsDesc.queryType       = rhi::QueryType::PIPELINE_STATISTICS;
	statsDesc.queryCount      = m_gpuProfilingFrameLatency;
	statsDesc.statisticsFlags = rhi::PipelineStatisticFlags::VERTEX_SHADER_INVOCATIONS |
	                            rhi::PipelineStatisticFlags::FRAGMENT_SHADER_INVOCATIONS;
	m_pipelineStatsQueryPool = device->CreateQueryPool(statsDesc);

	if (!m_pipelineStatsQueryPool)
	{
		LOG_WARNING("Failed to create pipeline stats query pool, fragment stats disabled");
	}

	LOG_INFO("GPU Profiling initialized: {} timestamp queries, {} stats queries",
	         timestampDesc.queryCount, statsDesc.queryCount);
}

void HybridSplatRendererApp::ShutdownGpuProfiling()
{
	m_timestampQueryPool       = nullptr;
	m_asyncPrecomputeQueryPool = nullptr;
	m_pipelineStatsQueryPool   = nullptr;
	m_pipelineStatsQueryActive = false;
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

	// Record state for this frame
	m_frameRasterizationPipelines[frameSlot] = m_rasterizationPipelineType;
	m_frameSortAsyncEnabled[frameSlot]       = m_asyncComputeEnabled;
	m_frameSplatPrecomputeEnabled[frameSlot] = m_splatPrecomputeEnabled && m_splatPrecomputePipeline && m_currentBackendType == BackendType::GPU;

	// Reset timestamp and pipeline stats queries for this frame
	cmdList->ResetQueryPool(m_timestampQueryPool.Get(), timestampOffset, TIMESTAMPS_PER_FRAME);
	if (m_pipelineStatsQueryPool)
	{
		cmdList->ResetQueryPool(m_pipelineStatsQueryPool.Get(), frameSlot, 1);
	}
}

void HybridSplatRendererApp::RecordPrecomputeTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_HW_PRECOMPUTE_BEGIN : TIMESTAMP_HW_PRECOMPUTE_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::RecordSortTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_HW_SORT_BEGIN : TIMESTAMP_HW_SORT_END);

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

	// Use different timestamp indices based on active rendering pipeline
	uint32_t queryIndex;
	if (m_rasterizationPipelineType == RasterizationPipelineType::ComputeRaster)
	{
		queryIndex = timestampOffset + (begin ? TIMESTAMP_COMPUTE_RENDER_BEGIN : TIMESTAMP_COMPUTE_RENDER_END);
	}
	else
	{
		queryIndex = timestampOffset + (begin ? TIMESTAMP_HW_RENDER_BEGIN : TIMESTAMP_HW_RENDER_END);
	}

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::RenderTarget);
}

void HybridSplatRendererApp::RecordComputePreprocessTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_COMPUTE_PREPROCESS_BEGIN : TIMESTAMP_COMPUTE_PREPROCESS_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::RecordComputeSortTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_COMPUTE_SORT_BEGIN : TIMESTAMP_COMPUTE_SORT_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::RecordComputeRangesTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_COMPUTE_RANGES_BEGIN : TIMESTAMP_COMPUTE_RANGES_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::RecordComputeRasterTimestamp(rhi::IRHICommandList *cmdList, bool begin)
{
	if (!m_profilingEnabled || !m_timestampQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot       = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	uint32_t timestampOffset = frameSlot * TIMESTAMPS_PER_FRAME;
	uint32_t queryIndex      = timestampOffset + (begin ? TIMESTAMP_COMPUTE_RASTER_BEGIN : TIMESTAMP_COMPUTE_RASTER_END);

	cmdList->WriteTimestamp(m_timestampQueryPool.Get(), queryIndex, rhi::StageMask::ComputeShader);
}

void HybridSplatRendererApp::BeginPipelineStatsQuery(rhi::IRHICommandList *cmdList)
{
	if (!m_profilingEnabled || !m_pipelineStatsQueryPool || m_profilingJustEnabled)
	{
		return;
	}

	uint32_t frameSlot = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	cmdList->BeginQuery(m_pipelineStatsQueryPool.Get(), frameSlot);
	m_pipelineStatsQueryActive = true;
}

void HybridSplatRendererApp::EndPipelineStatsQuery(rhi::IRHICommandList *cmdList)
{
	// Must end query if one was started, even if profiling was disabled mid-frame
	if (!m_pipelineStatsQueryActive || !m_pipelineStatsQueryPool)
	{
		return;
	}

	uint32_t frameSlot = m_profilingFrameIndex % m_gpuProfilingFrameLatency;
	cmdList->EndQuery(m_pipelineStatsQueryPool.Get(), frameSlot);
	m_pipelineStatsQueryActive = false;
}

void HybridSplatRendererApp::ReadAsyncPrecomputeTiming(uint32_t slotIndex)
{
	if (!m_asyncPrecomputeQueryPool || !m_profilingEnabled)
	{
		return;
	}

	rhi::IRHIDevice *device = m_deviceManager->GetDevice();

	uint64_t asyncTs[2] = {};
	bool     valid      = device->GetQueryPoolResults(
        m_asyncPrecomputeQueryPool.Get(),
        slotIndex * 2, 2,
        asyncTs, 2 * sizeof(uint64_t), sizeof(uint64_t),
        rhi::QueryResultFlags::NONE);

	if (valid && asyncTs[1] > asyncTs[0])
	{
		double ticks                = static_cast<double>(asyncTs[1] - asyncTs[0]);
		m_lastAsyncPrecomputeTimeMs = (ticks * m_timestampPeriod) / 1000000.0;
	}
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

	GpuTimingResults          results{};
	RasterizationPipelineType pipelineType    = m_frameRasterizationPipelines[readFrameIndex];
	bool                      sortAsyncWasOn  = m_frameSortAsyncEnabled[readFrameIndex];
	bool                      precomputeWasOn = m_frameSplatPrecomputeEnabled[readFrameIndex];

	// Read only the timestamps that were written for the active pipeline
	uint64_t timestamps[TIMESTAMPS_PER_FRAME] = {};
	bool     timestampsValid                  = false;

	if (pipelineType == RasterizationPipelineType::HardwareRaster)
	{
		// Hardware rasterization pipeline: read only the timestamps that were written
		if (sortAsyncWasOn)
		{
			// Async compute enabled: only render timestamps were written on graphics queue
			timestampsValid = device->GetQueryPoolResults(
			    m_timestampQueryPool.Get(),
			    timestampOffset + TIMESTAMP_HW_RENDER_BEGIN,
			    2,        // Read 2 timestamps (TIMESTAMP_HW_RENDER_BEGIN through TIMESTAMP_HW_RENDER_END)
			    &timestamps[TIMESTAMP_HW_RENDER_BEGIN],
			    2 * sizeof(uint64_t),
			    sizeof(uint64_t),
			    rhi::QueryResultFlags::NONE);
		}
		else if (precomputeWasOn)
		{
			// Single-queue with precompute: precompute(0-1) + sort(2-3) + render(4-5) timestamps
			timestampsValid = device->GetQueryPoolResults(
			    m_timestampQueryPool.Get(),
			    timestampOffset + TIMESTAMP_HW_PRECOMPUTE_BEGIN,
			    6,        // Read 6 timestamps (PRECOMPUTE_BEGIN through RENDER_END)
			    &timestamps[TIMESTAMP_HW_PRECOMPUTE_BEGIN],
			    6 * sizeof(uint64_t),
			    sizeof(uint64_t),
			    rhi::QueryResultFlags::NONE);
		}
		else
		{
			// Normal mode (no precompute): sort(2-3) + render(4-5) timestamps
			timestampsValid = device->GetQueryPoolResults(
			    m_timestampQueryPool.Get(),
			    timestampOffset + TIMESTAMP_HW_SORT_BEGIN,
			    4,        // Read 4 timestamps (SORT_BEGIN through RENDER_END)
			    &timestamps[TIMESTAMP_HW_SORT_BEGIN],
			    4 * sizeof(uint64_t),
			    sizeof(uint64_t),
			    rhi::QueryResultFlags::NONE);
		}
	}
	else        // RasterizationPipelineType::ComputeRaster
	{
		// Compute rasterization pipeline: read timestamps 6-15
		timestampsValid = device->GetQueryPoolResults(
		    m_timestampQueryPool.Get(),
		    timestampOffset + TIMESTAMP_COMPUTE_PREPROCESS_BEGIN,
		    10,        // Read 10 timestamps (COMPUTE_PREPROCESS_BEGIN through COMPUTE_RENDER_END)
		    &timestamps[TIMESTAMP_COMPUTE_PREPROCESS_BEGIN],
		    10 * sizeof(uint64_t),
		    sizeof(uint64_t),
		    rhi::QueryResultFlags::NONE);
	}

	results.valid = timestampsValid;

	if (timestampsValid)
	{
		if (pipelineType == RasterizationPipelineType::ComputeRaster)
		{
			// Compute rasterization pipeline: preprocess, sort, identify ranges, rasterize
			double preprocessTicks   = static_cast<double>(timestamps[TIMESTAMP_COMPUTE_PREPROCESS_END] - timestamps[TIMESTAMP_COMPUTE_PREPROCESS_BEGIN]);
			results.preprocessTimeMs = (preprocessTicks * m_timestampPeriod) / 1000000.0;

			double sortTicks          = static_cast<double>(timestamps[TIMESTAMP_COMPUTE_SORT_END] - timestamps[TIMESTAMP_COMPUTE_SORT_BEGIN]);
			results.computeSortTimeMs = (sortTicks * m_timestampPeriod) / 1000000.0;

			double rangesTicks   = static_cast<double>(timestamps[TIMESTAMP_COMPUTE_RANGES_END] - timestamps[TIMESTAMP_COMPUTE_RANGES_BEGIN]);
			results.rangesTimeMs = (rangesTicks * m_timestampPeriod) / 1000000.0;

			double rasterTicks   = static_cast<double>(timestamps[TIMESTAMP_COMPUTE_RASTER_END] - timestamps[TIMESTAMP_COMPUTE_RASTER_BEGIN]);
			results.rasterTimeMs = (rasterTicks * m_timestampPeriod) / 1000000.0;

			// Total render time
			double renderTicks   = static_cast<double>(timestamps[TIMESTAMP_COMPUTE_RENDER_END] - timestamps[TIMESTAMP_COMPUTE_RENDER_BEGIN]);
			results.renderTimeMs = (renderTicks * m_timestampPeriod) / 1000000.0;
		}
		else
		{
			// Hardware rasterization pipeline
			bool sortTimestampsWritten = (m_sortingEnabled || precomputeWasOn) && m_currentBackendType == BackendType::GPU && !sortAsyncWasOn;

			// Precompute timing (single-queue only, async uses dedicated pool)
			if (sortTimestampsWritten && precomputeWasOn && timestamps[TIMESTAMP_HW_PRECOMPUTE_BEGIN] != 0)
			{
				double precomputeTicks   = static_cast<double>(timestamps[TIMESTAMP_HW_PRECOMPUTE_END] - timestamps[TIMESTAMP_HW_PRECOMPUTE_BEGIN]);
				results.precomputeTimeMs = (precomputeTicks * m_timestampPeriod) / 1000000.0;
			}

			// Sort timing
			if (sortTimestampsWritten && timestamps[TIMESTAMP_HW_SORT_BEGIN] != 0)
			{
				// Sort happened on graphics queue
				double sortTicks   = static_cast<double>(timestamps[TIMESTAMP_HW_SORT_END] - timestamps[TIMESTAMP_HW_SORT_BEGIN]);
				results.sortTimeMs = (sortTicks * m_timestampPeriod) / 1000000.0;
			}
			else if (m_backend && (m_sortingEnabled || precomputeWasOn))
			{
				// Sort happened on CPU or async compute queue: get timing from backend metrics
				auto metrics       = m_backend->GetMetrics();
				results.sortTimeMs = metrics.sortDurationMs;

				// Precompute timing from async compute queue
				if (sortAsyncWasOn && precomputeWasOn)
				{
					results.precomputeTimeMs = m_lastAsyncPrecomputeTimeMs;
				}
			}

			if (timestamps[TIMESTAMP_HW_RENDER_BEGIN] != 0)
			{
				double renderTicks   = static_cast<double>(timestamps[TIMESTAMP_HW_RENDER_END] - timestamps[TIMESTAMP_HW_RENDER_BEGIN]);
				results.renderTimeMs = (renderTicks * m_timestampPeriod) / 1000000.0;
			}
		}
	}

	// Read pipeline statistics if available
	if (m_pipelineStatsQueryPool)
	{
		struct PipelineStats
		{
			uint64_t vertexInvocations;
			uint64_t fragmentInvocations;
		} stats;

		bool statsValid = device->GetQueryPoolResults(
		    m_pipelineStatsQueryPool.Get(),
		    readFrameIndex,
		    1,
		    &stats,
		    sizeof(stats),
		    sizeof(uint64_t),
		    rhi::QueryResultFlags::NONE);

		if (statsValid)
		{
			results.vertexInvocations   = stats.vertexInvocations;
			results.fragmentInvocations = stats.fragmentInvocations;
		}
	}

	if (results.valid)
	{
		m_currentGpuTiming = results;
		m_gpuTimingHistory.push_back(results);
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
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		const auto name = "m_frameUboBuffers[" + std::to_string(i) + "]";
		m_memoryTracker.AddBuffer(name.c_str(), m_frameUboBuffers[i], "App", MemoryType::HostVisible);
	}

	// GPU Sorter buffers (if GPU backend is active)
	if (m_currentBackendType == BackendType::GPU && m_backend)
	{
		auto *gpuBackend = dynamic_cast<engine::GpuSplatSortBackend *>(m_backend.get());
		if (gpuBackend && gpuBackend->GetSorter())
		{
			auto buffers = gpuBackend->GetSorterBufferInfo();
			m_memoryTracker.AddBuffer("splatDepths", buffers.splatDepths, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("splatIndicesOriginal", buffers.splatIndicesOriginal, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortPairsA", buffers.sortPairsA, "Sorter", MemoryType::DeviceLocal);
			m_memoryTracker.AddBuffer("sortPairsB", buffers.sortPairsB, "Sorter", MemoryType::DeviceLocal);
			for (uint32_t i = 0; i < buffers.outputBuffers.size(); ++i)
			{
				char name[32];
				snprintf(name, sizeof(name), "outputBuffer[%u]", i);
				m_memoryTracker.AddBuffer(name, buffers.outputBuffers[i], "Sorter", MemoryType::DeviceLocal);
			}
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
	uint32_t swapchainImageCount        = m_deviceManager->GetSwapchain()->GetImageCount();
	init_info.MinImageCount             = 2;
	init_info.ImageCount                = std::max(swapchainImageCount, MAX_FRAMES_IN_FLIGHT);
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
				m_profilingFrameIndex             = 0;
				m_asyncPrecomputeTimingWriteCount = 0;
				m_currentGpuTiming                = {};
				m_profilingJustEnabled            = true;        // Skip query writes for rest of this frame

				CollectAndLogBufferMemory();
			}
			ImGui::Separator();

			if (m_profilingEnabled)
			{
				if (m_rasterizationPipelineType == RasterizationPipelineType::ComputeRaster && m_currentGpuTiming.valid)
				{
					// Compute rasterization pipeline: detailed per-stage breakdown
					ImGui::Text("Preprocess:   %.3f ms", m_currentGpuTiming.preprocessTimeMs);
					ImGui::Text("Sort:         %.3f ms", m_currentGpuTiming.computeSortTimeMs);
					ImGui::Text("Ranges:       %.3f ms", m_currentGpuTiming.rangesTimeMs);
					ImGui::Text("Rasterize:    %.3f ms", m_currentGpuTiming.rasterTimeMs);
					ImGui::Text("Render Pass:  %.3f ms", m_currentGpuTiming.renderTimeMs);
				}
				else if (m_rasterizationPipelineType == RasterizationPipelineType::HardwareRaster)
				{
					// Hardware rasterization pipeline timings
					if (m_currentBackendType == BackendType::GPU && m_currentGpuTiming.valid)
					{
						// GPU backend
						double precomputeTime = m_currentGpuTiming.precomputeTimeMs;
						double sortTime       = m_sortingEnabled ? m_currentGpuTiming.sortTimeMs : 0.0;
						double totalTime      = precomputeTime + sortTime + m_currentGpuTiming.renderTimeMs;
						if (precomputeTime > 0.0)
						{
							ImGui::Text("Precompute:   %.3f ms", precomputeTime);
						}
						ImGui::Text("Sort Pass:    %.3f ms", sortTime);
						ImGui::Text("Render Pass:  %.3f ms", m_currentGpuTiming.renderTimeMs);
						ImGui::Text("Total:        %.3f ms", totalTime);
					}
					else if (m_currentBackendType == BackendType::CPU && m_currentGpuTiming.valid)
					{
						// CPU backend
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

						ImGui::Text("Sort Time:    %.2f ms", sortTime);
						if (uploadTime > 0.0f)
						{
							ImGui::Text("Upload Time:  %.2f ms", uploadTime);
						}

						totalTime += static_cast<float>(m_currentGpuTiming.renderTimeMs);
						ImGui::Text("Render Pass:  %.3f ms", m_currentGpuTiming.renderTimeMs);
						ImGui::Text("Total:        %.3f ms", totalTime);
					}
				}

				// Pipeline statistics (hardware rasterization pipeline only)
				if (m_rasterizationPipelineType == RasterizationPipelineType::HardwareRaster && m_pipelineStatsQueryPool && m_scene)
				{
					uint64_t vertexInvocations   = m_currentGpuTiming.vertexInvocations;
					uint64_t fragmentInvocations = m_currentGpuTiming.fragmentInvocations;
					uint32_t splatCount          = m_scene->GetTotalSplatCount();
					uint32_t pixelCount          = m_deviceManager->GetSwapchain()->GetBackBufferView(0)->GetWidth() *
					                      m_deviceManager->GetSwapchain()->GetBackBufferView(0)->GetHeight();

					float shadingLoad       = (pixelCount > 0) ? static_cast<float>(fragmentInvocations) / static_cast<float>(pixelCount) : 0.0f;
					float fragmentsPerSplat = (splatCount > 0) ? static_cast<float>(fragmentInvocations) / static_cast<float>(splatCount) : 0.0f;
					float verticesPerSplat  = (splatCount > 0) ? static_cast<float>(vertexInvocations) / static_cast<float>(splatCount) : 0.0f;

					ImGui::Text("Shading Load: %.2fx", shadingLoad);
					ImGui::SameLine();
					ImGui::TextDisabled("(?)");
					if (ImGui::IsItemHovered())
					{
						ImGui::BeginTooltip();
						ImGui::Text("VS Invocations: %llu", static_cast<unsigned long long>(vertexInvocations));
						ImGui::Text("PS Invocations: %llu", static_cast<unsigned long long>(fragmentInvocations));
						ImGui::Separator();
						ImGui::Text("Shading Load: PS invocations / Screen Pixels");
						ImGui::Text("Includes discarded fragments (~1.27x inflation from quad corners)");
						ImGui::Text("Vertices/Splat: %.1f (ideal: 4.0)", verticesPerSplat);
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

		// Rasterization Pipeline Selection
		ImGui::Text("Rasterization Pipeline:");
		int         pipelineIndex = static_cast<int>(m_rasterizationPipelineType);
		const char *pipelines[]   = {"Hardware Rasterization", "Compute Rasterization"};

		// Disable compute option if rasterizer is not initialized
		bool canUseCompute = m_computeRasterizer && m_computeRasterizer->IsInitialized();
		if (!canUseCompute && pipelineIndex == 1)
		{
			pipelineIndex               = 0;
			m_rasterizationPipelineType = RasterizationPipelineType::HardwareRaster;
		}

		if (ImGui::Combo("##RasterizationPipeline", &pipelineIndex, pipelines, 2))
		{
			m_rasterizationPipelineType = static_cast<RasterizationPipelineType>(pipelineIndex);
			LOG_INFO("Rasterization pipeline: {}", pipelines[pipelineIndex]);

			// When switching to compute rasterization, schedule async compute disable at frame boundary
			if (m_rasterizationPipelineType == RasterizationPipelineType::ComputeRaster && m_asyncComputeEnabled)
			{
				m_pendingOps.asyncComputeToggle = PendingOperations::AsyncComputeToggle{false};
				LOG_INFO("Async compute disable scheduled (not used with compute rasterization)");
			}
		}

		ImGui::Spacing();

		// Hardware rasterization pipeline controls
		if (m_rasterizationPipelineType == RasterizationPipelineType::HardwareRaster)
		{
			// Splat precompute toggle
			if (m_splatPrecomputePipeline)
			{
				bool canUsePreprocess = (m_currentBackendType == BackendType::GPU);
				if (!canUsePreprocess)
					ImGui::BeginDisabled();
				if (ImGui::Checkbox("Splat Precompute", &m_splatPrecomputeEnabled))
				{
					m_splatPrecomputeAsyncWarmup = 0;
					LOG_INFO("Splat precompute {}",
					         m_splatPrecomputeEnabled ? "enabled" : "disabled");
				}
				if (!canUsePreprocess)
					ImGui::EndDisabled();
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Precompute per-splat data in compute shader.");
					ImGui::TextUnformatted("Reduces vertex shader work from 4x to 1x per splat.");
					ImGui::EndTooltip();
				}
			}

			// Sorting enabled toggle (always on when precompute is active)
			bool precomputeForcesSorting = m_splatPrecomputeEnabled && m_splatPrecomputePipeline && m_currentBackendType == BackendType::GPU;
			if (precomputeForcesSorting)
				ImGui::BeginDisabled();
			bool sortEnabled = m_sortingEnabled || precomputeForcesSorting;
			if (ImGui::Checkbox("Enable Sorting", &sortEnabled))
			{
				m_pendingOps.sortingToggle = PendingOperations::SortingToggle{sortEnabled};
			}
			if (precomputeForcesSorting)
				ImGui::EndDisabled();

			// Transmittance culling toggle
			bool tcEnabled = m_transmittanceCullingEnabled;
			if (ImGui::Checkbox("Transmittance Culling", &tcEnabled))
			{
				m_pendingOps.transmittanceCullingToggle = PendingOperations::TransmittanceCullingToggle{tcEnabled};
				LOG_INFO("Transmittance culling toggle scheduled: {}", tcEnabled ? "enable" : "disable");
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Enable transmittance-based rendering.");
				ImGui::TextUnformatted("Renders near-to-far with accumulation buffer,");
				ImGui::TextUnformatted("then composites over background.");
				ImGui::EndTooltip();
			}

			// Chunked culling controls
			if (m_transmittanceCullingEnabled)
			{
				ImGui::Indent();

				// Chunk count slider
				int chunkCount = static_cast<int>(m_chunkCount);
				if (ImGui::SliderInt("Chunk Count", &chunkCount, 1, 8))
				{
					m_chunkCount = static_cast<uint32_t>(chunkCount);
					LOG_INFO("Chunk count set to {}", m_chunkCount);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Number of chunks to split splat rendering into.");
					ImGui::EndTooltip();
				}

				// Saturation threshold slider
				if (ImGui::SliderFloat("Saturation Threshold", &m_saturationThreshold, 0.001f, 1.001f, "%.3f"))
				{
					LOG_INFO("Saturation threshold set to {:.3f}", m_saturationThreshold);
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted("Transmittance below this value is considered saturated.");
					ImGui::TextUnformatted("0.001 = 99.9% opaque. Lower values = less culling.");
					ImGui::EndTooltip();
				}

				ImGui::Unindent();
			}

			ImGui::Spacing();

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
						m_currentShaderVariant = currentVariant;
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
					if (m_asyncComputeEnabled)
					{
						ImGui::SameLine();
						ImGui::Text("(Frames in flight: %u)", MAX_FRAMES_IN_FLIGHT);
					}
				}
			}
		}
		// Compute rasterization pipeline controls
		else if (m_rasterizationPipelineType == RasterizationPipelineType::ComputeRaster && canUseCompute)
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

			ImGui::Spacing();

			// Sort method selection
			int         currentMethod = m_computeRasterizer->GetSortMethod();
			const char *methods[]     = {"Prescan Radix Sort", "Integrated Scan Radix Sort"};
			if (ImGui::Combo("Sort Method", &currentMethod, methods, 2))
			{
				m_currentSortMethod = currentMethod;
				m_computeRasterizer->SetSortMethod(currentMethod);
				LOG_INFO("Compute rasterization pipeline: Switched to {} radix sort method", methods[currentMethod]);
			}

			// Shader variant selection
			int         currentVariant = m_computeRasterizer->GetShaderVariant();
			const char *variants[]     = {"Portable", "SubgroupOptimized"};
			if (ImGui::Combo("Shader Variant", &currentVariant, variants, 2))
			{
				m_currentShaderVariant = currentVariant;
				m_computeRasterizer->SetShaderVariant(currentVariant);
				LOG_INFO("Compute rasterization pipeline: Switched to {} shader variant", variants[currentVariant]);
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

			ImGui::Spacing();

			// CPU sort debug toggle
			bool cpuSort = m_computeRasterizer->IsCPUSortDebugEnabled();
			if (ImGui::Checkbox("CPU Sort (Debug)", &cpuSort))
			{
				m_computeRasterizer->SetCPUSortDebug(cpuSort);
				LOG_INFO("CPU sort debug {}", cpuSort ? "enabled" : "disabled");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Bypass GPU radix sort and use CPU std::sort");
				ImGui::EndTooltip();
			}

#ifdef ENABLE_SORT_VERIFICATION
			// Verify sort order button
			if (ImGui::Button("Verify Sort Order"))
			{
				bool sortOk = m_computeRasterizer->VerifySortOrder();
				LOG_INFO("Tile sort verification: {}", sortOk ? "PASSED" : "FAILED");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::Text("Reads back sorted tile keys from GPU");
				ImGui::Text("and verifies ascending order (blocks GPU)");
				ImGui::EndTooltip();
			}
#endif
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

#ifdef ENABLE_SORT_VERIFICATION
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
#endif

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
#	ifdef ENABLE_SORT_VERIFICATION
		ImGui::BulletText("V: Verify sorting");
		ImGui::BulletText("X: Toggle cross-backend verify");
#	endif
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

void HybridSplatRendererApp::CreateTransmCullingResources()
{
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	m_transmCullingFragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster_transm_culling.frag.spv", rhi::ShaderStage::FRAGMENT);
	m_fullscreenVertexShader      = m_shaderFactory->getOrCreateShader("shaders/compiled/fullscreen.vert.spv", rhi::ShaderStage::VERTEX);
	m_compositeFragmentShader     = m_shaderFactory->getOrCreateShader("shaders/compiled/composite.frag.spv", rhi::ShaderStage::FRAGMENT);
	m_stencilUpdateFragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/transm_stencil_update.frag.spv", rhi::ShaderStage::FRAGMENT);

	// Create per-frame accumulation and depth/stencil textures for multi-frame-in-flight
	int fbWidth, fbHeight;
	m_deviceManager->GetPlatformAdapter()->GetFramebufferSize(&fbWidth, &fbHeight);

	// Create sampler for composite pass (shared)
	rhi::SamplerDesc samplerDesc{};
	samplerDesc.minFilter    = rhi::FilterMode::LINEAR;
	samplerDesc.magFilter    = rhi::FilterMode::LINEAR;
	samplerDesc.addressModeU = rhi::SamplerAddressMode::CLAMP_TO_EDGE;
	samplerDesc.addressModeV = rhi::SamplerAddressMode::CLAMP_TO_EDGE;
	samplerDesc.addressModeW = rhi::SamplerAddressMode::CLAMP_TO_EDGE;
	m_compositeSampler       = device->CreateSampler(samplerDesc);

	// Create composite descriptor set layout (shared)
	rhi::DescriptorSetLayoutDesc compositeLayoutDesc{};
	compositeLayoutDesc.bindings = {
	    {0, rhi::DescriptorType::COMBINED_IMAGE_SAMPLER, 1, rhi::ShaderStageFlags::FRAGMENT},
	};
	m_compositeDescriptorSetLayout = device->CreateDescriptorSetLayout(compositeLayoutDesc);

	// Create descriptor layout for input attachment (shared)
	rhi::DescriptorSetLayoutDesc stencilLayoutDesc{};
	stencilLayoutDesc.bindings.push_back({0,
	                                      rhi::DescriptorType::INPUT_ATTACHMENT,
	                                      1,
	                                      rhi::ShaderStageFlags::FRAGMENT});
	m_stencilUpdateDescriptorLayout = device->CreateDescriptorSetLayout(stencilLayoutDesc);

	// Create per-frame textures, views, and descriptor sets
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		// Accumulation texture
		rhi::TextureDesc accumDesc{};
		accumDesc.width             = static_cast<uint32_t>(fbWidth);
		accumDesc.height            = static_cast<uint32_t>(fbHeight);
		accumDesc.format            = swapchain->GetBackBuffer(0)->GetFormat();
		accumDesc.isRenderTarget    = true;
		accumDesc.isInputAttachment = true;
		m_accumTextures[i]          = device->CreateTexture(accumDesc);

		rhi::TextureViewDesc accumViewDesc{};
		accumViewDesc.texture    = m_accumTextures[i].Get();
		accumViewDesc.aspectMask = rhi::TextureAspect::COLOR;
		m_accumTextureViews[i]   = device->CreateTextureView(accumViewDesc);

		// Composite descriptor set (per-frame, references this frame's accumTexture)
		m_compositeDescriptorSets[i] = device->CreateDescriptorSet(m_compositeDescriptorSetLayout.Get());
		rhi::TextureBinding accumBinding{};
		accumBinding.texture = m_accumTextures[i].Get();
		accumBinding.sampler = m_compositeSampler.Get();
		accumBinding.type    = rhi::DescriptorType::COMBINED_IMAGE_SAMPLER;
		m_compositeDescriptorSets[i]->BindTexture(0, accumBinding);

		// Depth/stencil texture
		rhi::TextureDesc dsDesc{};
		dsDesc.width              = static_cast<uint32_t>(fbWidth);
		dsDesc.height             = static_cast<uint32_t>(fbHeight);
		dsDesc.format             = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
		dsDesc.isDepthStencil     = true;
		m_depthStencilTextures[i] = device->CreateTexture(dsDesc);

		rhi::TextureViewDesc dsViewDesc{};
		dsViewDesc.texture     = m_depthStencilTextures[i].Get();
		dsViewDesc.aspectMask  = rhi::TextureAspect::DEPTH | rhi::TextureAspect::STENCIL;
		m_depthStencilViews[i] = device->CreateTextureView(dsViewDesc);

		// Stencil update descriptor set (per-frame, references this frame's accumTexture as input attachment)
		m_stencilUpdateDescriptorSets[i] = device->CreateDescriptorSet(m_stencilUpdateDescriptorLayout.Get());
		rhi::TextureBinding inputBinding{};
		inputBinding.texture = m_accumTextures[i].Get();
		inputBinding.type    = rhi::DescriptorType::INPUT_ATTACHMENT;
		inputBinding.layout  = rhi::ImageLayout::RENDERING_LOCAL_READ;
		m_stencilUpdateDescriptorSets[i]->BindTexture(0, inputBinding);
	}

	// Create composite pipeline (shared)
	rhi::GraphicsPipelineDesc compositePipelineDesc{};
	compositePipelineDesc.vertexShader                = m_fullscreenVertexShader.Get();
	compositePipelineDesc.fragmentShader              = m_compositeFragmentShader.Get();
	compositePipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_LIST;
	compositePipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	compositePipelineDesc.colorBlendAttachments.resize(1);
	compositePipelineDesc.colorBlendAttachments[0].blendEnable = false;
	compositePipelineDesc.targetSignature.colorFormats         = {swapchain->GetBackBuffer(0)->GetFormat()};
	compositePipelineDesc.descriptorSetLayouts                 = {m_compositeDescriptorSetLayout.Get()};
	compositePipelineDesc.pushConstantRanges                   = {{rhi::ShaderStageFlags::FRAGMENT, 0, sizeof(float) * 4}};
	m_compositePipeline                                        = device->CreateGraphicsPipeline(compositePipelineDesc);

	// Create stencil update pipeline
	rhi::GraphicsPipelineDesc stencilPipelineDesc{};
	stencilPipelineDesc.vertexShader   = m_fullscreenVertexShader.Get();
	stencilPipelineDesc.fragmentShader = m_stencilUpdateFragmentShader.Get();
	stencilPipelineDesc.topology       = rhi::PrimitiveTopology::TRIANGLE_LIST;
	stencilPipelineDesc.colorBlendAttachments.resize(1);
	stencilPipelineDesc.colorBlendAttachments[0].colorWriteMask = 0;
	stencilPipelineDesc.depthStencilState.stencilTestEnable     = true;
	stencilPipelineDesc.depthStencilState.front.compareOp       = rhi::CompareOp::ALWAYS;
	stencilPipelineDesc.depthStencilState.front.passOp          = rhi::StencilOp::REPLACE;
	stencilPipelineDesc.depthStencilState.front.failOp          = rhi::StencilOp::KEEP;
	stencilPipelineDesc.depthStencilState.front.depthFailOp     = rhi::StencilOp::KEEP;
	stencilPipelineDesc.depthStencilState.front.reference       = 1;
	stencilPipelineDesc.depthStencilState.front.writeMask       = 0xFF;
	stencilPipelineDesc.depthStencilState.front.compareMask     = 0xFF;
	stencilPipelineDesc.depthStencilState.back                  = stencilPipelineDesc.depthStencilState.front;

	stencilPipelineDesc.targetSignature.colorFormats = {swapchain->GetBackBuffer(0)->GetFormat()};
	stencilPipelineDesc.targetSignature.depthFormat  = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
	stencilPipelineDesc.descriptorSetLayouts         = {m_stencilUpdateDescriptorLayout.Get()};
	stencilPipelineDesc.pushConstantRanges.push_back({
	    rhi::ShaderStageFlags::FRAGMENT,
	    0,
	    sizeof(float)        // transmittanceThreshold
	});

	m_stencilUpdatePipeline = device->CreateGraphicsPipeline(stencilPipelineDesc);

	// Create splat pipeline (front-to-back blending + stencil test to skip saturated pixels)
	rhi::GraphicsPipelineDesc splatPipelineDesc{};
	splatPipelineDesc.vertexShader                = m_vertexShader.Get();
	splatPipelineDesc.fragmentShader              = m_transmCullingFragmentShader.Get();
	splatPipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_STRIP;
	splatPipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	splatPipelineDesc.colorBlendAttachments.resize(1);
	splatPipelineDesc.colorBlendAttachments[0].blendEnable         = true;
	splatPipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::DST_ALPHA;
	splatPipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE;
	splatPipelineDesc.colorBlendAttachments[0].colorBlendOp        = rhi::BlendOp::ADD;
	splatPipelineDesc.colorBlendAttachments[0].srcAlphaBlendFactor = rhi::BlendFactor::ZERO;
	splatPipelineDesc.colorBlendAttachments[0].dstAlphaBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
	splatPipelineDesc.colorBlendAttachments[0].alphaBlendOp        = rhi::BlendOp::ADD;

	// Skip fragments where stencil == 1 (saturated pixels)
	splatPipelineDesc.depthStencilState.stencilTestEnable = true;
	splatPipelineDesc.depthStencilState.front.compareOp   = rhi::CompareOp::NOT_EQUAL;
	splatPipelineDesc.depthStencilState.front.reference   = 1;
	splatPipelineDesc.depthStencilState.front.compareMask = 0xFF;
	splatPipelineDesc.depthStencilState.front.writeMask   = 0;
	splatPipelineDesc.depthStencilState.front.passOp      = rhi::StencilOp::KEEP;
	splatPipelineDesc.depthStencilState.front.failOp      = rhi::StencilOp::KEEP;
	splatPipelineDesc.depthStencilState.back              = splatPipelineDesc.depthStencilState.front;

	splatPipelineDesc.targetSignature.colorFormats = {swapchain->GetBackBuffer(0)->GetFormat()};
	splatPipelineDesc.targetSignature.depthFormat  = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
	splatPipelineDesc.descriptorSetLayouts         = {m_descriptorSetLayout.Get()};

	m_splatTransmCullingPipeline = device->CreateGraphicsPipeline(splatPipelineDesc);

	// Create preprocessed variant (same blend/stencil, but uses preprocessed vertex shader + layout)
	if (m_vertexShaderPreprocessed && m_descriptorSetLayoutPreprocessed)
	{
		rhi::GraphicsPipelineDesc prepSplatPipelineDesc = splatPipelineDesc;
		prepSplatPipelineDesc.vertexShader              = m_vertexShaderPreprocessed.Get();
		prepSplatPipelineDesc.descriptorSetLayouts      = {m_descriptorSetLayoutPreprocessed.Get()};
		m_splatTransmCullingPipelinePreprocessed        = device->CreateGraphicsPipeline(prepSplatPipelineDesc);
	}

	LOG_INFO("Transmittance culling resources created ({}x{}, {} chunks)", fbWidth, fbHeight, m_chunkCount);
}

void HybridSplatRendererApp::ResizeTransmCullingResources(uint32_t width, uint32_t height)
{
	if (!m_accumTextures[0])
	{
		return;
	}

	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		// Recreate accumulation texture at new size
		rhi::TextureDesc accumDesc{};
		accumDesc.width             = width;
		accumDesc.height            = height;
		accumDesc.format            = swapchain->GetBackBuffer(0)->GetFormat();
		accumDesc.isRenderTarget    = true;
		accumDesc.isInputAttachment = true;
		m_accumTextures[i]          = device->CreateTexture(accumDesc);

		rhi::TextureViewDesc accumViewDesc{};
		accumViewDesc.texture    = m_accumTextures[i].Get();
		accumViewDesc.aspectMask = rhi::TextureAspect::COLOR;
		m_accumTextureViews[i]   = device->CreateTextureView(accumViewDesc);

		// Rebind to composite descriptor set
		rhi::TextureBinding accumBinding{};
		accumBinding.texture = m_accumTextures[i].Get();
		accumBinding.sampler = m_compositeSampler.Get();
		accumBinding.type    = rhi::DescriptorType::COMBINED_IMAGE_SAMPLER;
		m_compositeDescriptorSets[i]->BindTexture(0, accumBinding);

		// Recreate depth/stencil texture at new size
		rhi::TextureDesc dsDesc{};
		dsDesc.width              = width;
		dsDesc.height             = height;
		dsDesc.format             = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
		dsDesc.isDepthStencil     = true;
		m_depthStencilTextures[i] = device->CreateTexture(dsDesc);

		rhi::TextureViewDesc dsViewDesc{};
		dsViewDesc.texture     = m_depthStencilTextures[i].Get();
		dsViewDesc.aspectMask  = rhi::TextureAspect::DEPTH | rhi::TextureAspect::STENCIL;
		m_depthStencilViews[i] = device->CreateTextureView(dsViewDesc);

		// Rebind input attachment
		if (m_stencilUpdateDescriptorSets[i])
		{
			rhi::TextureBinding inputBinding{};
			inputBinding.texture = m_accumTextures[i].Get();
			inputBinding.type    = rhi::DescriptorType::INPUT_ATTACHMENT;
			inputBinding.layout  = rhi::ImageLayout::RENDERING_LOCAL_READ;
			m_stencilUpdateDescriptorSets[i]->BindTexture(0, inputBinding);
		}
	}

	LOG_INFO("Transmittance culling resources resized to {}x{}", width, height);
}

void HybridSplatRendererApp::RenderTransmittanceCulling(
    rhi::IRHICommandList *cmdList,
    uint32_t              splatCount,
    uint32_t              width,
    uint32_t              height,
    uint32_t              imageIndex,
    uint32_t              frameIndex,
    rhi::IRHIBuffer      *indirectArgsBuffer)
{
	auto *swapchain = m_deviceManager->GetSwapchain();

	// When using GPU-driven pipeline, per-chunk draw commands are pre-written
	// at offset 100 in the indirect args buffer by PrepareIndirectArgs shader
	// Otherwise, compute chunk sizes from total splatCount on the CPU
	uint32_t splatsPerChunk = (splatCount + m_chunkCount - 1) / m_chunkCount;

	// Barrier: Transition accumulation texture to local read layout
	rhi::TextureTransition accumTransition{};
	accumTransition.texture = m_accumTextures[frameIndex].Get();
	accumTransition.before  = rhi::ResourceState::Undefined;
	accumTransition.after   = rhi::ResourceState::RenderingLocalRead;

	rhi::TextureTransition dsTransition{};
	dsTransition.texture = m_depthStencilTextures[frameIndex].Get();
	dsTransition.before  = rhi::ResourceState::Undefined;
	dsTransition.after   = rhi::ResourceState::DepthStencilWrite;

	rhi::TextureTransition transitions[] = {accumTransition, dsTransition};
	cmdList->Barrier(rhi::PipelineScope::Graphics, rhi::PipelineScope::Graphics,
	                 {}, {transitions, 2}, {});

	rhi::RenderingInfo renderInfo{};
	renderInfo.colorAttachments.resize(1);
	renderInfo.colorAttachments[0].view                = m_accumTextureViews[frameIndex].Get();
	renderInfo.colorAttachments[0].loadOp              = rhi::LoadOp::CLEAR;
	renderInfo.colorAttachments[0].storeOp             = rhi::StoreOp::STORE;
	renderInfo.colorAttachments[0].clearValue.color[0] = 0.0f;        // C_acc = 0
	renderInfo.colorAttachments[0].clearValue.color[1] = 0.0f;
	renderInfo.colorAttachments[0].clearValue.color[2] = 0.0f;
	renderInfo.colorAttachments[0].clearValue.color[3] = 1.0f;        // T = 1 (fully transparent)
	renderInfo.colorAttachments[0].layout              = rhi::ImageLayout::RENDERING_LOCAL_READ;

	renderInfo.depthStencilAttachment.view                            = m_depthStencilViews[frameIndex].Get();
	renderInfo.depthStencilAttachment.depthLoadOp                     = rhi::LoadOp::DONT_CARE;
	renderInfo.depthStencilAttachment.stencilLoadOp                   = rhi::LoadOp::CLEAR;
	renderInfo.depthStencilAttachment.stencilStoreOp                  = rhi::StoreOp::DONT_CARE;
	renderInfo.depthStencilAttachment.clearValue.depthStencil.stencil = 0;
	renderInfo.renderAreaWidth                                        = width;
	renderInfo.renderAreaHeight                                       = height;
	renderInfo.enableLocalRead                                        = true;
	renderInfo.colorAttachmentLocations                               = {0};

	cmdList->BeginRendering(renderInfo);
	cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
	cmdList->SetScissor(0, 0, width, height);

	// Render chunks with interleaved stencil updates
	for (uint32_t chunk = 0; chunk < m_chunkCount; ++chunk)
	{
		// === Render splats for this chunk ===
		if (m_splatPrecomputeEnabled && m_splatTransmCullingPipelinePreprocessed && m_descriptorSetsPreprocessed[frameIndex])
		{
			cmdList->SetPipeline(m_splatTransmCullingPipelinePreprocessed.Get());
			cmdList->BindDescriptorSet(0, m_descriptorSetsPreprocessed[frameIndex].Get());
		}
		else
		{
			cmdList->SetPipeline(m_splatTransmCullingPipeline.Get());
			cmdList->BindDescriptorSet(0, m_descriptorSets[frameIndex].Get());
		}
		cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

		if (indirectArgsBuffer)
		{
			// GPU-driven: per-chunk draw commands at offset 100 + chunk * 20
			constexpr size_t CHUNK_DRAW_OFFSET = 100;
			constexpr size_t DRAW_CMD_SIZE     = 20;
			cmdList->DrawIndexedIndirect(indirectArgsBuffer, CHUNK_DRAW_OFFSET + chunk * DRAW_CMD_SIZE, 1);
		}
		else
		{
			uint32_t firstSplat      = chunk * splatsPerChunk;
			uint32_t chunkSplatCount = std::min(splatsPerChunk, splatCount - firstSplat);
			if (chunkSplatCount == 0)
				break;
			cmdList->DrawIndexedInstanced(4, chunkSplatCount, 0, 0, firstSplat);
		}

		// === Stencil update pass (except for last chunk) ===
		if (chunk < m_chunkCount - 1)
		{
			// Barrier: color attachment write -> input attachment read
			rhi::MemoryBarrier colorToInput{};
			colorToInput.src_stages = rhi::StageMask::RenderTarget;
			colorToInput.src_access = rhi::AccessMask::RenderTargetWrite;
			colorToInput.dst_stages = rhi::StageMask::FragmentShader;
			colorToInput.dst_access = rhi::AccessMask::InputAttachmentRead;
			cmdList->Barrier(rhi::PipelineScope::Graphics, rhi::PipelineScope::Graphics,
			                 {}, {}, {&colorToInput, 1}, rhi::DependencyFlags::BY_REGION);

			// Stencil update pass
			cmdList->SetPipeline(m_stencilUpdatePipeline.Get());
			cmdList->BindDescriptorSet(0, m_stencilUpdateDescriptorSets[frameIndex].Get());
			cmdList->PushConstants(rhi::ShaderStageFlags::FRAGMENT, 0,
			                       {reinterpret_cast<const std::byte *>(&m_saturationThreshold),
			                        sizeof(float)});

			cmdList->Draw(3, 0);

			// Barrier: input attachment read -> color attachment write
			rhi::MemoryBarrier inputToColor{};
			inputToColor.src_stages = rhi::StageMask::FragmentShader | rhi::StageMask::LateFragmentTests;
			inputToColor.src_access = rhi::AccessMask::InputAttachmentRead | rhi::AccessMask::DepthStencilWrite;
			inputToColor.dst_stages = rhi::StageMask::RenderTarget | rhi::StageMask::EarlyFragmentTests;
			inputToColor.dst_access = rhi::AccessMask::RenderTargetWrite | rhi::AccessMask::DepthStencilRead;
			cmdList->Barrier(rhi::PipelineScope::Graphics, rhi::PipelineScope::Graphics,
			                 {}, {}, {&inputToColor, 1}, rhi::DependencyFlags::BY_REGION);
		}
	}

	cmdList->EndRendering();

	// === Composite pass to backbuffer ===
	{
		// Barrier: transition accumulation texture from local read to shader read for composite pass
		rhi::TextureTransition accumToRead{};
		accumToRead.texture = m_accumTextures[frameIndex].Get();
		accumToRead.before  = rhi::ResourceState::RenderingLocalRead;
		accumToRead.after   = rhi::ResourceState::GeneralRead;

		// Transition backbuffer to render target
		auto                  *backBuffer = swapchain->GetBackBuffer(imageIndex);
		rhi::TextureTransition backToRender{};
		backToRender.texture = backBuffer;
		backToRender.before  = rhi::ResourceState::Undefined;
		backToRender.after   = rhi::ResourceState::RenderTarget;

		rhi::TextureTransition compositeTransitions[] = {accumToRead, backToRender};
		cmdList->Barrier(rhi::PipelineScope::Graphics, rhi::PipelineScope::Graphics,
		                 {}, {compositeTransitions, 2}, {});

		rhi::RenderingInfo compositeRenderInfo{};
		compositeRenderInfo.colorAttachments.resize(1);
		compositeRenderInfo.colorAttachments[0].view    = swapchain->GetBackBufferView(imageIndex);
		compositeRenderInfo.colorAttachments[0].loadOp  = rhi::LoadOp::DONT_CARE;
		compositeRenderInfo.colorAttachments[0].storeOp = rhi::StoreOp::STORE;
		compositeRenderInfo.renderAreaWidth             = width;
		compositeRenderInfo.renderAreaHeight            = height;

		cmdList->BeginRendering(compositeRenderInfo);
		cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
		cmdList->SetScissor(0, 0, width, height);

		cmdList->SetPipeline(m_compositePipeline.Get());
		cmdList->BindDescriptorSet(0, m_compositeDescriptorSets[frameIndex].Get());

		// Set background color
		float backgroundColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		cmdList->PushConstants(rhi::ShaderStageFlags::FRAGMENT, 0,
		                       {reinterpret_cast<const std::byte *>(backgroundColor), sizeof(backgroundColor)});

		cmdList->Draw(3, 0);

		// Draw ImGui on top
		if (m_imguiEnabled && m_showImGui)
		{
			UpdateFpsHistory();
			RenderImGui();
			RenderImGuiToCommandBuffer(cmdList);
		}

		cmdList->EndRendering();
	}
}
