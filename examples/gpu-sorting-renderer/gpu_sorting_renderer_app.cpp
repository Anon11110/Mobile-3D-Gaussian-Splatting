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

	LoadSplatFile("assets/test.splat");

	sorter = container::make_unique<engine::GpuSplatSorter>(device);
	if (scene->GetTotalSplatCount() > 0)
	{
		sorter->Initialize(scene->GetTotalSplatCount());
		LOG_INFO("Initialized sorter for {} splats", scene->GetTotalSplatCount());
	}

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
	fpsCounter.frame();
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

	// Acquire next image
	uint32_t             acquireSemIndex = frameCount % imageAvailableSemaphores.size();
	uint32_t             imageIndex      = 0;
	rhi::SwapchainStatus status          = swapchain->AcquireNextImage(
        imageIndex,
        imageAvailableSemaphores[acquireSemIndex].Get());

	if (status == rhi::SwapchainStatus::OUT_OF_DATE)
	{
		return;
	}

	rhi::IRHICommandList *cmdList = commandLists[imageIndex].Get();
	cmdList->Begin();

	// Check verification results from previous frame if pending
	if (checkVerificationResults && sorter)
	{
		LOG_INFO("Checking sorting verification results...");
		bool sortingCorrect = sorter->CheckVerificationResults();
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
		auto startTime = std::chrono::high_resolution_clock::now();

		sorter->Sort(cmdList, *scene, camera);

		auto endTime  = std::chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

		// Prepare verification if requested
		if (verifyNextSort)
		{
			LOG_INFO("Preparing sorting verification...");
			sorter->PrepareVerification(cmdList);
			checkVerificationResults = true;
			verifyNextSort           = false;
		}

		// Log sorting time every 60 frames
		if (frameCount % 60 == 0)
		{
			LOG_INFO("GPU sorting took: {} microseconds", duration.count());
			if (fpsCounter.shouldUpdate())
			{
				LOG_INFO("FPS: {:.2f}", fpsCounter.getFPS());
			}
		}
	}

	rhi::BufferHandle sortedIndices;
	if (sorter)
	{
		sortedIndices = sorter->GetSortedIndices();
	}

	// TODO: actual rendering using the sorted indices

	rhi::IRHITexture *backBuffer = swapchain->GetBackBuffer(imageIndex);

	// Transition to present
	rhi::TextureTransition presentTransition = {};
	presentTransition.texture                = backBuffer;
	presentTransition.before                 = rhi::ResourceState::Undefined;
	presentTransition.after                  = rhi::ResourceState::Present;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    {&presentTransition, 1},
	    {});

	cmdList->End();

	// Submit command list
	rhi::SemaphoreWaitInfo waitInfo = {};
	waitInfo.semaphore              = imageAvailableSemaphores[acquireSemIndex].Get();
	waitInfo.waitStage              = rhi::StageMask::RenderTarget;

	rhi::SubmitInfo submitInfo    = {};
	submitInfo.waitSemaphores     = {&waitInfo, 1};
	rhi::IRHISemaphore *signalSem = renderFinishedSemaphores[imageIndex].Get();
	submitInfo.signalSemaphores   = {&signalSem, 1};
	submitInfo.signalFence        = inFlightFence.Get();

	device->SubmitCommandLists({&cmdList, 1}, rhi::QueueType::GRAPHICS, submitInfo);

	// Present
	swapchain->Present(imageIndex, renderFinishedSemaphores[imageIndex].Get());

	frameCount++;
}

void GpuSortingRendererApp::OnShutdown()
{
	if (deviceManager)
	{
		rhi::IRHIDevice *device = deviceManager->GetDevice();
		device->WaitIdle();
	}

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
				LOG_INFO("Will verify sorting on next frame");
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
	// engine::SplatLoader loader;
	// auto                futureData = loader.Load(filepath);

	// auto splatData = futureData.get();

	// if (splatData && splatData->numSplats > 0)
	// {
	// 	scene->AddMesh(splatData, math::Identity());
	// 	scene->AllocateGpuBuffers();
	// 	rhi::FenceHandle uploadFence = scene->UploadAttributeData();
	// 	uploadFence->Wait(UINT64_MAX);
	// 	LOG_INFO("Loaded {} splats from {}", splatData->numSplats, filepath);
	// }
	// else
	// {
	// 	CreateTestSplatData();
	// }

	CreateTestSplatData();
}

void GpuSortingRendererApp::CreateTestSplatData()
{
	// Create simple test splats at known Z positions to verify depth calculation
	// Camera is at (0, 0, 5) looking down -Z axis (towards origin)
	const uint32_t testSplatCount = 1000;
	auto           testData       = container::make_shared<engine::SplatSoA>();
	testData->Resize(testSplatCount, 0);        // No SH coefficients for test data

	LOG_INFO("Creating {} test splats for depth calculation verification", testSplatCount);
	LOG_INFO("Camera position: (0, 0, 5), looking towards origin (down -Z axis)");

	// Create splats at different known Z positions
	// Distribute splats from Z=-495 to Z=504 (1000 splats with spacing of 1.0)
	for (uint32_t i = 0; i < testSplatCount; ++i)
	{
		// Place splats in a line along Z axis, all in front of camera
		// Camera is at Z=5, so we place splats from Z=-495 to Z=504
		float z = -495.0f + i * 1.0f;        // Z from -495 to 504

		testData->posX[i] = 0.0f;
		testData->posY[i] = 0.0f;
		testData->posZ[i] = z;

		// View space Z = world_Z - camera_Z = z - 5
		float expectedViewZ = z - 5.0f;
		float expectedDepth = -expectedViewZ;

		// Only log first few, last few, and the special case at z=5
		if (i < 5 || i >= testSplatCount - 5 || i == 500)
		{
			LOG_INFO("  Splat[{}]: World Z={:.1f}, Expected view Z={:.1f}, Expected depth={:.1f}{}",
			         i, z, expectedViewZ, expectedDepth,
			         (i == 500) ? " <- Camera plane (depth=0)" : "");
		}
		else if (i == 5)
		{
			LOG_INFO("  ... (omitting middle entries for brevity) ...");
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
	LOG_INFO("Expected sorting order (far to near):");
	LOG_INFO("  Farthest: Splat[0] at Z=-495.0 (depth=500.0)");
	LOG_INFO("  Camera plane: Splat[500] at Z=5.0 (depth=0.0)");
	LOG_INFO("  Nearest:  Splat[999] at Z=504.0 (depth=-499.0)");
	LOG_INFO("");
	LOG_INFO("Depth distribution:");
	LOG_INFO("  Positive depths (behind camera): Splats 0-499 (500 splats)");
	LOG_INFO("  Zero depth (at camera): Splat 500 (1 splat)");
	LOG_INFO("  Negative depths (in front): Splats 501-999 (499 splats)");

	scene->AddMesh(testData, math::Identity());
	scene->AllocateGpuBuffers();

	rhi::FenceHandle uploadFence = scene->UploadAttributeData();
	uploadFence->Wait(UINT64_MAX);

	LOG_INFO("Created test scene with {} splats", testSplatCount);
}