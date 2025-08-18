#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "core/log.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "core/vfs.h"
#include "rhi.h"

#include <GLFW/glfw3.h>

using namespace msplat;

struct Vertex
{
	math::vec3 position;
	math::vec3 color;
};

struct UniformBufferObject
{
	math::mat4 mvp;               // MVP matrix
	float      time;              // Animation time
	float      padding[3];        // Alignment padding
};

std::vector<uint8_t> LoadShaderCode(const std::string &filename)
{
	return vfs::readFile(filename);
}

int main()
{
	// Initialize GLFW
	if (!glfwInit())
	{
		LOG_FATAL("Failed to initialize GLFW");
		return -1;
	}

	// Create window
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(800, 600, "Triangle Example", nullptr, nullptr);
	if (!window)
	{
		LOG_FATAL("Failed to create window");
		glfwTerminate();
		return -1;
	}

	try
	{
		LOG_INFO("Initializing RHI device");
		// Create RHI device
		auto device = rhi::CreateRHIDevice();

		// Create swapchain
		rhi::SwapchainDesc swapchainDesc{};
		int                fbw, fbh;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		swapchainDesc.windowHandle = window;
		swapchainDesc.width        = static_cast<uint32_t>(fbw);
		swapchainDesc.height       = static_cast<uint32_t>(fbh);
		swapchainDesc.format       = rhi::TextureFormat::R8G8B8A8_UNORM;
		auto swapchain             = device->CreateSwapchain(swapchainDesc);

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
		auto vertexBuffer    = device->CreateBuffer(vbDesc);

		// Create uniform buffer for MVP matrix and animation
		rhi::BufferDesc ubDesc{};
		ubDesc.size                      = sizeof(UniformBufferObject);
		ubDesc.usage                     = rhi::BufferUsage::UNIFORM;
		ubDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
		ubDesc.hints.persistently_mapped = true;
		auto uniformBuffer               = device->CreateBuffer(ubDesc);

		// Load and create shaders
		LOG_INFO("Loading shaders");
		auto vertexCode   = LoadShaderCode("shaders/compiled/triangle.vert.spv");
		auto fragmentCode = LoadShaderCode("shaders/compiled/triangle.frag.spv");

		rhi::ShaderDesc vsDesc{};
		vsDesc.stage      = rhi::ShaderStage::VERTEX;
		vsDesc.code       = vertexCode.data();
		vsDesc.codeSize   = vertexCode.size();
		auto vertexShader = device->CreateShader(vsDesc);

		rhi::ShaderDesc fsDesc{};
		fsDesc.stage        = rhi::ShaderStage::FRAGMENT;
		fsDesc.code         = fragmentCode.data();
		fsDesc.codeSize     = fragmentCode.size();
		auto fragmentShader = device->CreateShader(fsDesc);

		// Create descriptor set layout for uniform buffer
		rhi::DescriptorSetLayoutDesc layoutDesc{};
		layoutDesc.bindings      = {{0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::VERTEX}};
		auto descriptorSetLayout = device->CreateDescriptorSetLayout(layoutDesc);

		// Create descriptor set and bind uniform buffer
		auto descriptorSet = device->CreateDescriptorSet(descriptorSetLayout.get(), rhi::QueueType::GRAPHICS);

		rhi::BufferBinding bufferBinding{};
		bufferBinding.buffer = uniformBuffer.get();
		bufferBinding.offset = 0;
		bufferBinding.range  = 0;        // Whole buffer
		bufferBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
		descriptorSet->BindBuffer(0, bufferBinding);

		// Create pipeline
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShader   = vertexShader.get();
		pipelineDesc.fragmentShader = fragmentShader.get();

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

		pipelineDesc.descriptorSetLayouts = {descriptorSetLayout.get()};

		rhi::PushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags    = rhi::ShaderStageFlags::VERTEX;
		pushConstantRange.offset        = 0;
		pushConstantRange.size          = 16;        // 4 floats (scale, rotation, etc.)
		pipelineDesc.pushConstantRanges = {pushConstantRange};

		auto pipeline = device->CreateGraphicsPipeline(pipelineDesc);

		// Create synchronization objects
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> imageAvailableSemaphores;
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> renderFinishedSemaphores;
		for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			imageAvailableSemaphores.push_back(device->CreateSemaphore());
			renderFinishedSemaphores.push_back(device->CreateSemaphore());
		}
		auto inFlightFence = device->CreateFence(true);

		// Create command lists for each swapchain image
		std::vector<std::unique_ptr<rhi::IRHICommandList>> commandLists;
		for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			commandLists.push_back(device->CreateCommandList());
		}

		// Track per-image state for swapchain images
		// Initially all images are in Undefined state (first use)
		std::vector<bool> imageFirstUse(swapchain->GetImageCount(), true);

		// Main loop
		LOG_INFO("Starting render loop");
		timer::Timer      applicationTimer;
		timer::FPSCounter fpsCounter(1.0);
		applicationTimer.start();

		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			// Wait for previous frame
			inFlightFence->Wait();
			inFlightFence->Reset();

			// Update uniform buffer with animation
			float time = static_cast<float>(applicationTimer.elapsedSeconds());

			UniformBufferObject ubo{};
			// Simple identity matrix for MVP (no transformation)
			ubo.mvp  = math::identity();
			ubo.time = time;

			// Update uniform buffer
			void *uniformData = uniformBuffer->Map();
			memcpy(uniformData, &ubo, sizeof(UniformBufferObject));
			uniformBuffer->Unmap();

			// Acquire next image
			uint32_t             imageIndex;
			rhi::SwapchainStatus acquireStatus = swapchain->AcquireNextImage(imageIndex, imageAvailableSemaphores[0].get());

			if (acquireStatus == rhi::SwapchainStatus::OUT_OF_DATE)
			{
				// Swapchain is out of date, need to recreate it
				LOG_WARNING("Swapchain out of date, recreating");
				int width, height;
				glfwGetFramebufferSize(window, &width, &height);
				while (width == 0 || height == 0)
				{
					glfwGetFramebufferSize(window, &width, &height);
					glfwWaitEvents();
				}
				swapchain->Resize(width, height);

				std::fill(imageFirstUse.begin(), imageFirstUse.end(), true);
				continue;
			}
			else if (acquireStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
			{
				LOG_ERROR("Failed to acquire swapchain image");
				break;
			}

			// Record commands
			auto &cmdList = commandLists[imageIndex];
			cmdList->Reset();
			cmdList->Begin();

			// Query back buffer size (matches swapchain extent)
			auto    *backBuffer       = swapchain->GetBackBuffer(imageIndex);
			uint32_t backBufferWidth  = backBuffer->GetWidth();
			uint32_t backBufferHeight = backBuffer->GetHeight();

			// Transition swapchain image to render target
			// First frame: Undefined -> RenderTarget
			// Subsequent frames: Present -> RenderTarget
			rhi::TextureTransition swapchainTransition{};
			swapchainTransition.texture = backBuffer;
			swapchainTransition.before  = imageFirstUse[imageIndex] ? rhi::ResourceState::Undefined : rhi::ResourceState::Present;
			swapchainTransition.after   = rhi::ResourceState::RenderTarget;

			cmdList->Barrier(
			    rhi::PipelineScope::Graphics,
			    rhi::PipelineScope::Graphics,
			    {},
			    std::array{swapchainTransition},
			    {});

			// Mark this image as no longer first use
			imageFirstUse[imageIndex] = false;

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

			cmdList->SetPipeline(pipeline.get());
			cmdList->SetVertexBuffer(0, vertexBuffer.get());

			cmdList->BindDescriptorSet(0, descriptorSet.get());

			// Calculate projected NDC centroid of the triangle vertices
			// Triangle vertices in object space
			math::vec3 vertices_obj[3] = {
			    math::vec3(-0.5f, -0.5f, 0.0f),
			    math::vec3(0.5f, -0.5f, 0.0f),
			    math::vec3(0.0f, 0.5f, 0.0f)};

			math::vec2 ndc_sum(0.0f);

			for (int i = 0; i < 3; i++)
			{
				const math::vec3 &vertex = vertices_obj[i];

				// clip = MVP * [x y z 1]^T
				math::vec4 clip = ubo.mvp * math::vec4(vertex, 1.0f);

				// NDC per-vertex
				math::vec2 ndc = math::vec2(clip) / clip.w;
				ndc_sum += ndc;
			}

			// Average NDC = true screen-space centroid under perspective
			math::vec2 center_ndc = ndc_sum / 3.0f;

			float aspect = static_cast<float>(backBufferWidth) / static_cast<float>(backBufferHeight);

			float pushData[4] = {
			    center_ndc.x,        // centerNdc.x
			    center_ndc.y,        // centerNdc.y
			    time * 0.5f,         // rotation in radians
			    aspect               // aspect ratio
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
			                           imageAvailableSemaphores[0].get(), renderFinishedSemaphores[imageIndex].get(),
			                           inFlightFence.get());

			// Present
			rhi::SwapchainStatus presentStatus = swapchain->Present(imageIndex, renderFinishedSemaphores[imageIndex].get());

			if (presentStatus == rhi::SwapchainStatus::OUT_OF_DATE || presentStatus == rhi::SwapchainStatus::SUBOPTIMAL)
			{
				// Swapchain needs recreation on next frame
				LOG_WARNING("Swapchain needs recreation (out of date or suboptimal)");
				int width, height;
				glfwGetFramebufferSize(window, &width, &height);
				while (width == 0 || height == 0)
				{
					glfwGetFramebufferSize(window, &width, &height);
					glfwWaitEvents();
				}
				swapchain->Resize(width, height);
				std::fill(imageFirstUse.begin(), imageFirstUse.end(), true);
			}
			else if (presentStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
			{
				LOG_ERROR("Failed to present swapchain image");
				break;
			}

			// FPS counter
			fpsCounter.frame();
			if (fpsCounter.shouldUpdate())
			{
				double fps = fpsCounter.getFPS();
				LOG_INFO("FPS: {}", static_cast<int>(fps + 0.5));
				fpsCounter.reset();
			}
		}

		// Wait for GPU to finish
		LOG_INFO("Waiting for GPU to finish");
		device->WaitIdle();
	}
	catch (const std::exception &e)
	{
		LOG_FATAL("Error: {}", e.what());
		glfwDestroyWindow(window);
		glfwTerminate();
		return -1;
	}

	// Cleanup
	LOG_INFO("Cleaning up and exiting");
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}