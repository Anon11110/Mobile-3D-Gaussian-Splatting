#include "gpu_sorting_renderer_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <msplat/engine/gpu_splat_sorter.h>
#include <msplat/engine/scene.h>
#include <msplat/engine/splat_loader.h>

GpuSortingRendererApp::GpuSortingRendererApp()                                             = default;
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

	LoadSplatFile("assets/flowers_1.ply");

	sorter = container::make_unique<engine::GpuSplatSorter>(device);
	if (scene->GetTotalSplatCount() > 0)
	{
		sorter->Initialize(scene->GetTotalSplatCount());
		LOG_INFO("Initialized sorter for {} splats", scene->GetTotalSplatCount());
	}

	// Create static index buffer for indexed quad rendering
	// Pattern: [0,1,2, 2,1,3, 4,5,6, 6,5,7, ...]
	if (scene->GetTotalSplatCount() > 0)
	{
		uint32_t splatCount = scene->GetTotalSplatCount();
		uint32_t indexCount = splatCount * 6;

		container::vector<uint32_t> indices;
		indices.reserve(indexCount);

		for (uint32_t i = 0; i < splatCount; ++i)
		{
			uint32_t baseVertex = i * 4;
			// First triangle: 0, 1, 2
			indices.push_back(baseVertex + 0);
			indices.push_back(baseVertex + 1);
			indices.push_back(baseVertex + 2);
			// Second triangle: 2, 1, 3
			indices.push_back(baseVertex + 2);
			indices.push_back(baseVertex + 1);
			indices.push_back(baseVertex + 3);
		}

		rhi::BufferDesc ibDesc{};
		ibDesc.size        = indices.size() * sizeof(uint32_t);
		ibDesc.usage       = rhi::BufferUsage::INDEX;
		ibDesc.indexType   = rhi::IndexType::UINT32;
		ibDesc.initialData = indices.data();
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
	pipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_LIST;
	pipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable         = true;
	pipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
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

	// Update UBO
	int width, height;
	glfwGetWindowSize(deviceManager->GetWindow(), &width, &height);
	FrameUBO ubo{};
	ubo.view       = camera.GetViewMatrix();
	ubo.projection = camera.GetProjectionMatrix();
	ubo.projection[1][1] *= -1.0f;        // Flip the Y[1][1] component to match Vulkan's NDC
	ubo.cameraPos          = math::vec4(camera.GetPosition(), 1.0f);
	ubo.viewport           = {static_cast<float>(width), static_cast<float>(height)};
	ubo.focal              = {ubo.projection[0][0] * width * 0.5f,
	                          ubo.projection[1][1] * height * 0.5f};
	ubo.splatScale         = 1.0f;                 // Default scale factor
	ubo.alphaCullThreshold = 1.0f / 255.0f;        // Default alpha cutoff
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

	// Draw using index buffer: 6 indices per quad, 4 vertices per splat
	uint32_t indexCount = scene->GetTotalSplatCount() * 6;
	cmdList->DrawIndexed(indexCount, 0, 0);

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

	if (!benchmarkMode)
	{
		// Normal mode: wait for image available and signal when rendering is done
		rhi::SemaphoreWaitInfo waitInfo = {};
		waitInfo.semaphore              = imageAvailableSemaphores[acquireSemIndex].Get();
		waitInfo.waitStage              = rhi::StageMask::RenderTarget;

		submitInfo.waitSemaphores     = {&waitInfo, 1};
		rhi::IRHISemaphore *signalSem = renderFinishedSemaphores[imageIndex].Get();
		submitInfo.signalSemaphores   = {&signalSem, 1};
	}

	device->SubmitCommandLists({&cmdList, 1}, rhi::QueueType::GRAPHICS, submitInfo);

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
	const uint32_t testSplatCount = 10000000;
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