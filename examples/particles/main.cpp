#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

#include "core/containers/memory.h"        // For container::pmr::GetUpstreamAllocator()
#include "core/log.h"
#include "core/math/math.h"
#include "core/timer.h"
#include "core/vfs.h"
#include "engine/shader_factory.h"
#include "rhi/rhi.h"

#include <GLFW/glfw3.h>

using namespace msplat;

constexpr uint32_t PARTICLE_COUNT       = 10000;
constexpr uint32_t WORKGROUP_SIZE       = 64;
constexpr int      MAX_FRAMES_IN_FLIGHT = 2;
constexpr float    FIXED_TIMESTEP       = 1.0f / 60.0f;

struct Particle
{
	math::vec3 position;
	float      padding1;
	math::vec3 velocity;
	float      padding2;
};

struct SimulationParams
{
	float      deltaTime;
	float      gravity;
	math::vec2 bounds;
};

int main()
{
	if (!glfwInit())
	{
		LOG_FATAL("Failed to initialize GLFW");
		return -1;
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *window = glfwCreateWindow(800, 600, "Particle Simulation", nullptr, nullptr);
	if (!window)
	{
		LOG_FATAL("Failed to create window");
		glfwTerminate();
		return -1;
	}

	try
	{
		LOG_INFO("Initializing RHI device");
		auto device = rhi::CreateRHIDevice();
		LOG_ASSERT(device != nullptr, "Failed to create RHI device");

		rhi::SwapchainDesc swapchainDesc{};
		int                fbw, fbh;
		glfwGetFramebufferSize(window, &fbw, &fbh);
		swapchainDesc.windowHandle = window;
		swapchainDesc.width        = static_cast<uint32_t>(fbw);
		swapchainDesc.height       = static_cast<uint32_t>(fbh);
		swapchainDesc.format       = rhi::TextureFormat::R8G8B8A8_UNORM;
		auto swapchain             = device->CreateSwapchain(swapchainDesc);

		// Initialize particle data
		std::vector<Particle> initialParticles(PARTICLE_COUNT);
		for (uint32_t i = 0; i < PARTICLE_COUNT; ++i)
		{
			float x = (static_cast<float>(i) / PARTICLE_COUNT - 0.5f) * 2.0f;
			float y = 1.0f;
			float z = 0.0f;

			initialParticles[i].position = math::vec3(x, y, z);
			initialParticles[i].velocity = math::vec3(
			    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f,
			    0.0f,
			    (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f);
		}

		// Create particle buffers (double buffered for producer/consumer pattern)
		rhi::BufferDesc particleBufferDesc{};
		particleBufferDesc.size          = sizeof(Particle) * PARTICLE_COUNT;
		particleBufferDesc.usage         = rhi::BufferUsage::STORAGE | rhi::BufferUsage::VERTEX;
		particleBufferDesc.resourceUsage = rhi::ResourceUsage::DynamicUpload;
		particleBufferDesc.initialData   = initialParticles.data();

		auto particleBufferA = device->CreateBuffer(particleBufferDesc);
		auto particleBufferB = device->CreateBuffer(particleBufferDesc);

		// Create simulation parameters buffer
		rhi::BufferDesc paramsBufferDesc{};
		paramsBufferDesc.size                      = sizeof(SimulationParams);
		paramsBufferDesc.usage                     = rhi::BufferUsage::UNIFORM;
		paramsBufferDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
		paramsBufferDesc.hints.persistently_mapped = true;
		auto  paramsBuffer                         = device->CreateBuffer(paramsBufferDesc);
		void *paramsDataPtr                        = paramsBuffer->Map();

		// Create shader factory
		msplat::engine::ShaderFactory shaderFactory(device.get());

		auto computeShader = shaderFactory.getOrCreateShader(
		    "shaders/compiled/particle_compute.comp.spv",
		    rhi::ShaderStage::COMPUTE);

		// Create compute descriptor set layout
		rhi::DescriptorSetLayoutDesc computeLayoutDesc{};
		computeLayoutDesc.bindings = {
		    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},        // params
		    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},        // input particles
		    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE}         // output particles
		};
		auto computeDescriptorSetLayout = device->CreateDescriptorSetLayout(computeLayoutDesc);

		// Create compute pipeline
		rhi::ComputePipelineDesc computePipelineDesc{};
		computePipelineDesc.computeShader        = computeShader.get();
		computePipelineDesc.descriptorSetLayouts = {computeDescriptorSetLayout.get()};
		auto computePipeline                     = device->CreateComputePipeline(computePipelineDesc);

		// Create compute descriptor sets (double buffered)
		auto computeDescriptorSetA = device->CreateDescriptorSet(computeDescriptorSetLayout.get(), rhi::QueueType::COMPUTE);
		auto computeDescriptorSetB = device->CreateDescriptorSet(computeDescriptorSetLayout.get(), rhi::QueueType::COMPUTE);

		// Bind compute descriptor sets
		rhi::BufferBinding paramsBinding{};
		paramsBinding.buffer = paramsBuffer.get();
		paramsBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;

		rhi::BufferBinding inputBindingA{};
		inputBindingA.buffer = particleBufferA.get();
		inputBindingA.type   = rhi::DescriptorType::STORAGE_BUFFER;

		rhi::BufferBinding outputBindingA{};
		outputBindingA.buffer = particleBufferB.get();
		outputBindingA.type   = rhi::DescriptorType::STORAGE_BUFFER;

		rhi::BufferBinding inputBindingB{};
		inputBindingB.buffer = particleBufferB.get();
		inputBindingB.type   = rhi::DescriptorType::STORAGE_BUFFER;

		rhi::BufferBinding outputBindingB{};
		outputBindingB.buffer = particleBufferA.get();
		outputBindingB.type   = rhi::DescriptorType::STORAGE_BUFFER;

		// Set A: reads from A, writes to B
		computeDescriptorSetA->BindBuffer(0, paramsBinding);
		computeDescriptorSetA->BindBuffer(1, inputBindingA);
		computeDescriptorSetA->BindBuffer(2, outputBindingA);

		// Set B: reads from B, writes to A
		computeDescriptorSetB->BindBuffer(0, paramsBinding);
		computeDescriptorSetB->BindBuffer(1, inputBindingB);
		computeDescriptorSetB->BindBuffer(2, outputBindingB);

		auto vertexShader = shaderFactory.getOrCreateShader(
		    "shaders/compiled/particle_render.vert.spv",
		    rhi::ShaderStage::VERTEX);
		auto fragmentShader = shaderFactory.getOrCreateShader(
		    "shaders/compiled/particle_render.frag.spv",
		    rhi::ShaderStage::FRAGMENT);

		// Create MVP uniform buffer
		rhi::BufferDesc mvpBufferDesc{};
		mvpBufferDesc.size                      = sizeof(math::mat4);
		mvpBufferDesc.usage                     = rhi::BufferUsage::UNIFORM;
		mvpBufferDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
		mvpBufferDesc.hints.persistently_mapped = true;
		auto  mvpBuffer                         = device->CreateBuffer(mvpBufferDesc);
		void *mvpDataPtr                        = mvpBuffer->Map();

		// Create graphics descriptor set layout
		rhi::DescriptorSetLayoutDesc graphicsLayoutDesc{};
		graphicsLayoutDesc.bindings = {
		    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::VERTEX}        // MVP matrix
		};
		auto graphicsDescriptorSetLayout = device->CreateDescriptorSetLayout(graphicsLayoutDesc);

		// Create graphics descriptor set
		auto graphicsDescriptorSet = device->CreateDescriptorSet(graphicsDescriptorSetLayout.get(), rhi::QueueType::GRAPHICS);

		rhi::BufferBinding mvpBinding{};
		mvpBinding.buffer = mvpBuffer.get();
		mvpBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
		graphicsDescriptorSet->BindBuffer(0, mvpBinding);

		// Create graphics pipeline
		rhi::GraphicsPipelineDesc pipelineDesc{};
		pipelineDesc.vertexShader   = vertexShader.get();
		pipelineDesc.fragmentShader = fragmentShader.get();

		pipelineDesc.vertexLayout.attributes = {
		    {0, 0, rhi::VertexFormat::R32G32B32_SFLOAT, 0}        // position
		};
		pipelineDesc.vertexLayout.bindings = {{0, sizeof(Particle), false}};

		pipelineDesc.topology = rhi::PrimitiveTopology::POINT_LIST;

		pipelineDesc.rasterizationState.cullMode    = rhi::CullMode::NONE;
		pipelineDesc.rasterizationState.frontFace   = rhi::FrontFace::CLOCKWISE;
		pipelineDesc.rasterizationState.polygonMode = rhi::PolygonMode::FILL;

		pipelineDesc.colorBlendAttachments.resize(1);
		pipelineDesc.colorBlendAttachments[0].blendEnable    = false;
		pipelineDesc.colorBlendAttachments[0].colorWriteMask = 0xF;

		pipelineDesc.targetSignature.colorFormats = {swapchain->GetBackBuffer(0)->GetFormat()};
		pipelineDesc.targetSignature.depthFormat  = rhi::TextureFormat::UNDEFINED;
		pipelineDesc.targetSignature.sampleCount  = rhi::SampleCount::COUNT_1;

		pipelineDesc.descriptorSetLayouts = {graphicsDescriptorSetLayout.get()};

		auto graphicsPipeline = device->CreateGraphicsPipeline(pipelineDesc);

		// Create synchronization objects for frames in flight
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> imageAvailableSemaphores(MAX_FRAMES_IN_FLIGHT);
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> computeFinishedSemaphores(MAX_FRAMES_IN_FLIGHT);
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> graphicsReleasedSemaphores(MAX_FRAMES_IN_FLIGHT);
		std::vector<std::unique_ptr<rhi::IRHIFence>>     inFlightFences(MAX_FRAMES_IN_FLIGHT);

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			imageAvailableSemaphores[i]   = device->CreateSemaphore();
			computeFinishedSemaphores[i]  = device->CreateSemaphore();
			graphicsReleasedSemaphores[i] = device->CreateSemaphore();
			inFlightFences[i]             = device->CreateFence(true);
		}

		// Create render finished semaphores per swapchain image (needed for presentation)
		std::vector<std::unique_ptr<rhi::IRHISemaphore>> renderFinishedSemaphores(swapchain->GetImageCount());
		for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
		{
			renderFinishedSemaphores[i] = device->CreateSemaphore();
		}

		// Create command lists for frames in flight
		std::vector<std::unique_ptr<rhi::IRHICommandList>> computeCommandLists(MAX_FRAMES_IN_FLIGHT);
		std::vector<std::unique_ptr<rhi::IRHICommandList>> graphicsPreCommandLists(MAX_FRAMES_IN_FLIGHT);
		std::vector<std::unique_ptr<rhi::IRHICommandList>> graphicsCommandLists(MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			computeCommandLists[i]     = device->CreateCommandList(rhi::QueueType::COMPUTE);
			graphicsPreCommandLists[i] = device->CreateCommandList(rhi::QueueType::GRAPHICS);
			graphicsCommandLists[i]    = device->CreateCommandList(rhi::QueueType::GRAPHICS);
		}

		std::vector<bool> imageFirstUse(swapchain->GetImageCount(), true);

		// State tracking
		bool     useBufferA   = true;        // true = A->B, false = B->A
		uint32_t currentFrame = 0;
		bool     firstFrame   = true;        // Track first frame for initial transitions

		LOG_INFO("Starting particle simulation");
		timer::Timer      applicationTimer;
		timer::FPSCounter fpsCounter(1.0);
		applicationTimer.start();

		float lastTime    = 0.0f;
		float accumulator = 0.0f;        // Physics accumulator for fixed timestep

		while (!glfwWindowShouldClose(window))
		{
			glfwPollEvents();

			// Wait for the fence of the CURRENT frame to ensure its resources are free
			inFlightFences[currentFrame]->Wait();
			inFlightFences[currentFrame]->Reset();

			float currentTime = static_cast<float>(applicationTimer.elapsedSeconds());
			float deltaTime   = currentTime - lastTime;
			lastTime          = currentTime;

			accumulator += deltaTime;

			// Only run physics simulation when we have accumulated enough time
			bool shouldRunPhysics = accumulator >= FIXED_TIMESTEP;
			if (shouldRunPhysics)
			{
				SimulationParams simParams{};
				simParams.deltaTime = FIXED_TIMESTEP;
				simParams.gravity   = -2.0f;
				simParams.bounds    = math::vec2(2.0f, 2.0f);

				memcpy(paramsDataPtr, &simParams, sizeof(SimulationParams));

				accumulator -= FIXED_TIMESTEP;
			}

			// Get current framebuffer size for correct aspect ratio
			glfwGetFramebufferSize(window, &fbw, &fbh);
			float aspect = static_cast<float>(fbw) / static_cast<float>(fbh);

			// Simple camera setup
			math::mat4 view = math::lookAt(
			    math::vec3(0.0f, 0.0f, 3.0f),         // eye
			    math::vec3(0.0f, 0.0f, 0.0f),         // center
			    math::vec3(0.0f, 1.0f, 0.0f));        // up

			math::mat4 proj = math::perspective(math::radians(45.0f), aspect, 0.1f, 100.0f);
			proj[1][1] *= -1;
			math::mat4 mvp = proj * view;

			// Direct write to persistently mapped buffer
			memcpy(mvpDataPtr, &mvp, sizeof(math::mat4));

			uint32_t imageIndex;
			// Use the semaphore for the CURRENT frame to acquire the image
			rhi::SwapchainStatus acquireStatus = swapchain->AcquireNextImage(imageIndex, imageAvailableSemaphores[currentFrame].get());

			if (acquireStatus == rhi::SwapchainStatus::OUT_OF_DATE)
			{
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
				imageFirstUse.resize(swapchain->GetImageCount(), true);

				// Recreate render finished semaphores if swapchain image count changed
				if (renderFinishedSemaphores.size() != swapchain->GetImageCount())
				{
					renderFinishedSemaphores.clear();
					renderFinishedSemaphores.resize(swapchain->GetImageCount());
					for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
					{
						renderFinishedSemaphores[i] = device->CreateSemaphore();
					}
				}
				continue;
			}
			else if (acquireStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
			{
				LOG_ERROR("Failed to acquire swapchain image");
				break;
			}

			auto *inputBuffer  = useBufferA ? particleBufferA.get() : particleBufferB.get();
			auto *outputBuffer = useBufferA ? particleBufferB.get() : particleBufferA.get();

			// JIT GRAPHICS PRE-SUBMIT: Release input buffer from graphics to compute (only if physics will run)
			bool didPreRelease = false;
			if (shouldRunPhysics && !firstFrame)
			{
				auto &gfxPre = graphicsPreCommandLists[currentFrame];
				gfxPre->Reset();
				gfxPre->Begin();

				rhi::BufferTransition releaseToCompute{};
				releaseToCompute.buffer = inputBuffer;
				releaseToCompute.before = rhi::ResourceState::VertexBuffer;        // last frame's draw state
				releaseToCompute.after  = rhi::ResourceState::VertexBuffer;        // ownership-only transfer

				gfxPre->ReleaseToQueue(rhi::QueueType::COMPUTE, std::array{releaseToCompute}, {});
				gfxPre->End();

				rhi::SubmitInfo preSubmit{};
				preSubmit.signalSemaphores = std::array{graphicsReleasedSemaphores[currentFrame].get()};
				auto gfxPreSpan            = std::array{gfxPre.get()};
				device->SubmitCommandLists(gfxPreSpan, rhi::QueueType::GRAPHICS, preSubmit);

				didPreRelease = true;
			}

			// COMPUTE PHASE (Producer) - Only run when physics should advance
			if (shouldRunPhysics)
			{
				auto &computeCmdList = computeCommandLists[currentFrame];
				computeCmdList->Reset();
				computeCmdList->Begin();

				if (didPreRelease)
				{
					// Acquire exactly the buffer we just released from graphics
					rhi::BufferTransition acquireFromGraphics{};
					acquireFromGraphics.buffer = inputBuffer;
					acquireFromGraphics.before = rhi::ResourceState::VertexBuffer;
					acquireFromGraphics.after  = rhi::ResourceState::ShaderReadWrite;

					computeCmdList->AcquireFromQueue(
					    rhi::QueueType::GRAPHICS,
					    std::array{acquireFromGraphics},
					    {});
				}
				else
				{
					// First compute ever: local init path for both buffers
					rhi::BufferTransition initInput{};
					initInput.buffer = inputBuffer;
					initInput.before = rhi::ResourceState::Undefined;
					initInput.after  = rhi::ResourceState::ShaderReadWrite;

					rhi::BufferTransition initOutput{};
					initOutput.buffer = outputBuffer;
					initOutput.before = rhi::ResourceState::Undefined;
					initOutput.after  = rhi::ResourceState::ShaderReadWrite;

					computeCmdList->Barrier(
					    rhi::PipelineScope::Compute,
					    rhi::PipelineScope::Compute,
					    std::array{initInput, initOutput},
					    {},
					    {});
				}

				computeCmdList->SetPipeline(computePipeline.get());
				computeCmdList->BindDescriptorSet(0, useBufferA ? computeDescriptorSetA.get() : computeDescriptorSetB.get());

				uint32_t workgroupCount = (PARTICLE_COUNT + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
				computeCmdList->Dispatch(workgroupCount);

				// Release the freshly written output to graphics for this frame's draw
				rhi::BufferTransition releaseOutput{};
				releaseOutput.buffer = outputBuffer;
				releaseOutput.before = rhi::ResourceState::ShaderReadWrite;
				releaseOutput.after  = rhi::ResourceState::VertexBuffer;

				computeCmdList->ReleaseToQueue(
				    rhi::QueueType::GRAPHICS,
				    std::array{releaseOutput},
				    {});

				computeCmdList->End();

				// Submit compute; wait only if we actually did the pre-release
				std::vector<rhi::SemaphoreWaitInfo> computeWaits;
				if (didPreRelease)
				{
					computeWaits.push_back({graphicsReleasedSemaphores[currentFrame].get(), rhi::StageMask::ComputeShader});
				}

				rhi::SubmitInfo computeSubmit{};
				computeSubmit.waitSemaphores   = computeWaits;
				computeSubmit.signalSemaphores = std::array{computeFinishedSemaphores[currentFrame].get()};

				auto computeCmdListSpan = std::array{computeCmdList.get()};
				device->SubmitCommandLists(computeCmdListSpan, rhi::QueueType::COMPUTE, computeSubmit);

				useBufferA = !useBufferA;
			}

			// GRAPHICS PHASE (Consumer)
			auto &graphicsCmdList = graphicsCommandLists[currentFrame];
			graphicsCmdList->Reset();
			graphicsCmdList->Begin();

			auto    *backBuffer       = swapchain->GetBackBuffer(imageIndex);
			uint32_t backBufferWidth  = backBuffer->GetWidth();
			uint32_t backBufferHeight = backBuffer->GetHeight();

			// ACQUIRE the buffer that was written by compute (only if physics ran)
			if (shouldRunPhysics)
			{
				rhi::BufferTransition acquireBuffer{};
				acquireBuffer.buffer = outputBuffer;
				acquireBuffer.before = rhi::ResourceState::VertexBuffer;
				acquireBuffer.after  = rhi::ResourceState::VertexBuffer;

				graphicsCmdList->AcquireFromQueue(
				    rhi::QueueType::COMPUTE,
				    std::array{acquireBuffer},
				    {});
			}

			// Transition swapchain image
			rhi::TextureTransition swapchainTransition{};
			swapchainTransition.texture = backBuffer;
			swapchainTransition.before  = imageFirstUse[imageIndex] ? rhi::ResourceState::Undefined : rhi::ResourceState::Present;
			swapchainTransition.after   = rhi::ResourceState::RenderTarget;

			graphicsCmdList->Barrier(
			    rhi::PipelineScope::Graphics,
			    rhi::PipelineScope::Graphics,
			    {},
			    std::array{swapchainTransition},
			    {});

			imageFirstUse[imageIndex] = false;

			// Begin rendering
			rhi::RenderingInfo renderingInfo{};
			renderingInfo.renderAreaX      = 0;
			renderingInfo.renderAreaY      = 0;
			renderingInfo.renderAreaWidth  = backBufferWidth;
			renderingInfo.renderAreaHeight = backBufferHeight;
			renderingInfo.layerCount       = 1;

			rhi::ColorAttachment colorAttachment{};
			colorAttachment.view       = swapchain->GetBackBufferView(imageIndex);
			colorAttachment.loadOp     = rhi::LoadOp::CLEAR;
			colorAttachment.storeOp    = rhi::StoreOp::STORE;
			colorAttachment.clearValue = {{0.0f, 0.0f, 0.1f, 1.0f}};
			renderingInfo.colorAttachments.push_back(colorAttachment);

			graphicsCmdList->BeginRendering(renderingInfo);

			graphicsCmdList->SetViewport(0, 0, float(backBufferWidth), float(backBufferHeight));
			graphicsCmdList->SetScissor(0, 0, backBufferWidth, backBufferHeight);

			graphicsCmdList->SetPipeline(graphicsPipeline.get());
			graphicsCmdList->BindDescriptorSet(0, graphicsDescriptorSet.get());

			// Use the buffer that was just written by compute (or the last valid buffer if physics didn't run)
			auto *renderBuffer = shouldRunPhysics ? outputBuffer : (useBufferA ? particleBufferA.get() : particleBufferB.get());
			graphicsCmdList->SetVertexBuffer(0, renderBuffer);
			graphicsCmdList->Draw(PARTICLE_COUNT);

			graphicsCmdList->EndRendering();

			// Transition swapchain image back to present
			swapchainTransition.before = rhi::ResourceState::RenderTarget;
			swapchainTransition.after  = rhi::ResourceState::Present;

			graphicsCmdList->Barrier(
			    rhi::PipelineScope::Graphics,
			    rhi::PipelineScope::Graphics,
			    {},
			    std::array{swapchainTransition},
			    {});

			graphicsCmdList->End();

			// Submit graphics work - conditionally wait on compute semaphore only if physics ran
			std::vector<rhi::SemaphoreWaitInfo> waitSemaphores;
			waitSemaphores.push_back({imageAvailableSemaphores[currentFrame].get(), rhi::StageMask::RenderTarget});

			if (shouldRunPhysics)
			{
				waitSemaphores.push_back({computeFinishedSemaphores[currentFrame].get(), rhi::StageMask::VertexInput});
			}

			// Build signal semaphores - only signal presentation
			std::vector<rhi::IRHISemaphore *> signalSemaphores;
			signalSemaphores.push_back(renderFinishedSemaphores[imageIndex].get());

			rhi::SubmitInfo submitInfo{};
			submitInfo.waitSemaphores   = std::span<const rhi::SemaphoreWaitInfo>(waitSemaphores.data(), waitSemaphores.size());
			submitInfo.signalSemaphores = signalSemaphores;
			submitInfo.signalFence      = inFlightFences[currentFrame].get();

			auto graphicsCmdListSpan = std::array{graphicsCmdList.get()};
			device->SubmitCommandLists(graphicsCmdListSpan, rhi::QueueType::GRAPHICS, submitInfo);

			// Present - wait on the render finished semaphore for the IMAGE
			rhi::SwapchainStatus presentStatus = swapchain->Present(imageIndex, renderFinishedSemaphores[imageIndex].get());

			if (presentStatus == rhi::SwapchainStatus::OUT_OF_DATE || presentStatus == rhi::SwapchainStatus::SUBOPTIMAL)
			{
				LOG_WARNING("Swapchain needs recreation");
				int width, height;
				glfwGetFramebufferSize(window, &width, &height);
				while (width == 0 || height == 0)
				{
					glfwGetFramebufferSize(window, &width, &height);
					glfwWaitEvents();
				}
				swapchain->Resize(width, height);
				std::fill(imageFirstUse.begin(), imageFirstUse.end(), true);
				imageFirstUse.resize(swapchain->GetImageCount(), true);

				// Recreate render finished semaphores if swapchain image count changed
				if (renderFinishedSemaphores.size() != swapchain->GetImageCount())
				{
					renderFinishedSemaphores.clear();
					renderFinishedSemaphores.resize(swapchain->GetImageCount());
					for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
					{
						renderFinishedSemaphores[i] = device->CreateSemaphore();
					}
				}
			}
			else if (presentStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
			{
				LOG_ERROR("Failed to present swapchain image");
				break;
			}

			firstFrame = false;

			currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

			fpsCounter.frame();
			if (fpsCounter.shouldUpdate())
			{
				double fps = fpsCounter.getFPS();
				LOG_INFO("FPS: {} | Particles: {}", static_cast<int>(fps + 0.5), PARTICLE_COUNT);
				fpsCounter.reset();
			}
		}

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

	LOG_INFO("Cleaning up and exiting");
	glfwDestroyWindow(window);
	glfwTerminate();
	return 0;
}