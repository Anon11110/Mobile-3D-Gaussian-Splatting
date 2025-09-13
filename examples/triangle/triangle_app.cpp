#include <array>
#include <cstring>

#include "app/device_manager.h"
#include "core/log.h"
#include "engine/shader_factory.h"
#include "rhi/rhi.h"
#include "triangle_app.h"

#include <GLFW/glfw3.h>

using namespace msplat;

bool TriangleApp::OnInit(app::DeviceManager *deviceManager)
{
	m_deviceManager = deviceManager;

	LOG_INFO("Initializing Triangle application");

	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	// Create vertex buffer
	Vertex vertices[] = {
	    {math::vec3(-0.5f, -0.5f, 0.0f), math::vec3(1.0f, 0.0f, 0.0f)},
	    {math::vec3(0.5f, -0.5f, 0.0f), math::vec3(0.0f, 1.0f, 0.0f)},
	    {math::vec3(0.0f, 0.5f, 0.0f), math::vec3(0.0f, 0.0f, 1.0f)}};

	rhi::BufferDesc vbDesc{};
	vbDesc.size          = sizeof(vertices);
	vbDesc.usage         = rhi::BufferUsage::VERTEX;
	vbDesc.resourceUsage = rhi::ResourceUsage::Static;
	vbDesc.initialData   = vertices;
	m_vertexBuffer       = device->CreateBuffer(vbDesc);

	// Create uniform buffer for MVP matrix and animation
	rhi::BufferDesc ubDesc{};
	ubDesc.size                      = sizeof(UniformBufferObject);
	ubDesc.usage                     = rhi::BufferUsage::UNIFORM;
	ubDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	ubDesc.hints.persistently_mapped = true;
	m_uniformBuffer                  = device->CreateBuffer(ubDesc);
	m_uniformDataPtr                 = m_uniformBuffer->Map();

	// Create shader factory and load shaders
	m_shaderFactory = container::make_unique<engine::ShaderFactory>(device);

	m_vertexShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/triangle.vert.spv",
	    rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/triangle.frag.spv",
	    rhi::ShaderStage::FRAGMENT);

	// Create descriptor set layout for uniform buffer
	rhi::DescriptorSetLayoutDesc layoutDesc{};
	layoutDesc.bindings   = {{0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::VERTEX}};
	m_descriptorSetLayout = device->CreateDescriptorSetLayout(layoutDesc);

	// Create descriptor set and bind uniform buffer
	m_descriptorSet = device->CreateDescriptorSet(m_descriptorSetLayout.get(), rhi::QueueType::GRAPHICS);

	rhi::BufferBinding bufferBinding{};
	bufferBinding.buffer = m_uniformBuffer.get();
	bufferBinding.offset = 0;
	bufferBinding.range  = 0;        // Whole buffer
	bufferBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_descriptorSet->BindBuffer(0, bufferBinding);

	// Create pipeline
	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertexShader   = m_vertexShader.get();
	pipelineDesc.fragmentShader = m_fragmentShader.get();

	// Vertex layout
	pipelineDesc.vertexLayout.attributes = {
	    {0, 0, rhi::VertexFormat::R32G32B32_SFLOAT, 0},        // position
	    {1, 0, rhi::VertexFormat::R32G32B32_SFLOAT, 12}        // color
	};
	pipelineDesc.vertexLayout.bindings = {{0, sizeof(Vertex), false}};

	pipelineDesc.topology = rhi::PrimitiveTopology::TRIANGLE_LIST;

	pipelineDesc.rasterizationState.cullMode    = rhi::CullMode::BACK;
	pipelineDesc.rasterizationState.frontFace   = rhi::FrontFace::CLOCKWISE;
	pipelineDesc.rasterizationState.polygonMode = rhi::PolygonMode::FILL;

	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable    = false;
	pipelineDesc.colorBlendAttachments[0].colorWriteMask = 0xF;

	pipelineDesc.targetSignature.colorFormats = {swapchain->GetBackBuffer(0)->GetFormat()};
	pipelineDesc.targetSignature.depthFormat  = rhi::TextureFormat::UNDEFINED;
	pipelineDesc.targetSignature.sampleCount  = rhi::SampleCount::COUNT_1;

	pipelineDesc.descriptorSetLayouts = {m_descriptorSetLayout.get()};

	rhi::PushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags    = rhi::ShaderStageFlags::VERTEX;
	pushConstantRange.offset        = 0;
	pushConstantRange.size          = 16;        // 4 floats (scale, rotation, etc.)
	pipelineDesc.pushConstantRanges = {pushConstantRange};

	m_pipeline = device->CreateGraphicsPipeline(pipelineDesc);

	// Create synchronization objects
	uint32_t imageCount = swapchain->GetImageCount();
	m_imageAvailableSemaphores.reserve(imageCount);
	m_renderFinishedSemaphores.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		m_imageAvailableSemaphores.push_back(device->CreateSemaphore());
		m_renderFinishedSemaphores.push_back(device->CreateSemaphore());
	}
	m_inFlightFence = device->CreateFence(true);

	// Create command lists for each swapchain image
	m_commandLists.reserve(imageCount);
	for (uint32_t i = 0; i < imageCount; i++)
	{
		m_commandLists.push_back(device->CreateCommandList());
	}

	// Track per-image state for swapchain images
	m_imageFirstUse.resize(imageCount, true);

	// Initialize timers
	m_applicationTimer.start();
	m_fpsCounter = timer::FPSCounter(1.0);

	LOG_INFO("Triangle application initialized successfully");
	return true;
}

void TriangleApp::OnUpdate(float deltaTime)
{
	// Update uniform buffer with animation
	float time = static_cast<float>(m_applicationTimer.elapsedSeconds());

	UniformBufferObject ubo{};
	// Simple identity matrix for MVP (no transformation)
	ubo.mvp  = math::Identity();
	ubo.time = time;

	// Update uniform buffer
	memcpy(m_uniformDataPtr, &ubo, sizeof(UniformBufferObject));

	// Update FPS counter
	m_fpsCounter.frame();
	if (m_fpsCounter.shouldUpdate())
	{
		double fps = m_fpsCounter.getFPS();
		LOG_INFO("FPS: {}", static_cast<int>(fps + 0.5));
		m_fpsCounter.reset();
	}
}

void TriangleApp::OnRender()
{
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	// Wait for previous frame
	m_inFlightFence->Wait();
	m_inFlightFence->Reset();

	// Acquire next image
	uint32_t             imageIndex;
	rhi::SwapchainStatus acquireStatus = swapchain->AcquireNextImage(imageIndex, m_imageAvailableSemaphores[0].get());

	if (acquireStatus == rhi::SwapchainStatus::OUT_OF_DATE)
	{
		// Swapchain is out of date, need to recreate it
		LOG_WARNING("Swapchain out of date, recreating");
		int width, height;
		glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
			glfwWaitEvents();
		}
		swapchain->Resize(width, height);

		for (auto &firstUse : m_imageFirstUse)
		{
			firstUse = true;
		}
		return;
	}
	else if (acquireStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
	{
		LOG_ERROR("Failed to acquire swapchain image");
		return;
	}

	// Record commands
	auto &cmdList = m_commandLists[imageIndex];
	cmdList->Reset();
	cmdList->Begin();

	// Query back buffer size
	auto    *backBuffer       = swapchain->GetBackBuffer(imageIndex);
	uint32_t backBufferWidth  = backBuffer->GetWidth();
	uint32_t backBufferHeight = backBuffer->GetHeight();

	// Transition swapchain image to render target
	rhi::TextureTransition swapchainTransition{};
	swapchainTransition.texture = backBuffer;
	swapchainTransition.before  = m_imageFirstUse[imageIndex] ? rhi::ResourceState::Undefined : rhi::ResourceState::Present;
	swapchainTransition.after   = rhi::ResourceState::RenderTarget;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    std::array{swapchainTransition},
	    {});

	// Mark this image as no longer first use
	m_imageFirstUse[imageIndex] = false;

	// Begin rendering with dynamic rendering
	rhi::RenderingInfo renderingInfo{};
	renderingInfo.renderAreaX      = 0;
	renderingInfo.renderAreaY      = 0;
	renderingInfo.renderAreaWidth  = backBufferWidth;
	renderingInfo.renderAreaHeight = backBufferHeight;
	renderingInfo.layerCount       = 1;

	// Setup color attachment
	rhi::ColorAttachment colorAttachment{};
	colorAttachment.view       = swapchain->GetBackBufferView(imageIndex);
	colorAttachment.loadOp     = rhi::LoadOp::CLEAR;
	colorAttachment.storeOp    = rhi::StoreOp::STORE;
	colorAttachment.clearValue = {{0.1f, 0.1f, 0.1f, 1.0f}};
	renderingInfo.colorAttachments.push_back(colorAttachment);

	cmdList->BeginRendering(renderingInfo);

	// Viewport and scissor
	cmdList->SetViewport(0, 0, float(backBufferWidth), float(backBufferHeight));
	cmdList->SetScissor(0, 0, backBufferWidth, backBufferHeight);

	cmdList->SetPipeline(m_pipeline.get());
	cmdList->SetVertexBuffer(0, m_vertexBuffer.get());

	cmdList->BindDescriptorSet(0, m_descriptorSet.get());

	// Calculate projected NDC centroid of the triangle vertices
	math::vec3 vertices_obj[3] = {
	    math::vec3(-0.5f, -0.5f, 0.0f),
	    math::vec3(0.5f, -0.5f, 0.0f),
	    math::vec3(0.0f, 0.5f, 0.0f)};

	math::vec2 ndc_sum(0.0f);

	// Get current UBO from mapped memory
	UniformBufferObject *ubo = static_cast<UniformBufferObject *>(m_uniformDataPtr);

	for (int i = 0; i < 3; i++)
	{
		const math::vec3 &vertex = vertices_obj[i];

		// clip = MVP * [x y z 1]^T
		math::vec4 clip = ubo->mvp * math::vec4(vertex, 1.0f);

		// NDC per-vertex
		math::vec2 ndc = math::vec2(clip) / clip.w;
		ndc_sum += ndc;
	}

	// Average NDC = true screen-space centroid under perspective
	math::vec2 center_ndc = ndc_sum / 3.0f;

	float aspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);

	float pushData[4] = {
	    center_ndc.x,            // centerNdc.x
	    center_ndc.y,            // centerNdc.y
	    ubo->time * 0.5f,        // rotation in radians
	    aspect                   // aspect ratio
	};
	cmdList->PushConstants(rhi::ShaderStageFlags::VERTEX, 0,
	                       std::as_bytes(std::span(pushData)));

	// Draw triangle
	cmdList->Draw(3);

	cmdList->EndRendering();

	// Transition swapchain image from render target to present
	swapchainTransition.before = rhi::ResourceState::RenderTarget;
	swapchainTransition.after  = rhi::ResourceState::Present;

	cmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    std::array{swapchainTransition},
	    {});

	cmdList->End();

	// Submit
	auto cmdListSpan = std::array{cmdList.get()};
	device->SubmitCommandLists(cmdListSpan, rhi::QueueType::GRAPHICS,
	                           m_imageAvailableSemaphores[0].get(), m_renderFinishedSemaphores[imageIndex].get(),
	                           m_inFlightFence.get());

	// Present
	rhi::SwapchainStatus presentStatus = swapchain->Present(imageIndex, m_renderFinishedSemaphores[imageIndex].get());

	if (presentStatus == rhi::SwapchainStatus::OUT_OF_DATE || presentStatus == rhi::SwapchainStatus::SUBOPTIMAL)
	{
		// Swapchain needs recreation on next frame
		LOG_WARNING("Swapchain needs recreation (out of date or suboptimal)");
		int width, height;
		glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
		while (width == 0 || height == 0)
		{
			glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
			glfwWaitEvents();
		}
		swapchain->Resize(width, height);
		for (auto &firstUse : m_imageFirstUse)
		{
			firstUse = true;
		}
	}
	else if (presentStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
	{
		LOG_ERROR("Failed to present swapchain image");
	}
}

void TriangleApp::OnShutdown()
{
	LOG_INFO("Shutting down Triangle application");

	// Wait for GPU to finish
	if (m_deviceManager && m_deviceManager->GetDevice())
	{
		m_deviceManager->GetDevice()->WaitIdle();
	}

	// Unmap uniform buffer
	if (m_uniformBuffer && m_uniformDataPtr)
	{
		m_uniformBuffer->Unmap();
		m_uniformDataPtr = nullptr;
	}

	// Clear resources (unique_ptrs will auto-delete)
	m_commandLists.clear();
	m_inFlightFence.reset();
	m_renderFinishedSemaphores.clear();
	m_imageAvailableSemaphores.clear();
	m_pipeline.reset();
	m_descriptorSet.reset();
	m_descriptorSetLayout.reset();
	m_fragmentShader.reset();
	m_vertexShader.reset();
	m_shaderFactory.reset();
	m_uniformBuffer.reset();
	m_vertexBuffer.reset();
}

void TriangleApp::OnKey(int key, int action, int mods)
{
	// Handle keyboard input if needed
	// For now, ESC key can close the application
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
	{
		glfwSetWindowShouldClose(m_deviceManager->GetWindow(), GLFW_TRUE);
	}
}

void TriangleApp::OnMouseButton(int button, int action, int mods)
{
	// Handle mouse button input if needed
}

void TriangleApp::OnMouseMove(double xpos, double ypos)
{
	// Handle mouse movement if needed
}