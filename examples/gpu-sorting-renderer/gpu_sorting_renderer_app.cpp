#include "gpu_sorting_renderer_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/gpu_splat_sorter.h>
#include <msplat/engine/splat/splat_loader.h>
#include <vulkan/vulkan.h>

GpuSortingRendererApp::GpuSortingRendererApp()
{
	fpsHistory.fill(0.0f);
}
GpuSortingRendererApp::~GpuSortingRendererApp()                                            = default;
GpuSortingRendererApp::GpuSortingRendererApp(GpuSortingRendererApp &&) noexcept            = default;
GpuSortingRendererApp &GpuSortingRendererApp::operator=(GpuSortingRendererApp &&) noexcept = default;

bool GpuSortingRendererApp::OnInit(app::DeviceManager *deviceManager)
{
	this->deviceManager = deviceManager;

	rhi::IRHIDevice *device = deviceManager->GetDevice();
	if (!device)
	{
		LOG_ERROR("Failed to get RHI device");
		return false;
	}

	rhi::IRHISwapchain *swapchain = deviceManager->GetSwapchain();

	shaderFactory = container::make_unique<engine::ShaderFactory>(device);
	scene         = container::make_unique<engine::Scene>(device);

	camera.SetPosition(math::vec3(0.0f, 0.0f, 5.0f));
	camera.SetTarget(math::vec3(0.0f, 0.0f, 0.0f));

	int width, height;
	glfwGetWindowSize(deviceManager->GetWindow(), &width, &height);
	float aspectRatio = static_cast<float>(width) / static_cast<float>(height);

	camera.SetPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);
	camera.SetMovementSpeed(5.0f);
	camera.SetMouseSensitivity(0.1f);

	// For comprehensive verification, always use test data
	if (!useSimpleVerification)
	{
		LOG_INFO("Comprehensive verification mode enabled - using test data");
		CreateTestSplatData();
	}
	else
	{
		LoadSplatFile("assets/flowers_1.ply");
		// LoadSplatFile("assets/train_7000.ply");
	}

	sorter = container::make_unique<engine::GpuSplatSorter>(device);
	if (scene->GetTotalSplatCount() > 0)
	{
		sorter->Initialize(scene->GetTotalSplatCount());
		LOG_INFO("Initialized sorter for {} splats", scene->GetTotalSplatCount());
	}

	// Create quad index buffer for instanced rendering
	// Uses triangle strip topology: 0, 1, 2, 3 forms two triangles
	// Triangle 1: vertices 0, 1, 2
	// Triangle 2: vertices 1, 2, 3
	if (scene->GetTotalSplatCount() > 0)
	{
		container::vector<uint32_t> quadIndices = {0, 1, 2, 3};

		rhi::BufferDesc ibDesc{};
		ibDesc.size        = quadIndices.size() * sizeof(uint32_t);
		ibDesc.usage       = rhi::BufferUsage::INDEX;
		ibDesc.indexType   = rhi::IndexType::UINT32;
		ibDesc.initialData = quadIndices.data();
		quadIndexBuffer    = device->CreateBuffer(ibDesc);
	}

	// Load splat rendering shaders
	vertexShader   = shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.vert.spv", rhi::ShaderStage::VERTEX);
	fragmentShader = shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.frag.spv", rhi::ShaderStage::FRAGMENT);

	// Create UBO
	rhi::BufferDesc uboDesc{};
	uboDesc.size                      = sizeof(FrameUBO);
	uboDesc.usage                     = rhi::BufferUsage::UNIFORM;
	uboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	uboDesc.hints.persistently_mapped = true;
	frameUboBuffer                    = device->CreateBuffer(uboDesc);
	frameUboDataPtr                   = frameUboBuffer->Map();

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
	descriptorSetLayout = device->CreateDescriptorSetLayout(layoutDesc);

	// Create descriptor set
	descriptorSet = device->CreateDescriptorSet(descriptorSetLayout.Get());

	// Binding 0: UBO
	rhi::BufferBinding uboBinding{};
	uboBinding.buffer = frameUboBuffer.Get();
	uboBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	descriptorSet->BindBuffer(0, uboBinding);

	// Binding 1: Positions
	rhi::BufferBinding positionsBinding{};
	positionsBinding.buffer = scene->GetGpuData().positions.Get();
	positionsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	descriptorSet->BindBuffer(1, positionsBinding);

	// Binding 2: Covariances3D
	rhi::BufferBinding cov3DBinding{};
	cov3DBinding.buffer = scene->GetGpuData().covariances3D.Get();
	cov3DBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	descriptorSet->BindBuffer(2, cov3DBinding);

	// Binding 3: Colors
	rhi::BufferBinding colorsBinding{};
	colorsBinding.buffer = scene->GetGpuData().colors.Get();
	colorsBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	descriptorSet->BindBuffer(3, colorsBinding);

	// Binding 4: SH Rest
	rhi::BufferBinding shBinding{};
	shBinding.buffer = scene->GetGpuData().shRest.Get();
	shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	descriptorSet->BindBuffer(4, shBinding);

	// Binding 5 (sorted indices) will be updated after sorting

	// Create graphics pipeline
	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertexShader                = vertexShader.Get();
	pipelineDesc.fragmentShader              = fragmentShader.Get();
	pipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_STRIP;
	pipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable         = true;
	pipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].srcAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].dstAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.targetSignature.colorFormats                 = {swapchain->GetBackBuffer(0)->GetFormat()};
	pipelineDesc.descriptorSetLayouts                         = {descriptorSetLayout.Get()};
	renderPipeline                                            = device->CreateGraphicsPipeline(pipelineDesc);

	uint32_t imageCount = deviceManager->GetSwapchain()->GetImageCount();

	imageAvailableSemaphores.resize(imageCount);
	renderFinishedSemaphores.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		imageAvailableSemaphores[i] = device->CreateSemaphore();
		renderFinishedSemaphores[i] = device->CreateSemaphore();
	}
	inFlightFence = device->CreateFence(true);

	commandLists.resize(imageCount);
	for (uint32_t i = 0; i < imageCount; ++i)
	{
		commandLists[i] = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	}

	applicationTimer.start();
	fpsCounter.reset();

	InitImGui();

	return true;
}

void GpuSortingRendererApp::OnUpdate(float deltaTime)
{
	camera.Update(deltaTime, deviceManager->GetWindow());
}

void GpuSortingRendererApp::OnRender()
{
	if (!deviceManager || !scene || scene->GetTotalSplatCount() == 0)
	{
		return;
	}

	rhi::IRHIDevice    *device    = deviceManager->GetDevice();
	rhi::IRHISwapchain *swapchain = deviceManager->GetSwapchain();

	// Check if window is minimized before waiting on fence
	int width, height;
	glfwGetWindowSize(deviceManager->GetWindow(), &width, &height);
	if (width == 0 || height == 0)
	{
		return;
	}

	// Wait for previous frame
	inFlightFence->Wait(UINT64_MAX);
	inFlightFence->Reset();

	fpsCounter.frame();

	// Acquire next image (skip in benchmark mode)
	uint32_t acquireSemIndex = frameCount % imageAvailableSemaphores.size();
	uint32_t imageIndex      = 0;

	if (!benchmarkMode)
	{
		rhi::SwapchainStatus status = swapchain->AcquireNextImage(
		    imageIndex,
		    imageAvailableSemaphores[acquireSemIndex].Get());

		if (status == rhi::SwapchainStatus::OUT_OF_DATE)
		{
			return;
		}
	}
	else
	{
		imageIndex = 0;
	}

	FrameUBO ubo{};
	ubo.view       = camera.GetViewMatrix();
	ubo.projection = camera.GetProjectionMatrix();
	ubo.projection[1][1] *= -1.0f;        // Flip the Y[1][1] component to match Vulkan's NDC
	ubo.cameraPos          = math::vec4(camera.GetPosition(), 1.0f);
	ubo.viewport           = {static_cast<float>(width), static_cast<float>(height)};
	ubo.focal              = {ubo.projection[0][0] * width * 0.5f,
	                          ubo.projection[1][1] * height * 0.5f};
	ubo.splatScale         = 1.0f;                                 // Default scale factor
	ubo.alphaCullThreshold = 1.0f / 255.0f;                        // Default alpha cutoff
	ubo.maxSplatRadius     = 2048.0f;                              // Maximum splat radius in pixels
	ubo.enableSplatFilter  = 1;                                    // Enable EWA filtering
	ubo.basisViewport      = {1.0f / width, 1.0f / height};        // Resolution-aware scaling
	ubo.inverseFocalAdj    = 1.0f;                                 // No FOV adjustment by default
	ubo.screenRotation     = {1.0f, 0.0f, 0.0f, 1.0f};             // Identity rotation matrix
	memcpy(frameUboDataPtr, &ubo, sizeof(FrameUBO));

	rhi::IRHICommandList *cmdList = commandLists[imageIndex].Get();
	cmdList->Begin();

	// Check verification results from previous frame if pending
	if (checkVerificationResults && sorter)
	{
		LOG_INFO("Checking sorting verification results...");

		bool sortingCorrect;
		if (useSimpleVerification)
		{
			sortingCorrect = sorter->VerifySortOrder();
		}
		else
		{
			sortingCorrect = sorter->CheckVerificationResults(&testSplatPositions);
		}

		if (sortingCorrect)
		{
			LOG_INFO("Sorting verification completed successfully");
		}
		else
		{
			LOG_ERROR("Sorting verification failed - check logs for details");
		}
		checkVerificationResults = false;
	}

	// Perform GPU sorting if enabled
	if (sorter && sortingEnabled)
	{
		sorter->Sort(cmdList, *scene, camera);

		// Prepare verification if requested
		if (verifyNextSort)
		{
			LOG_INFO("Preparing sorting verification...");
			sorter->PrepareVerification(cmdList);
			checkVerificationResults = true;
			verifyNextSort           = false;
		}

		// Update sorted indices buffer (only when it changes)
		rhi::BufferHandle newSortedIndices = sorter->GetSortedIndices();
		if (newSortedIndices.Get() != sortedIndices.Get())
		{
			sortedIndices = newSortedIndices;

			rhi::BufferBinding indicesBinding{};
			indicesBinding.buffer = sortedIndices.Get();
			indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			descriptorSet->BindBuffer(5, indicesBinding);
		}
	}

	// Log FPS periodically
	if (frameCount % 60 == 0 && fpsCounter.shouldUpdate())
	{
		LOG_INFO("Frame FPS: {:.2f} | Sort: {} | Splats: {}",
		         fpsCounter.getFPS(),
		         sortingEnabled ? (sorter ? sorter->GetSortMethodName() : "N/A") : "Disabled",
		         scene->GetTotalSplatCount());
		fpsCounter.reset();
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
	cmdList->SetPipeline(renderPipeline.Get());
	cmdList->BindDescriptorSet(0, descriptorSet.Get());
	cmdList->BindIndexBuffer(quadIndexBuffer.Get());

	// Draw using instanced rendering: 4 indices per strip, one instances per splat
	uint32_t indexCount    = 4;
	uint32_t instanceCount = scene->GetTotalSplatCount();
	cmdList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);

	if (showImGui)
	{
		UpdateFpsHistory();
		RenderImGui();
		RenderImGuiToCommandBuffer(cmdList);
	}

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
	submitInfo.signalFence     = inFlightFence.Get();

	// Use explicit arrays to avoid std::span brace-initialization issues on release builds
	rhi::SemaphoreWaitInfo waitInfoArray[1];
	rhi::IRHISemaphore    *signalSemArray[1];

	if (!benchmarkMode)
	{
		// Normal mode: wait for image available and signal when rendering is done
		waitInfoArray[0].semaphore = imageAvailableSemaphores[acquireSemIndex].Get();
		waitInfoArray[0].waitStage = rhi::StageMask::RenderTarget;

		signalSemArray[0] = renderFinishedSemaphores[imageIndex].Get();

		submitInfo.waitSemaphores   = std::span<const rhi::SemaphoreWaitInfo>(waitInfoArray, 1);
		submitInfo.signalSemaphores = std::span<rhi::IRHISemaphore *const>(signalSemArray, 1);
	}

	rhi::IRHICommandList *cmdListArray[1] = {cmdList};
	device->SubmitCommandLists(std::span<rhi::IRHICommandList *const>(cmdListArray, 1), rhi::QueueType::GRAPHICS, submitInfo);

	// Present (skip in benchmark mode to remove vsync bottleneck)
	if (!benchmarkMode)
	{
		swapchain->Present(imageIndex, renderFinishedSemaphores[imageIndex].Get());
	}

	frameCount++;
}

void GpuSortingRendererApp::OnShutdown()
{
	if (deviceManager)
	{
		rhi::IRHIDevice *device = deviceManager->GetDevice();
		device->WaitIdle();

		ShutdownImGui();

		if (frameUboDataPtr)
		{
			frameUboBuffer->Unmap();
			frameUboDataPtr = nullptr;
		}
	}

	renderPipeline      = nullptr;
	descriptorSet       = nullptr;
	descriptorSetLayout = nullptr;

	vertexShader   = nullptr;
	fragmentShader = nullptr;

	frameUboBuffer  = nullptr;
	quadIndexBuffer = nullptr;
	sortedIndices   = nullptr;

	sorter.reset();
	scene.reset();
	shaderFactory.reset();

	imageAvailableSemaphores.clear();
	renderFinishedSemaphores.clear();
	inFlightFence = nullptr;
	commandLists.clear();
}

void GpuSortingRendererApp::OnKey(int key, int action, int mods)
{
	camera.OnKey(key, action, mods);

	if (action == GLFW_PRESS)
	{
		switch (key)
		{
			case GLFW_KEY_SPACE:
				// Toggle sorting on/off for performance comparison
				sortingEnabled = !sortingEnabled;
				LOG_INFO("Sorting {}", sortingEnabled ? "enabled" : "disabled");
				break;

			case GLFW_KEY_V:
				// Verify sorting on next frame
				verifyNextSort = true;
				LOG_INFO("Will verify sorting on next frame using {} verification",
				         useSimpleVerification ? "SIMPLE" : "COMPREHENSIVE");
				break;

			case GLFW_KEY_M:
				// Toggle sorting method between prescan and integrated scan
				if (sorter)
				{
					if (sorter->GetSortMethod() == engine::GpuSplatSorter::SortMethod::IntegratedScan)
					{
						sorter->SetSortMethod(engine::GpuSplatSorter::SortMethod::Prescan);
						LOG_INFO("Switched to Prescan radix sort method");
					}
					else
					{
						sorter->SetSortMethod(engine::GpuSplatSorter::SortMethod::IntegratedScan);
						LOG_INFO("Switched to Integrated Scan radix sort method");
					}
				}
				break;

			case GLFW_KEY_T:
				// Toggle verification mode
				useSimpleVerification = !useSimpleVerification;
				LOG_INFO("Verification mode switched to: {}",
				         useSimpleVerification ? "SIMPLE (sort order only)" : "COMPREHENSIVE (all steps)");
				break;

			case GLFW_KEY_B:
				// Toggle benchmark mode (skip present to remove vsync limit)
				benchmarkMode = !benchmarkMode;
				LOG_INFO("Benchmark mode {}", benchmarkMode ? "enabled (no vsync)" : "disabled (vsync on)");
				fpsCounter.reset();
				break;

			case GLFW_KEY_H:
				// Toggle ImGui visibility
				showImGui = !showImGui;
				LOG_INFO("ImGui {}", showImGui ? "shown" : "hidden");
				break;

			case GLFW_KEY_ESCAPE:
				// Exit application
				glfwSetWindowShouldClose(deviceManager->GetWindow(), GLFW_TRUE);
				break;
		}
	}
}

void GpuSortingRendererApp::OnMouseButton(int button, int action, int mods)
{
	camera.OnMouseButton(button, action, mods);
}

void GpuSortingRendererApp::OnMouseMove(double xpos, double ypos)
{
	camera.OnMouseMove(xpos, ypos);
}

void GpuSortingRendererApp::LoadSplatFile(const char *filepath)
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

		scene->AddMesh(splatData, math::Identity());
		scene->AllocateGpuBuffers();
		rhi::FenceHandle uploadFence = scene->UploadAttributeData();
		uploadFence->Wait(UINT64_MAX);
		LOG_INFO("Loaded {} splats from {}", splatData->numSplats, filepath);
	}
	else
	{
		LOG_WARNING("Failed to load splat file {}, creating test data", filepath);
		CreateTestSplatData();
	}
}

void GpuSortingRendererApp::CreateTestSplatData()
{
	// Camera is at (0, 0, 5) looking down -Z axis (towards origin)
	const uint32_t testSplatCount = 500000;
	auto           testData       = container::make_shared<engine::SplatSoA>();
	testData->Resize(testSplatCount, 0);

	LOG_INFO("Creating {} random test splats for comprehensive sorting verification", testSplatCount);
	LOG_INFO("Camera position: (0, 0, 5), looking towards origin (down -Z axis)");

	std::srand(114514);

	// Store positions for verification later
	testSplatPositions.clear();
	testSplatPositions.reserve(testSplatCount);

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
		testSplatPositions.push_back({testData->posX[i], testData->posY[i], testData->posZ[i]});

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

	scene->AddMesh(testData, math::Identity());
	scene->AllocateGpuBuffers();

	rhi::FenceHandle uploadFence = scene->UploadAttributeData();
	uploadFence->Wait(UINT64_MAX);

	LOG_INFO("Created random test scene with {} splats", testSplatCount);
}

void GpuSortingRendererApp::InitImGui()
{
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	io.FontGlobalScale = 1.3f;

	ImGui_ImplGlfw_InitForVulkan(deviceManager->GetWindow(), true);

	// Get RHI device
	rhi::IRHIDevice *device = deviceManager->GetDevice();

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
	imguiDescriptorPool = static_cast<void *>(imguiPool);

	// Setup ImGui Vulkan backend
	ImGui_ImplVulkan_InitInfo init_info = {};
	init_info.Instance                  = static_cast<VkInstance>(device->GetNativeInstance());
	init_info.PhysicalDevice            = static_cast<VkPhysicalDevice>(device->GetNativePhysicalDevice());
	init_info.Device                    = vkDevice;
	init_info.QueueFamily               = device->GetGraphicsQueueFamily();
	init_info.Queue                     = static_cast<VkQueue>(device->GetNativeGraphicsQueue());
	init_info.DescriptorPool            = imguiPool;
	init_info.MinImageCount             = 2;
	init_info.ImageCount                = deviceManager->GetSwapchain()->GetImageCount();
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

void GpuSortingRendererApp::ShutdownImGui()
{
	// Cleanup ImGui
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	// Destroy descriptor pool
	if (imguiDescriptorPool && deviceManager)
	{
		rhi::IRHIDevice *device   = deviceManager->GetDevice();
		VkDevice         vkDevice = static_cast<VkDevice>(device->GetNativeDevice());
		VkDescriptorPool pool     = static_cast<VkDescriptorPool>(imguiDescriptorPool);
		vkDestroyDescriptorPool(vkDevice, pool, nullptr);
		imguiDescriptorPool = nullptr;
	}
}

void GpuSortingRendererApp::UpdateFpsHistory()
{
	float currentFps            = static_cast<float>(fpsCounter.getFPS());
	fpsHistory[fpsHistoryIndex] = currentFps;
	fpsHistoryIndex             = (fpsHistoryIndex + 1) % FPS_HISTORY_SIZE;
}

void GpuSortingRendererApp::RenderImGui()
{
	// Start the Dear ImGui frame
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	// Create main control window
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("GPU Splat Renderer Controls", &showImGui))
	{
		ImGui::Text("Performance");
		ImGui::Separator();

		// FPS display with current value
		float currentFps = static_cast<float>(fpsCounter.getFPS());
		ImGui::Text("FPS: %.1f", currentFps);

		// FPS graph
		ImGui::PlotLines("FPS History",
		                 fpsHistory.data(),
		                 static_cast<int>(FPS_HISTORY_SIZE),
		                 static_cast<int>(fpsHistoryIndex),
		                 nullptr,
		                 0.0f,
		                 120.0f,
		                 ImVec2(0, 80));

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Rendering Controls");
		ImGui::Separator();

		// Sorting enabled toggle
		if (ImGui::Checkbox("Enable Sorting", &sortingEnabled))
		{
			LOG_INFO("Sorting {}", sortingEnabled ? "enabled" : "disabled");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Toggle depth sorting of splats\nDisabling may improve performance but reduce quality");
		}

		// Sort method selection
		if (sorter)
		{
			ImGui::Text("Sort Method:");
			int         currentMethod = (sorter->GetSortMethod() == engine::GpuSplatSorter::SortMethod::Prescan) ? 0 : 1;
			const char *methods[]     = {"Prescan Radix Sort", "Integrated Scan Radix Sort"};

			if (ImGui::Combo("##SortMethod", &currentMethod, methods, 2))
			{
				if (currentMethod == 0)
				{
					sorter->SetSortMethod(engine::GpuSplatSorter::SortMethod::Prescan);
					LOG_INFO("Switched to Prescan radix sort method");
				}
				else
				{
					sorter->SetSortMethod(engine::GpuSplatSorter::SortMethod::IntegratedScan);
					LOG_INFO("Switched to Integrated Scan radix sort method");
				}
			}
		}

		ImGui::Spacing();

		// Verification controls
		ImGui::Text("Verification");
		if (ImGui::Button("Verify Sorting Result"))
		{
			verifyNextSort = true;
			LOG_INFO("Will verify sorting on next frame");
		}

		ImGui::Spacing();
		ImGui::Separator();

		ImGui::Text("Scene Information");
		ImGui::Separator();

		// Scene stats
		if (scene)
		{
			ImGui::Text("Total Splats: %u", scene->GetTotalSplatCount());
		}
		ImGui::Text("Frame Count: %u", frameCount);

		ImGui::Spacing();

		// Application info
		ImGui::Separator();
		ImGui::Text("Controls Help");
		ImGui::Separator();
		ImGui::BulletText("WASD: Move camera");
		ImGui::BulletText("Mouse: Look around");
		ImGui::BulletText("ESC: Exit application");
		ImGui::BulletText("H: Toggle this UI");
	}
	ImGui::End();

	ImGui::Render();
}

void GpuSortingRendererApp::RenderImGuiToCommandBuffer(rhi::IRHICommandList *cmdList)
{
	VkCommandBuffer vkCmdBuf = static_cast<VkCommandBuffer>(cmdList->GetNativeCommandBuffer());
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vkCmdBuf);
}