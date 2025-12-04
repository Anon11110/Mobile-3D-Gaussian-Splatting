#include "naive_splat_cpu_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#include "engine/splat_loader.h"
#include <stdexcept>

#include <GLFW/glfw3.h>

#include <msplat/core/windows_sanitized.h>

bool NaiveSplatCpuApp::OnInit(app::DeviceManager *deviceManager)
{
	m_deviceManager = deviceManager;
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	LOG_INFO("Initializing NaiveSplatCpuApp");

	// 1. Setup Scene
	m_scene = msplat::container::make_unique<engine::Scene>(device);
	engine::SplatLoader loader;
	auto                future = loader.Load("flowers_1.ply");
	// auto                future    = loader.Load("train_7000.ply");
	auto splatData = future.get();
	if (!splatData || splatData->empty())
	{
		LOG_ERROR("Failed to load splat data.");
		return false;
	}
	m_scene->AddMesh(splatData);
	m_scene->AllocateGpuBuffers();
	auto uploadFence = m_scene->UploadAttributeData();
	if (uploadFence)
	{
		uploadFence->Wait();
		device->RetireCompletedFrame();
	}

	// 2. Create quad index buffer for instanced rendering
	// Uses triangle strip topology: 0, 1, 2, 3 forms two triangles
	// Triangle 1: vertices 0, 1, 2
	// Triangle 2: vertices 1, 2, 3
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

	// 3. Setup Shaders and Pipeline
	m_shaderFactory  = msplat::container::make_unique<engine::ShaderFactory>(device);
	m_vertexShader   = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.vert.spv", rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/splat_raster.frag.spv", rhi::ShaderStage::FRAGMENT);

	// 4. Create UBO
	rhi::BufferDesc uboDesc{};
	uboDesc.size                      = sizeof(FrameUBO);
	uboDesc.usage                     = rhi::BufferUsage::UNIFORM;
	uboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	uboDesc.hints.persistently_mapped = true;
	m_frameUboBuffer                  = device->CreateBuffer(uboDesc);
	m_frameUboDataPtr                 = m_frameUboBuffer->Map();

	// 5. Create Descriptor Set Layout
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

	// 6. Create Descriptor Set
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
	rhi::BufferBinding shBinding{};
	shBinding.buffer = m_scene->GetGpuData().shRest.Get();
	shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(4, shBinding);

	// Binding 5: Sorted Indices
	rhi::BufferBinding indicesBinding{};
	indicesBinding.buffer = m_scene->GetGpuData().sortedIndices.Get();
	indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(5, indicesBinding);

	// 7. Create Graphics Pipeline
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
	m_pipeline                                                = device->CreateGraphicsPipeline(pipelineDesc);

	// 8. Setup Camera
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
	m_camera.SetPerspectiveProjection(45.0f, (float) width / height, 0.1f, 100.0f);
	m_camera.SetPosition({0.0f, 0.0f, 5.0f});

	// 9. Synchronization Primitives
	m_inFlightFences.resize(2);
	m_imageAvailableSemaphores.resize(2);
	for (int i = 0; i < 2; ++i)
	{
		m_inFlightFences[i]           = device->CreateFence(true);
		m_imageAvailableSemaphores[i] = device->CreateSemaphore();
	}
	m_renderFinishedSemaphores.resize(swapchain->GetImageCount());
	for (uint32_t i = 0; i < swapchain->GetImageCount(); ++i)
	{
		m_renderFinishedSemaphores[i] = device->CreateSemaphore();
	}

	m_applicationTimer.start();
	m_fpsCounter = timer::FPSCounter(1.0);

	LOG_INFO("NaiveSplatCpuApp initialized successfully.");
	return true;
}

void NaiveSplatCpuApp::OnUpdate(float deltaTime)
{
	m_camera.Update(deltaTime, m_deviceManager->GetWindow());

	// If camera moved, request a new sort
	m_scene->UpdateView(m_camera.GetViewMatrix());
}

void NaiveSplatCpuApp::OnRender()
{
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	// Check if window is minimized before waiting on fence
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
	if (width == 0 || height == 0)
	{
		return;
	}

	// Wait for the frame in flight to be finished
	m_inFlightFences[m_currentFrame]->Wait();
	m_inFlightFences[m_currentFrame]->Reset();

	m_fpsCounter.frame();

	// Acquire an image from the swap chain
	uint32_t imageIndex;
	auto     acquireStatus = swapchain->AcquireNextImage(imageIndex, m_imageAvailableSemaphores[m_currentFrame].Get());
	if (acquireStatus != rhi::SwapchainStatus::SUCCESS)
	{
		// Handle swapchain recreation if necessary
		return;
	}

	FrameUBO ubo{};
	ubo.view       = m_camera.GetViewMatrix();
	ubo.projection = m_camera.GetProjectionMatrix();
	ubo.projection[1][1] *= -1.0f;        // Flip Y for Vulkan NDC
	ubo.cameraPos          = math::vec4(m_camera.GetPosition(), 1.0f);
	ubo.viewport           = {(float) width, (float) height};
	ubo.focal              = {ubo.projection[0][0] * width * 0.5f,
	                          ubo.projection[1][1] * height * 0.5f};
	ubo.splatScale         = 1.0f;                                 // Default scale factor
	ubo.alphaCullThreshold = 1.0f / 255.0f;                        // Default alpha cutoff
	ubo.maxSplatRadius     = 2048.0f;                              // Maximum splat radius in pixels
	ubo.enableSplatFilter  = 1;                                    // Enable EWA filtering
	ubo.basisViewport      = {1.0f / width, 1.0f / height};        // Resolution-aware scaling
	ubo.inverseFocalAdj    = 1.0f;                                 // No FOV adjustment by default
	memcpy(m_frameUboDataPtr, &ubo, sizeof(FrameUBO));

	// Check for new sorted indices and upload them
	auto uploadFence = m_scene->ConsumeAndUploadSortedIndices();
	if (uploadFence)
	{
		uploadFence->Wait();        // Simple sync for this example
		device->RetireCompletedFrame();
	}

	// Record command buffer
	auto cmdList = device->CreateCommandList();
	cmdList->Begin();

	// Transition swapchain image to render target
	// Using Undefined as source allows driver to skip preserving contents (optimal for CLEAR)
	rhi::TextureTransition renderTransition{};
	renderTransition.texture = swapchain->GetBackBuffer(imageIndex);
	renderTransition.before  = rhi::ResourceState::Undefined;        // Don't care about previous contents
	renderTransition.after   = rhi::ResourceState::RenderTarget;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    {&renderTransition, 1},
	    {});

	rhi::RenderingInfo renderingInfo{};
	renderingInfo.colorAttachments.resize(1);
	renderingInfo.colorAttachments[0].view    = swapchain->GetBackBufferView(imageIndex);
	renderingInfo.colorAttachments[0].loadOp  = rhi::LoadOp::CLEAR;
	renderingInfo.colorAttachments[0].storeOp = rhi::StoreOp::STORE;
	renderingInfo.renderAreaWidth             = width;
	renderingInfo.renderAreaHeight            = height;

	cmdList->BeginRendering(renderingInfo);
	cmdList->SetViewport(0, 0, (float) width, (float) height);
	cmdList->SetScissor(0, 0, width, height);

	// Draw the splats using indexed procedural vertex generation
	cmdList->SetPipeline(m_pipeline.Get());
	cmdList->BindDescriptorSet(0, m_descriptorSet.Get());
	cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

	// Draw using instanced rendering: 4 indices per strip, one instances per splat
	uint32_t indexCount    = 4;
	uint32_t instanceCount = m_scene->GetTotalSplatCount();
	cmdList->DrawIndexedInstanced(indexCount, instanceCount, 0, 0, 0);

	cmdList->EndRendering();

	// Transition swapchain image to present layout
	rhi::TextureTransition presentTransition{};
	presentTransition.texture = swapchain->GetBackBuffer(imageIndex);
	presentTransition.before  = rhi::ResourceState::RenderTarget;
	presentTransition.after   = rhi::ResourceState::Present;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    {&presentTransition, 1},
	    {});

	cmdList->End();

	// Submit to the graphics queue
	rhi::SemaphoreWaitInfo waitInfo   = {m_imageAvailableSemaphores[m_currentFrame].Get(), rhi::StageMask::RenderTarget};
	rhi::IRHISemaphore    *signalSem  = m_renderFinishedSemaphores[imageIndex].Get();
	rhi::IRHICommandList  *cmdListPtr = cmdList.Get();

	rhi::SubmitInfo submitInfo{};
	submitInfo.waitSemaphores   = std::span<const rhi::SemaphoreWaitInfo>(&waitInfo, 1);
	submitInfo.signalSemaphores = std::span<rhi::IRHISemaphore *const>(&signalSem, 1);
	submitInfo.signalFence      = m_inFlightFences[m_currentFrame].Get();

	device->SubmitCommandLists(std::span<rhi::IRHICommandList *const>(&cmdListPtr, 1), rhi::QueueType::GRAPHICS, submitInfo);

	// Present
	swapchain->Present(imageIndex, m_renderFinishedSemaphores[imageIndex].Get());

	m_currentFrame = (m_currentFrame + 1) % 2;

	// Log FPS periodically
	if (m_fpsCounter.shouldUpdate())
	{
		LOG_INFO("Frame FPS: {:.2f} | Sort: CPU | Splats: {}",
		         m_fpsCounter.getFPS(),
		         m_scene->GetTotalSplatCount());
		m_fpsCounter.reset();
	}
}

void NaiveSplatCpuApp::OnShutdown()
{
	LOG_INFO("NaiveSplatCpuApp::OnShutdown");

	if (m_deviceManager && m_deviceManager->GetDevice())
	{
		m_deviceManager->GetDevice()->WaitIdle();

		if (m_frameUboDataPtr)
		{
			m_frameUboBuffer->Unmap();
			m_frameUboDataPtr = nullptr;
		}

		m_inFlightFences.clear();
		m_imageAvailableSemaphores.clear();
		m_renderFinishedSemaphores.clear();

		m_pipeline            = nullptr;
		m_descriptorSet       = nullptr;
		m_descriptorSetLayout = nullptr;

		m_vertexShader   = nullptr;
		m_fragmentShader = nullptr;
		m_shaderFactory  = nullptr;

		m_frameUboBuffer  = nullptr;
		m_quadIndexBuffer = nullptr;

		m_scene.reset();
	}
}

void NaiveSplatCpuApp::OnKey(int key, int action, int mods)
{
	m_camera.OnKey(key, action, mods);

	if (action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
	{
		glfwSetWindowShouldClose(m_deviceManager->GetWindow(), GLFW_TRUE);
	}
}

void NaiveSplatCpuApp::OnMouseButton(int button, int action, int mods)
{
	m_camera.OnMouseButton(button, action, mods);
}

void NaiveSplatCpuApp::OnMouseMove(double xpos, double ypos)
{
	m_camera.OnMouseMove(xpos, ypos);
}
