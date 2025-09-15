#include <cmath>
#include <cstring>
#include <numbers>

#include "app/device_manager.h"
#include "core/containers/array.h"
#include "core/log.h"
#include "engine/mesh_generator.h"
#include "engine/shader_factory.h"
#include "particles_app.h"
#include "rhi/rhi.h"

#include <GLFW/glfw3.h>

using namespace msplat;

bool ParticlesApp::OnInit(app::DeviceManager *deviceManager)
{
	m_deviceManager = deviceManager;

	LOG_INFO("Initializing Particles application");

	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	// Initialize particle data
	container::vector<Particle> initialParticles;
	initialParticles.resize(PARTICLE_COUNT);
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

	m_particleBufferA = device->CreateBuffer(particleBufferDesc);
	m_particleBufferB = device->CreateBuffer(particleBufferDesc);

	// Create simulation parameters buffer
	rhi::BufferDesc paramsBufferDesc{};
	paramsBufferDesc.size                      = sizeof(SimulationParams);
	paramsBufferDesc.usage                     = rhi::BufferUsage::UNIFORM;
	paramsBufferDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	paramsBufferDesc.hints.persistently_mapped = true;
	m_paramsBuffer                             = device->CreateBuffer(paramsBufferDesc);
	m_paramsDataPtr                            = m_paramsBuffer->Map();

	// Create shader factory
	m_shaderFactory = container::make_unique<engine::ShaderFactory>(device);

	m_computeShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/particle_compute.comp.spv",
	    rhi::ShaderStage::COMPUTE);

	// Create compute descriptor set layout
	rhi::DescriptorSetLayoutDesc computeLayoutDesc{};
	computeLayoutDesc.bindings = {
	    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},        // params
	    {1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE},        // input particles
	    {2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE}         // output particles
	};
	m_computeDescriptorSetLayout = device->CreateDescriptorSetLayout(computeLayoutDesc);

	// Create compute pipeline
	rhi::ComputePipelineDesc computePipelineDesc{};
	computePipelineDesc.computeShader        = m_computeShader.get();
	computePipelineDesc.descriptorSetLayouts = {m_computeDescriptorSetLayout.get()};
	m_computePipeline                        = device->CreateComputePipeline(computePipelineDesc);

	// Create compute descriptor sets (double buffered)
	m_computeDescriptorSetA = device->CreateDescriptorSet(m_computeDescriptorSetLayout.get(), rhi::QueueType::COMPUTE);
	m_computeDescriptorSetB = device->CreateDescriptorSet(m_computeDescriptorSetLayout.get(), rhi::QueueType::COMPUTE);

	// Bind compute descriptor sets
	rhi::BufferBinding paramsBindingCompute{};
	paramsBindingCompute.buffer = m_paramsBuffer.get();
	paramsBindingCompute.type   = rhi::DescriptorType::UNIFORM_BUFFER;

	rhi::BufferBinding inputBindingA{};
	inputBindingA.buffer = m_particleBufferA.get();
	inputBindingA.type   = rhi::DescriptorType::STORAGE_BUFFER;

	rhi::BufferBinding outputBindingA{};
	outputBindingA.buffer = m_particleBufferB.get();
	outputBindingA.type   = rhi::DescriptorType::STORAGE_BUFFER;

	rhi::BufferBinding inputBindingB{};
	inputBindingB.buffer = m_particleBufferB.get();
	inputBindingB.type   = rhi::DescriptorType::STORAGE_BUFFER;

	rhi::BufferBinding outputBindingB{};
	outputBindingB.buffer = m_particleBufferA.get();
	outputBindingB.type   = rhi::DescriptorType::STORAGE_BUFFER;

	// Set A: reads from A, writes to B
	m_computeDescriptorSetA->BindBuffer(0, paramsBindingCompute);
	m_computeDescriptorSetA->BindBuffer(1, inputBindingA);
	m_computeDescriptorSetA->BindBuffer(2, outputBindingA);

	// Set B: reads from B, writes to A
	m_computeDescriptorSetB->BindBuffer(0, paramsBindingCompute);
	m_computeDescriptorSetB->BindBuffer(1, inputBindingB);
	m_computeDescriptorSetB->BindBuffer(2, outputBindingB);

	// --- Graphics Resources for Particles ---
	m_vertexShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/particle_render.vert.spv",
	    rhi::ShaderStage::VERTEX);
	m_fragmentShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/particle_render.frag.spv",
	    rhi::ShaderStage::FRAGMENT);

	// Create MVP uniform buffer
	rhi::BufferDesc mvpBufferDesc{};
	mvpBufferDesc.size                      = sizeof(math::mat4);
	mvpBufferDesc.usage                     = rhi::BufferUsage::UNIFORM;
	mvpBufferDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	mvpBufferDesc.hints.persistently_mapped = true;
	m_mvpBuffer                             = device->CreateBuffer(mvpBufferDesc);
	m_mvpDataPtr                            = m_mvpBuffer->Map();

	// Create graphics descriptor set layout
	rhi::DescriptorSetLayoutDesc graphicsLayoutDesc{};
	graphicsLayoutDesc.bindings = {
	    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::VERTEX},         // MVP matrix
	    {1, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::FRAGMENT}        // Simulation params for time
	};
	m_graphicsDescriptorSetLayout = device->CreateDescriptorSetLayout(graphicsLayoutDesc);

	// Create graphics descriptor set
	m_graphicsDescriptorSet = device->CreateDescriptorSet(m_graphicsDescriptorSetLayout.get(), rhi::QueueType::GRAPHICS);

	rhi::BufferBinding mvpBinding{};
	mvpBinding.buffer = m_mvpBuffer.get();
	mvpBinding.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_graphicsDescriptorSet->BindBuffer(0, mvpBinding);

	// Also bind the simulation parameters buffer for the fragment shader
	rhi::BufferBinding paramsBindingGfx{};
	paramsBindingGfx.buffer = m_paramsBuffer.get();
	paramsBindingGfx.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_graphicsDescriptorSet->BindBuffer(1, paramsBindingGfx);

	// Create graphics pipeline
	rhi::GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertexShader   = m_vertexShader.get();
	pipelineDesc.fragmentShader = m_fragmentShader.get();

	pipelineDesc.vertexLayout.attributes = {
	    {0, 0, rhi::VertexFormat::R32G32B32_SFLOAT, offsetof(Particle, position)}        // position
	};
	pipelineDesc.vertexLayout.bindings = {{0, sizeof(Particle), false}};

	pipelineDesc.topology = rhi::PrimitiveTopology::POINT_LIST;

	pipelineDesc.rasterizationState.cullMode    = rhi::CullMode::NONE;
	pipelineDesc.rasterizationState.frontFace   = rhi::FrontFace::CLOCKWISE;
	pipelineDesc.rasterizationState.polygonMode = rhi::PolygonMode::FILL;

	pipelineDesc.colorBlendAttachments.resize(1);
	pipelineDesc.colorBlendAttachments[0].blendEnable         = true;
	pipelineDesc.colorBlendAttachments[0].srcColorBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].dstColorBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].colorBlendOp        = rhi::BlendOp::ADD;
	pipelineDesc.colorBlendAttachments[0].srcAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].dstAlphaBlendFactor = rhi::BlendFactor::ONE;
	pipelineDesc.colorBlendAttachments[0].alphaBlendOp        = rhi::BlendOp::ADD;
	pipelineDesc.colorBlendAttachments[0].colorWriteMask      = 0xF;

	pipelineDesc.targetSignature.colorFormats = {swapchain->GetBackBuffer(0)->GetFormat()};
	pipelineDesc.targetSignature.depthFormat  = rhi::TextureFormat::UNDEFINED;
	pipelineDesc.targetSignature.sampleCount  = rhi::SampleCount::COUNT_1;

	pipelineDesc.descriptorSetLayouts = {m_graphicsDescriptorSetLayout.get()};

	m_graphicsPipeline = device->CreateGraphicsPipeline(pipelineDesc);

	// --- Debug Sphere Resources ---
	// Always initialize debug resources for runtime toggling

	m_debugVertexShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/debug_sphere.vert.spv",
	    rhi::ShaderStage::VERTEX);
	m_debugFragmentShader = m_shaderFactory->getOrCreateShader(
	    "shaders/compiled/debug_sphere.frag.spv",
	    rhi::ShaderStage::FRAGMENT);

	// Generate sphere wireframe with icosahedron subdivision
	auto sphereMesh = engine::GenerateIcosphereWireframe(2);
	m_sphereIndices = sphereMesh.indices;

	rhi::BufferDesc sphereVbDesc{};
	sphereVbDesc.size          = sizeof(math::vec3) * sphereMesh.vertices.size();
	sphereVbDesc.usage         = rhi::BufferUsage::VERTEX;
	sphereVbDesc.resourceUsage = rhi::ResourceUsage::DynamicUpload;
	sphereVbDesc.initialData   = sphereMesh.vertices.data();
	m_sphereVertexBuffer       = device->CreateBuffer(sphereVbDesc);

	rhi::BufferDesc sphereIbDesc{};
	sphereIbDesc.size          = sizeof(uint16_t) * m_sphereIndices.size();
	sphereIbDesc.usage         = rhi::BufferUsage::INDEX;
	sphereIbDesc.resourceUsage = rhi::ResourceUsage::DynamicUpload;
	sphereIbDesc.indexType     = rhi::IndexType::UINT16;        // Explicitly specify UINT16
	sphereIbDesc.initialData   = m_sphereIndices.data();
	m_sphereIndexBuffer        = device->CreateBuffer(sphereIbDesc);

	// Descriptor set for the debug sphere's UBO
	rhi::DescriptorSetLayoutDesc debugLayoutDesc{};
	debugLayoutDesc.bindings = {
	    {0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::VERTEX}};
	m_debugDescriptorSetLayout = device->CreateDescriptorSetLayout(debugLayoutDesc);

	// Create two separate descriptor sets and uniform buffers for each sphere
	m_debugDescriptorSet1 = device->CreateDescriptorSet(m_debugDescriptorSetLayout.get(), rhi::QueueType::GRAPHICS);
	m_debugDescriptorSet2 = device->CreateDescriptorSet(m_debugDescriptorSetLayout.get(), rhi::QueueType::GRAPHICS);

	rhi::BufferDesc debugUboDesc{};
	debugUboDesc.size                      = sizeof(DebugUBO);
	debugUboDesc.usage                     = rhi::BufferUsage::UNIFORM;
	debugUboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	debugUboDesc.hints.persistently_mapped = true;

	// Create separate buffers for each sphere
	m_debugUboBuffer1  = device->CreateBuffer(debugUboDesc);
	m_debugUboDataPtr1 = m_debugUboBuffer1->Map();

	m_debugUboBuffer2  = device->CreateBuffer(debugUboDesc);
	m_debugUboDataPtr2 = m_debugUboBuffer2->Map();

	// Bind buffers to their respective descriptor sets
	rhi::BufferBinding debugUboBinding1{};
	debugUboBinding1.buffer = m_debugUboBuffer1.get();
	debugUboBinding1.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_debugDescriptorSet1->BindBuffer(0, debugUboBinding1);

	rhi::BufferBinding debugUboBinding2{};
	debugUboBinding2.buffer = m_debugUboBuffer2.get();
	debugUboBinding2.type   = rhi::DescriptorType::UNIFORM_BUFFER;
	m_debugDescriptorSet2->BindBuffer(0, debugUboBinding2);

	// Pipeline for the debug sphere (wireframe)
	rhi::GraphicsPipelineDesc debugPipelineDesc{};
	debugPipelineDesc.vertexShader                   = m_debugVertexShader.get();
	debugPipelineDesc.fragmentShader                 = m_debugFragmentShader.get();
	debugPipelineDesc.vertexLayout.attributes        = {{0, 0, rhi::VertexFormat::R32G32B32_SFLOAT, 0}};
	debugPipelineDesc.vertexLayout.bindings          = {{0, sizeof(math::vec3), false}};
	debugPipelineDesc.topology                       = rhi::PrimitiveTopology::LINE_LIST;
	debugPipelineDesc.rasterizationState.polygonMode = rhi::PolygonMode::FILL;        // FILL mode required for LINE_LIST primitives
	debugPipelineDesc.rasterizationState.cullMode    = rhi::CullMode::NONE;           // No culling for lines
	debugPipelineDesc.colorBlendAttachments.resize(1);
	debugPipelineDesc.colorBlendAttachments[0].colorWriteMask = 0xF;
	debugPipelineDesc.targetSignature.colorFormats            = {swapchain->GetBackBuffer(0)->GetFormat()};
	debugPipelineDesc.descriptorSetLayouts                    = {m_debugDescriptorSetLayout.get()};
	m_debugPipeline                                           = device->CreateGraphicsPipeline(debugPipelineDesc);

	// Create synchronization objects for frames in flight
	m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	m_computeFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	m_graphicsReleasedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
	m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_imageAvailableSemaphores[i]   = device->CreateSemaphore();
		m_computeFinishedSemaphores[i]  = device->CreateSemaphore();
		m_graphicsReleasedSemaphores[i] = device->CreateSemaphore();
		m_inFlightFences[i]             = device->CreateFence(true);
	}

	// Create render finished semaphores per swapchain image (needed for presentation)
	m_renderFinishedSemaphores.resize(swapchain->GetImageCount());
	for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
	{
		m_renderFinishedSemaphores[i] = device->CreateSemaphore();
	}

	// Create command lists for frames in flight
	m_computeCommandLists.resize(MAX_FRAMES_IN_FLIGHT);
	m_graphicsPreCommandLists.resize(MAX_FRAMES_IN_FLIGHT);
	m_graphicsCommandLists.resize(MAX_FRAMES_IN_FLIGHT);
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		m_computeCommandLists[i]     = device->CreateCommandList(rhi::QueueType::COMPUTE);
		m_graphicsPreCommandLists[i] = device->CreateCommandList(rhi::QueueType::GRAPHICS);
		m_graphicsCommandLists[i]    = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	}

	m_imageFirstUse.resize(swapchain->GetImageCount(), true);

	// Initialize timers
	m_applicationTimer.start();
	m_fpsCounter = timer::FPSCounter(1.0);

	// Initialize camera
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
	float aspect = static_cast<float>(width) / static_cast<float>(height);
	m_camera.SetPerspectiveProjection(45.0f, aspect, 0.1f, 100.0f);
	m_camera.SetPosition(math::vec3(0.0f, 0.0f, 6.0f));
	m_camera.SetTarget(math::vec3(0.0f, 0.0f, 0.0f));

	LOG_INFO("Particles application initialized successfully");
	return true;
}

void ParticlesApp::OnUpdate(float deltaTime)
{
	// Update camera
	m_camera.Update(deltaTime, m_deviceManager->GetWindow());

	// Update aspect ratio if window resized
	int width, height;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &width, &height);
	if (width > 0 && height > 0)
	{
		float aspect = static_cast<float>(width) / static_cast<float>(height);
		m_camera.SetPerspectiveProjection(45.0f, aspect, 0.1f, 100.0f);
	}
	float currentTime = static_cast<float>(m_applicationTimer.elapsedSeconds());
	float frameDelta  = currentTime - m_lastTime;
	m_lastTime        = currentTime;

	m_accumulator += frameDelta;

	// Update emission sphere positions (bounce within box boundaries)
	// Using the fixed timestep for consistent physics
	if (m_accumulator >= FIXED_TIMESTEP)
	{
		// Always update sphere physics for smooth runtime toggling
		// Update sphere 1 position
		m_sphere1Pos += m_sphere1Vel * FIXED_TIMESTEP;

		// Bounce sphere 1 off walls using msplat::math functions
		if (math::Abs(m_sphere1Pos.x) > m_wallBoundsX - m_sphereRadius)
		{
			m_sphere1Pos.x = math::Sign(m_sphere1Pos.x) * (m_wallBoundsX - m_sphereRadius);
			m_sphere1Vel.x = -m_sphere1Vel.x * 0.9f;
		}
		if (math::Abs(m_sphere1Pos.y) > m_wallBoundsY - m_sphereRadius)
		{
			m_sphere1Pos.y = math::Sign(m_sphere1Pos.y) * (m_wallBoundsY - m_sphereRadius);
			m_sphere1Vel.y = -m_sphere1Vel.y * 0.9f;
		}
		if (math::Abs(m_sphere1Pos.z) > m_wallBoundsZ - m_sphereRadius)
		{
			m_sphere1Pos.z = math::Sign(m_sphere1Pos.z) * (m_wallBoundsZ - m_sphereRadius);
			m_sphere1Vel.z = -m_sphere1Vel.z * 0.9f;
		}

		// Update sphere 2 position
		m_sphere2Pos += m_sphere2Vel * FIXED_TIMESTEP;

		// Bounce sphere 2 off walls using msplat::math functions
		if (math::Abs(m_sphere2Pos.x) > m_wallBoundsX - m_sphereRadius)
		{
			m_sphere2Pos.x = math::Sign(m_sphere2Pos.x) * (m_wallBoundsX - m_sphereRadius);
			m_sphere2Vel.x = -m_sphere2Vel.x * 0.9f;
		}
		if (math::Abs(m_sphere2Pos.y) > m_wallBoundsY - m_sphereRadius)
		{
			m_sphere2Pos.y = math::Sign(m_sphere2Pos.y) * (m_wallBoundsY - m_sphereRadius);
			m_sphere2Vel.y = -m_sphere2Vel.y * 0.9f;
		}
		if (math::Abs(m_sphere2Pos.z) > m_wallBoundsZ - m_sphereRadius)
		{
			m_sphere2Pos.z = math::Sign(m_sphere2Pos.z) * (m_wallBoundsZ - m_sphereRadius);
			m_sphere2Vel.z = -m_sphere2Vel.z * 0.9f;
		}
	}
}

void ParticlesApp::OnRender()
{
	auto *device    = m_deviceManager->GetDevice();
	auto *swapchain = m_deviceManager->GetSwapchain();

	// Wait for the fence of the CURRENT frame to ensure its resources are free
	m_inFlightFences[m_currentFrame]->Wait();
	m_inFlightFences[m_currentFrame]->Reset();

	// Only run physics simulation when we have accumulated enough time
	bool shouldRunPhysics = m_accumulator >= FIXED_TIMESTEP;
	if (shouldRunPhysics)
	{
		SimulationParams simParams{};
		simParams.deltaTime    = FIXED_TIMESTEP;
		simParams.time         = static_cast<float>(m_applicationTimer.elapsedSeconds());
		simParams.bounds       = math::vec2(m_wallBoundsX, m_wallBoundsZ);
		simParams.sphere1Pos   = m_sphere1Pos;
		simParams.sphereRadius = m_sphereRadius;
		simParams.sphere1Vel   = m_sphere1Vel;
		simParams.sphere2Pos   = m_sphere2Pos;
		simParams.sphere2Vel   = m_sphere2Vel;

		memcpy(m_paramsDataPtr, &simParams, sizeof(SimulationParams));

		m_accumulator -= FIXED_TIMESTEP;
	}

	// Get current framebuffer size for correct aspect ratio
	int fbw, fbh;
	glfwGetFramebufferSize(m_deviceManager->GetWindow(), &fbw, &fbh);
	float aspect = static_cast<float>(fbw) / static_cast<float>(fbh);

	// Get camera matrices
	math::mat4 mvp = m_camera.GetViewProjectionMatrix();

	// Direct write to persistently mapped buffer
	memcpy(m_mvpDataPtr, &mvp, sizeof(math::mat4));

	auto *inputBuffer  = m_useBufferA ? m_particleBufferA.get() : m_particleBufferB.get();
	auto *outputBuffer = m_useBufferA ? m_particleBufferB.get() : m_particleBufferA.get();

	// JIT GRAPHICS PRE-SUBMIT: Release input buffer from graphics to compute (only if physics will run)
	bool didPreRelease = false;
	if (shouldRunPhysics && !m_firstFrame)
	{
		auto &gfxPre = m_graphicsPreCommandLists[m_currentFrame];
		gfxPre->Reset();
		gfxPre->Begin();

		rhi::BufferTransition releaseToCompute{};
		releaseToCompute.buffer = inputBuffer;
		releaseToCompute.before = rhi::ResourceState::VertexBuffer;        // last frame's draw state
		releaseToCompute.after  = rhi::ResourceState::VertexBuffer;        // ownership-only transfer

		gfxPre->ReleaseToQueue(rhi::QueueType::COMPUTE, container::array<rhi::BufferTransition, 1>{releaseToCompute}, {});
		gfxPre->End();

		rhi::SubmitInfo preSubmit{};
		preSubmit.signalSemaphores = container::array<rhi::IRHISemaphore *, 1>{m_graphicsReleasedSemaphores[m_currentFrame].get()};
		auto gfxPreSpan            = container::array<rhi::IRHICommandList *, 1>{gfxPre.get()};
		device->SubmitCommandLists(gfxPreSpan, rhi::QueueType::GRAPHICS, preSubmit);

		didPreRelease = true;
	}

	// COMPUTE PHASE (Producer) - Only run when physics should advance
	if (shouldRunPhysics)
	{
		auto &computeCmdList = m_computeCommandLists[m_currentFrame];
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
			    container::array<rhi::BufferTransition, 1>{acquireFromGraphics},
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
			    container::array<rhi::BufferTransition, 2>{initInput, initOutput},
			    {},
			    {});
		}

		computeCmdList->SetPipeline(m_computePipeline.get());
		computeCmdList->BindDescriptorSet(0, m_useBufferA ? m_computeDescriptorSetA.get() : m_computeDescriptorSetB.get());

		uint32_t workgroupCount = (PARTICLE_COUNT + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
		computeCmdList->Dispatch(workgroupCount);

		// Release the freshly written output to graphics for this frame's draw
		rhi::BufferTransition releaseOutput{};
		releaseOutput.buffer = outputBuffer;
		releaseOutput.before = rhi::ResourceState::ShaderReadWrite;
		releaseOutput.after  = rhi::ResourceState::VertexBuffer;

		computeCmdList->ReleaseToQueue(
		    rhi::QueueType::GRAPHICS,
		    container::array<rhi::BufferTransition, 1>{releaseOutput},
		    {});

		computeCmdList->End();

		// Submit compute; wait only if we actually did the pre-release
		container::vector<rhi::SemaphoreWaitInfo> computeWaits;
		if (didPreRelease)
		{
			computeWaits.push_back({m_graphicsReleasedSemaphores[m_currentFrame].get(), rhi::StageMask::ComputeShader});
		}

		rhi::SubmitInfo computeSubmit{};
		computeSubmit.waitSemaphores   = computeWaits;
		computeSubmit.signalSemaphores = container::array<rhi::IRHISemaphore *, 1>{m_computeFinishedSemaphores[m_currentFrame].get()};

		auto computeCmdListSpan = container::array<rhi::IRHICommandList *, 1>{computeCmdList.get()};
		device->SubmitCommandLists(computeCmdListSpan, rhi::QueueType::COMPUTE, computeSubmit);

		m_useBufferA = !m_useBufferA;
	}

	uint32_t imageIndex;
	// Use the semaphore for the CURRENT frame to acquire the image
	rhi::SwapchainStatus acquireStatus = swapchain->AcquireNextImage(imageIndex, m_imageAvailableSemaphores[m_currentFrame].get());

	if (acquireStatus == rhi::SwapchainStatus::OUT_OF_DATE)
	{
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
		m_imageFirstUse.resize(swapchain->GetImageCount(), true);

		// Recreate render finished semaphores if swapchain image count changed
		if (m_renderFinishedSemaphores.size() != swapchain->GetImageCount())
		{
			m_renderFinishedSemaphores.clear();
			m_renderFinishedSemaphores.resize(swapchain->GetImageCount());
			for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
			{
				m_renderFinishedSemaphores[i] = device->CreateSemaphore();
			}
		}
		return;
	}
	else if (acquireStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
	{
		LOG_ERROR("Failed to acquire swapchain image");
		return;
	}

	// GRAPHICS PHASE (Consumer)
	auto &graphicsCmdList = m_graphicsCommandLists[m_currentFrame];
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
		    container::array<rhi::BufferTransition, 1>{acquireBuffer},
		    {});
	}

	// Transition swapchain image
	rhi::TextureTransition swapchainTransition{};
	swapchainTransition.texture = backBuffer;
	swapchainTransition.before  = m_imageFirstUse[imageIndex] ? rhi::ResourceState::Undefined : rhi::ResourceState::Present;
	swapchainTransition.after   = rhi::ResourceState::RenderTarget;

	graphicsCmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    container::array<rhi::TextureTransition, 1>{swapchainTransition},
	    {});

	m_imageFirstUse[imageIndex] = false;

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
	colorAttachment.clearValue = {{0.05f, 0.0f, 0.1f, 1.0f}};
	renderingInfo.colorAttachments.push_back(colorAttachment);

	graphicsCmdList->BeginRendering(renderingInfo);

	graphicsCmdList->SetViewport(0, 0, float(backBufferWidth), float(backBufferHeight));
	graphicsCmdList->SetScissor(0, 0, backBufferWidth, backBufferHeight);

	// --- Draw Particles ---
	graphicsCmdList->SetPipeline(m_graphicsPipeline.get());
	graphicsCmdList->BindDescriptorSet(0, m_graphicsDescriptorSet.get());

	// Use the buffer that was just written by compute (or the last valid buffer if physics didn't run)
	auto *renderBuffer = shouldRunPhysics ? outputBuffer : (m_useBufferA ? m_particleBufferA.get() : m_particleBufferB.get());
	graphicsCmdList->SetVertexBuffer(0, renderBuffer);
	graphicsCmdList->Draw(PARTICLE_COUNT);

	// --- Draw Debug Spheres ---
	if (m_enableDebugSpheres)
	{
		graphicsCmdList->SetPipeline(m_debugPipeline.get());
		graphicsCmdList->SetVertexBuffer(0, m_sphereVertexBuffer.get());
		graphicsCmdList->BindIndexBuffer(m_sphereIndexBuffer.get(), 0);

		// Prepare and draw sphere 1
		DebugUBO debugUbo1;
		debugUbo1.mvp = mvp * math::Translate(math::mat4(1.0f), m_sphere1Pos) * math::Scale(math::mat4(1.0f), math::vec3(m_sphereRadius));
		memcpy(m_debugUboDataPtr1, &debugUbo1, sizeof(DebugUBO));

		graphicsCmdList->BindDescriptorSet(0, m_debugDescriptorSet1.get());

		graphicsCmdList->DrawIndexed((uint32_t) m_sphereIndices.size());

		// Prepare and draw sphere 2
		DebugUBO debugUbo2;
		debugUbo2.mvp = mvp * math::Translate(math::mat4(1.0f), m_sphere2Pos) * math::Scale(math::mat4(1.0f), math::vec3(m_sphereRadius));
		memcpy(m_debugUboDataPtr2, &debugUbo2, sizeof(DebugUBO));

		graphicsCmdList->BindDescriptorSet(0, m_debugDescriptorSet2.get());

		graphicsCmdList->DrawIndexed((uint32_t) m_sphereIndices.size());
	}

	graphicsCmdList->EndRendering();

	// Transition swapchain image back to present
	swapchainTransition.before = rhi::ResourceState::RenderTarget;
	swapchainTransition.after  = rhi::ResourceState::Present;

	graphicsCmdList->Barrier(
	    rhi::PipelineScope::Graphics,
	    rhi::PipelineScope::Graphics,
	    {},
	    container::array<rhi::TextureTransition, 1>{swapchainTransition},
	    {});

	graphicsCmdList->End();

	// Submit graphics work - conditionally wait on compute semaphore only if physics ran
	container::vector<rhi::SemaphoreWaitInfo> waitSemaphores;
	waitSemaphores.push_back({m_imageAvailableSemaphores[m_currentFrame].get(), rhi::StageMask::RenderTarget});

	if (shouldRunPhysics)
	{
		waitSemaphores.push_back({m_computeFinishedSemaphores[m_currentFrame].get(), rhi::StageMask::VertexInput});
	}

	// Build signal semaphores - only signal presentation
	container::vector<rhi::IRHISemaphore *> signalSemaphores;
	signalSemaphores.push_back(m_renderFinishedSemaphores[imageIndex].get());

	rhi::SubmitInfo submitInfo{};
	submitInfo.waitSemaphores   = container::span<const rhi::SemaphoreWaitInfo>(waitSemaphores.data(), waitSemaphores.size());
	submitInfo.signalSemaphores = signalSemaphores;
	submitInfo.signalFence      = m_inFlightFences[m_currentFrame].get();

	auto graphicsCmdListSpan = container::array<rhi::IRHICommandList *, 1>{graphicsCmdList.get()};
	device->SubmitCommandLists(graphicsCmdListSpan, rhi::QueueType::GRAPHICS, submitInfo);

	// Present - wait on the render finished semaphore for the IMAGE
	rhi::SwapchainStatus presentStatus = swapchain->Present(imageIndex, m_renderFinishedSemaphores[imageIndex].get());

	if (presentStatus == rhi::SwapchainStatus::OUT_OF_DATE || presentStatus == rhi::SwapchainStatus::SUBOPTIMAL)
	{
		LOG_WARNING("Swapchain needs recreation");
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
		m_imageFirstUse.resize(swapchain->GetImageCount(), true);

		// Recreate render finished semaphores if swapchain image count changed
		if (m_renderFinishedSemaphores.size() != swapchain->GetImageCount())
		{
			m_renderFinishedSemaphores.clear();
			m_renderFinishedSemaphores.resize(swapchain->GetImageCount());
			for (uint32_t i = 0; i < swapchain->GetImageCount(); i++)
			{
				m_renderFinishedSemaphores[i] = device->CreateSemaphore();
			}
		}
	}
	else if (presentStatus == rhi::SwapchainStatus::ERROR_OCCURRED)
	{
		LOG_ERROR("Failed to present swapchain image");
	}

	m_firstFrame = false;

	m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

	m_fpsCounter.frame();
	if (m_fpsCounter.shouldUpdate())
	{
		double fps = m_fpsCounter.getFPS();
		LOG_INFO("FPS: {} | Particles: {}", static_cast<int>(fps + 0.5), PARTICLE_COUNT);
		m_fpsCounter.reset();
	}
}

void ParticlesApp::OnShutdown()
{
	LOG_INFO("Shutting down Particles application");

	// Wait for GPU to finish
	if (m_deviceManager && m_deviceManager->GetDevice())
	{
		m_deviceManager->GetDevice()->WaitIdle();
	}

	// Unmap buffers
	if (m_paramsBuffer && m_paramsDataPtr)
	{
		m_paramsBuffer->Unmap();
		m_paramsDataPtr = nullptr;
	}
	if (m_mvpBuffer && m_mvpDataPtr)
	{
		m_mvpBuffer->Unmap();
		m_mvpDataPtr = nullptr;
	}

	// Clean up debug sphere resources (always initialized)
	if (m_debugUboBuffer1 && m_debugUboDataPtr1)
	{
		m_debugUboBuffer1->Unmap();
		m_debugUboDataPtr1 = nullptr;
	}
	if (m_debugUboBuffer2 && m_debugUboDataPtr2)
	{
		m_debugUboBuffer2->Unmap();
		m_debugUboDataPtr2 = nullptr;
	}

	// Clear resources (unique_ptrs will auto-delete)
	m_graphicsCommandLists.clear();
	m_graphicsPreCommandLists.clear();
	m_computeCommandLists.clear();
	m_renderFinishedSemaphores.clear();
	m_inFlightFences.clear();
	m_graphicsReleasedSemaphores.clear();
	m_computeFinishedSemaphores.clear();
	m_imageAvailableSemaphores.clear();

	// Clear debug sphere resources (always initialized)
	m_debugPipeline.reset();
	m_debugDescriptorSet2.reset();
	m_debugDescriptorSet1.reset();
	m_debugDescriptorSetLayout.reset();
	m_debugFragmentShader.reset();
	m_debugVertexShader.reset();
	m_debugUboBuffer2.reset();
	m_debugUboBuffer1.reset();
	m_sphereIndexBuffer.reset();
	m_sphereVertexBuffer.reset();

	m_graphicsPipeline.reset();
	m_graphicsDescriptorSet.reset();
	m_graphicsDescriptorSetLayout.reset();

	m_computePipeline.reset();
	m_computeDescriptorSetB.reset();
	m_computeDescriptorSetA.reset();
	m_computeDescriptorSetLayout.reset();

	m_fragmentShader.reset();
	m_vertexShader.reset();
	m_computeShader.reset();

	m_shaderFactory.reset();
	m_mvpBuffer.reset();
	m_paramsBuffer.reset();
	m_particleBufferB.reset();
	m_particleBufferA.reset();
}

void ParticlesApp::OnKey(int key, int action, int mods)
{
	// Forward to camera
	m_camera.OnKey(key, action, mods);

	// Handle application-specific keys
	if (action == GLFW_PRESS)
	{
		// ESC key closes the application
		if (key == GLFW_KEY_ESCAPE)
		{
			glfwSetWindowShouldClose(m_deviceManager->GetWindow(), GLFW_TRUE);
		}
		// D key toggles debug sphere rendering
		else if (key == GLFW_KEY_D)
		{
			m_enableDebugSpheres = !m_enableDebugSpheres;
		}
	}
}

void ParticlesApp::OnMouseButton(int button, int action, int mods)
{
	// Forward to camera
	m_camera.OnMouseButton(button, action, mods);
}

void ParticlesApp::OnMouseMove(double xpos, double ypos)
{
	// Forward to camera
	m_camera.OnMouseMove(xpos, ypos);
}
