#include "naive_splat_cpu_app.h"
#include "app/device_manager.h"
#include "core/log.h"
#include "engine/splat_loader.h"
#include <stdexcept>

#include <GLFW/glfw3.h>

#include <msplat/core/windows_sanitized.h>

namespace
{
// A simple quad mesh for instanced rendering
struct QuadVertex
{
	msplat::math::vec2 pos;
};

const msplat::container::vector<QuadVertex> g_quadVertices = {
    {{-1.0f, -1.0f}},
    {{1.0f, -1.0f}},
    {{1.0f, 1.0f}},
    {{-1.0f, 1.0f}},
};

const msplat::container::vector<uint16_t> g_quadIndices = {0, 1, 2, 2, 3, 0};

}        // namespace

bool NaiveSplatCpuApp::OnInit(app::DeviceManager *deviceManager)
{
	m_deviceManager = deviceManager;
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	LOG_INFO("Initializing NaiveSplatCpuApp");

	// 1. Setup Scene
	m_scene = msplat::container::make_unique<engine::Scene>(device);
	engine::SplatLoader loader;
	auto                future    = loader.Load("flowers_1.ply");
	auto                splatData = future.get();
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

	// 2. Create Quad Mesh for Instancing
	rhi::BufferDesc vbDesc{};
	vbDesc.size        = g_quadVertices.size() * sizeof(QuadVertex);
	vbDesc.usage       = rhi::BufferUsage::VERTEX;
	vbDesc.initialData = g_quadVertices.data();
	m_quadVertexBuffer = device->CreateBuffer(vbDesc);

	rhi::BufferDesc ibDesc{};
	ibDesc.size        = g_quadIndices.size() * sizeof(uint16_t);
	ibDesc.usage       = rhi::BufferUsage::INDEX;
	ibDesc.indexType   = rhi::IndexType::UINT16;
	ibDesc.initialData = g_quadIndices.data();
	m_quadIndexBuffer  = device->CreateBuffer(ibDesc);

	// 3. Setup Shaders and Pipeline
	m_shaderFactory  = msplat::container::make_unique<engine::ShaderFactory>(device);
	m_vertexShader   = m_shaderFactory->getOrCreateShader("shaders/compiled/raster.vert.spv", rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader("shaders/compiled/raster.frag.spv", rhi::ShaderStage::FRAGMENT);

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
	    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::ALL_GRAPHICS},
	    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // Positions
	    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // Scales
	    {3, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // Rotations
	    {4, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // SH Coefficients
	    {5, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // Opacities
	    {6, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},        // Sorted Indices
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
	rhi::BufferBinding posBinding{};
	posBinding.buffer = m_scene->GetGpuData().positions.Get();
	posBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(1, posBinding);

	// Binding 2: Scales
	rhi::BufferBinding scaleBinding{};
	scaleBinding.buffer = m_scene->GetGpuData().scales.Get();
	scaleBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(2, scaleBinding);

	// Binding 3: Rotations
	rhi::BufferBinding rotBinding{};
	rotBinding.buffer = m_scene->GetGpuData().rotations.Get();
	rotBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(3, rotBinding);

	// Binding 4: Colors
	rhi::BufferBinding colorBinding{};
	colorBinding.buffer = m_scene->GetGpuData().colors.Get();
	colorBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(4, colorBinding);

	// Binding 5: SH Rest
	rhi::BufferBinding shBinding{};
	shBinding.buffer = m_scene->GetGpuData().shRest.Get();
	shBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(5, shBinding);

	// Binding 6: Sorted Indices
	rhi::BufferBinding indicesBinding{};
	indicesBinding.buffer = m_scene->GetGpuData().sorted_indices.Get();
	indicesBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
	m_descriptorSet->BindBuffer(6, indicesBinding);

	// 7. Create Graphics Pipeline
	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertexShader                = m_vertexShader.Get();
	pipelineDesc.fragmentShader              = m_fragmentShader.Get();
	pipelineDesc.vertexLayout.attributes     = {{0, 0, rhi::VertexFormat::R32G32_SFLOAT, 0}};
	pipelineDesc.vertexLayout.bindings       = {{0, sizeof(QuadVertex), false}};
	pipelineDesc.topology                    = rhi::PrimitiveTopology::TRIANGLE_LIST;
	pipelineDesc.rasterizationState.cullMode = rhi::CullMode::NONE;
	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable         = true;        // Enable additive blending
	pipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::SRC_ALPHA;
	pipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE_MINUS_SRC_ALPHA;
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

	// Wait for the frame in flight to be finished
	m_inFlightFences[m_currentFrame]->Wait();
	m_inFlightFences[m_currentFrame]->Reset();

	// Acquire an image from the swap chain
	uint32_t imageIndex;
	auto     acquireStatus = swapchain->AcquireNextImage(imageIndex, m_imageAvailableSemaphores[m_currentFrame].Get());
	if (acquireStatus != rhi::SwapchainStatus::SUCCESS)
	{
		// Handle swapchain recreation if necessary
		return;
	}

	// Update UBO
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
	FrameUBO ubo{};
	ubo.viewProjection = m_camera.GetViewProjectionMatrix();
	ubo.cameraPos      = math::vec4(m_camera.GetPosition(), 1.0f);
	ubo.viewport       = {(float) width, (float) height};
	ubo.focal          = {m_camera.GetProjectionMatrix()[0][0] * width / 2.0f, m_camera.GetProjectionMatrix()[1][1] * height / 2.0f};
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

	cmdList->SetPipeline(m_pipeline.Get());
	cmdList->BindDescriptorSet(0, m_descriptorSet.Get());
	cmdList->SetVertexBuffer(0, m_quadVertexBuffer.Get());
	cmdList->BindIndexBuffer(m_quadIndexBuffer.Get());

	// Draw the quad mesh instanced for each splat
	cmdList->DrawIndexedInstanced(static_cast<uint32_t>(g_quadIndices.size()), m_scene->GetTotalSplatCount(), 0, 0, 0);

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

	m_fpsCounter.frame();
	if (m_fpsCounter.shouldUpdate())
	{
		LOG_INFO("FPS: {}", m_fpsCounter.getFPS());
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

		m_frameUboBuffer   = nullptr;
		m_quadIndexBuffer  = nullptr;
		m_quadVertexBuffer = nullptr;

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
