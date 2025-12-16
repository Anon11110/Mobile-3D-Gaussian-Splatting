#include "hybrid_splat_renderer_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#if !defined(__ANDROID__)
#	include <GLFW/glfw3.h>
#endif
#include <chrono>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/splat_sort_backend.h>
#include <msplat/engine/splat/splat_loader.h>
#include <thread>
#include <vulkan/vulkan.h>

HybridSplatRendererApp::HybridSplatRendererApp()
{
#if !defined(__ANDROID__)
	m_fpsHistory.fill(0.0f);
#endif
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

	m_camera.SetPosition(math::vec3(0.0f, 0.0f, 5.0f));
	m_camera.SetTarget(math::vec3(0.0f, 0.0f, 0.0f));

	int width, height;
	deviceManager->GetPlatformAdapter()->GetFramebufferSize(&width, &height);
	float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

	m_camera.SetPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);
	m_camera.SetMovementSpeed(5.0f);
	m_camera.SetMouseSensitivity(0.1f);

	if (m_splatPath.empty())
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

	if (!m_splatPath.empty())
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
	rhi::BufferBinding positionsBinding{};
	positionsBinding.buffer = m_scene->GetGpuData().positions.Get();
	positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(1, positionsBinding);

	// Binding 2: Covariances3D
	rhi::BufferBinding cov3DBinding{};
	cov3DBinding.buffer = m_scene->GetGpuData().covariances3D.Get();
	cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(2, cov3DBinding);

	// Binding 3: Colors
	rhi::BufferBinding colorsBinding{};
	colorsBinding.buffer = m_scene->GetGpuData().colors.Get();
	colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(3, colorsBinding);

	// Binding 4: SH Rest
	if (m_scene->GetGpuData().shRest)
	{
		rhi::BufferBinding shBinding{};
		shBinding.buffer = m_scene->GetGpuData().shRest.Get();
		shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(4, shBinding);
	}
	else
	{
		LOG_INFO("Skipping SH rest buffer binding (no SH data in scene)");
	}

	// Binding 5: Sorted indices (app-owned buffer, written to by backend)
	if (m_sortedIndices)
	{
		rhi::BufferBinding indicesBinding{};
		indicesBinding.buffer = m_sortedIndices.Get();
		indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
		m_descriptorSet->BindBuffer(5, indicesBinding);
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

#if !defined(__ANDROID__)
	InitImGui();
#endif

	// Log initialization summary
	int initWidth, initHeight;
	deviceManager->GetPlatformAdapter()->GetFramebufferSize(&initWidth, &initHeight);
	LOG_INFO("Window size: {}x{}", initWidth, initHeight);
	LOG_INFO("Backend: {} ({})",
	         m_backend ? m_backend->GetName() : "None",
	         m_backend ? m_backend->GetMethodName() : "N/A");
	LOG_INFO("Splats loaded: {}", m_scene ? m_scene->GetTotalSplatCount() : 0);
	LOG_INFO("=== Initialization Complete ===");

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

	m_currentBackendType = newType;
	LOG_INFO("Switched to {} backend ({})", m_backend->GetName(), m_backend->GetMethodName());
}

void HybridSplatRendererApp::OnUpdate(float deltaTime)
{
	m_camera.Update(deltaTime, m_deviceManager->GetWindow());
}

void HybridSplatRendererApp::OnRender()
{
	if (!m_deviceManager || !m_scene || m_scene->GetTotalSplatCount() == 0)
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

	// Perform GPU sorting if enabled
	if (m_backend && m_sortingEnabled)
	{
		// Check verification results from previous frame if pending
		if (m_checkVerificationResults)
		{
			LOG_INFO("Checking sorting verification results...");
			bool sortingCorrect = m_backend->VerifySort();
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

		m_backend->Update(m_camera);

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
				bool sortingCorrect = m_backend->VerifySort();
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
		LOG_INFO("Frame FPS: {:.2f} | Sort: {} | Splats: {}",
		         m_fpsCounter.getFPS(),
		         m_sortingEnabled ? (m_backend ? m_backend->GetMethodName() : "N/A") : "Disabled",
		         m_scene->GetTotalSplatCount());
		m_fpsCounter.reset();
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

	cmdList->BeginRendering(renderingInfo);
	cmdList->SetViewport(0, 0, static_cast<float>(width), static_cast<float>(height));
	cmdList->SetScissor(0, 0, width, height);

	// Draw the splats using indexed procedural vertex generation
	cmdList->SetPipeline(m_renderPipeline.Get());
	cmdList->BindDescriptorSet(0, m_descriptorSet.Get());
	cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

	// Draw using instanced rendering: 4 indices per strip, one instance per splat
	uint32_t indexCount    = 4;
	uint32_t instanceCount = m_scene->GetTotalSplatCount();
	cmdList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);

#if !defined(__ANDROID__)
	if (m_showImGui)
	{
		UpdateFpsHistory();
		RenderImGui();
		RenderImGuiToCommandBuffer(cmdList);
	}
#endif

	cmdList->EndRendering();

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
	rhi::SemaphoreWaitInfo waitInfoArray[1];
	rhi::IRHISemaphore    *signalSemArray[1];

	if (!m_vsyncBypassMode)
	{
		// Normal mode: wait for image available and signal when rendering is done
		waitInfoArray[0].semaphore = m_imageAvailableSemaphores[acquireSemIndex].Get();
		waitInfoArray[0].waitStage = rhi::StageMask::RenderTarget;

		signalSemArray[0] = m_renderFinishedSemaphores[imageIndex].Get();

		submitInfo.waitSemaphores   = std::span<const rhi::SemaphoreWaitInfo>(waitInfoArray, 1);
		submitInfo.signalSemaphores = std::span<rhi::IRHISemaphore *const>(signalSemArray, 1);
	}

	rhi::IRHICommandList *cmdListArray[1] = {cmdList};
	device->SubmitCommandLists(std::span<rhi::IRHICommandList *const>(cmdListArray, 1), rhi::QueueType::GRAPHICS, submitInfo);

	// Present (skip in vsync bypass mode)
	if (!m_vsyncBypassMode)
	{
		swapchain->Present(imageIndex, m_renderFinishedSemaphores[imageIndex].Get());
	}

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

#if !defined(__ANDROID__)
		ShutdownImGui();
#endif

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
						m_backend->SetSortMethod(0);
						LOG_INFO("Switched to Prescan radix sort method");
					}
					else
					{
						m_backend->SetSortMethod(1);
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
				// Switch between CPU and GPU backends
				if (m_currentBackendType == BackendType::GPU)
				{
					SwitchBackend(BackendType::CPU);
				}
				else
				{
					SwitchBackend(BackendType::GPU);
				}
				break;

			case GLFW_KEY_H:
				// Toggle ImGui visibility
				m_showImGui = !m_showImGui;
				LOG_INFO("ImGui {}", m_showImGui ? "shown" : "hidden");
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

		m_scene->AddMesh(splatData, math::Identity());
		m_scene->AllocateGpuBuffers();
		rhi::FenceHandle uploadFence = m_scene->UploadAttributeData();
		uploadFence->Wait(UINT64_MAX);
		LOG_INFO("Loaded {} splats from {}", splatData->numSplats, filepath);
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
	const uint32_t testSplatCount = 100000;
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

	for (uint32_t i = 0; i < testSplatCount; ++i)
	{
		float randomValue = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		float z           = minZ + randomValue * zRange;

		// Add some specific test cases to ensure edge cases are covered
		if (i == 0)
			z = minZ;        // Ensure we have the minimum Z
		if (i == 1)
			z = maxZ;        // Ensure we have the maximum Z
		if (i == 2)
			z = 5.0f;        // Ensure we have a splat at camera position
		if (i == 3)
			z = 4.99f;        // Very close behind camera
		if (i == 4)
			z = 5.01f;        // Very close in front of camera
		if (i == 5)
			z = -1000.0f;        // Moderately far behind camera
		if (i == 6)
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

	LOG_INFO("Swapchain resized: {}x{}", width, height);
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

#if !defined(__ANDROID__)
void HybridSplatRendererApp::InitImGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	io.FontGlobalScale = 1.3f;

	ImGui_ImplGlfw_InitForVulkan(m_deviceManager->GetWindow(), true);

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
	ImGui_ImplGlfw_Shutdown();
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
	// Start the Dear ImGui frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Create main control window
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Hybrid Splat Renderer Controls", &m_showImGui))
	{
		ImGui::Text("Performance");
		ImGui::Separator();

		// FPS display with current value
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

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Rendering Controls");
		ImGui::Separator();

		// Sorting enabled toggle
		if (ImGui::Checkbox("Enable Sorting", &m_sortingEnabled))
		{
			LOG_INFO("Sorting {}", m_sortingEnabled ? "enabled" : "disabled");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Toggle depth sorting of splats\nDisabling may improve performance but reduce quality");
		}

		// Backend selection
		ImGui::Text("Sort Backend:");
		int         backendIndex = static_cast<int>(m_currentBackendType);
		const char *backends[]   = {"GPU", "CPU"};
		if (ImGui::Combo("##Backend", &backendIndex, backends, 2))
		{
			SwitchBackend(static_cast<BackendType>(backendIndex));
		}

		// Backend-specific info
		if (m_backend)
		{
			ImGui::Text("Method: %s", m_backend->GetMethodName());

			auto metrics = m_backend->GetMetrics();
			ImGui::Text("Sort Time: %.2f ms", metrics.sortDurationMs);

			// Upload time only relevant for CPU backend
			if (m_currentBackendType == BackendType::CPU)
			{
				ImGui::Text("Upload Time: %.2f ms", metrics.uploadDurationMs);
			}

			// Sort method switching only for GPU backend
			if (m_currentBackendType == BackendType::GPU)
			{
				int         currentMethod = m_backend->GetSortMethod();
				const char *methods[]     = {"Prescan Radix Sort", "Integrated Scan Radix Sort"};
				if (ImGui::Combo("##SortMethod", &currentMethod, methods, 2))
				{
					m_backend->SetSortMethod(currentMethod);
					LOG_INFO("Switched to {} radix sort method", methods[currentMethod]);
				}
			}
		}

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
		ImGui::Separator();

		ImGui::Text("Scene Information");
		ImGui::Separator();

		// Scene stats
		if (m_scene)
		{
			ImGui::Text("Total Splats: %u", m_scene->GetTotalSplatCount());
		}
		ImGui::Text("Frame Count: %u", m_frameCount);

		ImGui::Spacing();

		// Application info
		ImGui::Separator();
		ImGui::Text("Controls Help");
		ImGui::Separator();
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
	}
	ImGui::End();

	ImGui::Render();
}

void HybridSplatRendererApp::RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList)
{
	VkCommandBuffer vkCmdBuf = static_cast<VkCommandBuffer>(cmdList->GetNativeCommandBuffer());
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuf);
}
#endif        // !defined(__ANDROID__)
