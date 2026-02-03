#include "shaders/shaderio.h"
#include <msplat/core/log.h>
#include <msplat/engine/rendering/compute_splat_rasterizer.h>
#include <msplat/engine/rendering/shader_factory.h>

namespace msplat::engine
{

ComputeSplatRasterizer::ComputeSplatRasterizer(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs) :
    m_device(device), m_vfs(vfs)
{
}

void ComputeSplatRasterizer::Initialize(uint32_t screenWidth, uint32_t screenHeight, uint32_t maxSplatCount)
{
	if (m_isInitialized)
	{
		return;
	}

	m_maxSplatCount    = maxSplatCount;
	m_maxTileInstances = maxSplatCount * MaxTilesPerSplat;

	m_tileConfig.Update(screenWidth, screenHeight);

	LOG_INFO("ComputeSplatRasterizer: Initializing with {}x{} screen, {} tiles ({}x{}), max {} splats",
	         screenWidth, screenHeight, m_tileConfig.totalTiles,
	         m_tileConfig.tilesX, m_tileConfig.tilesY, maxSplatCount);

	CreateBuffers(maxSplatCount);
	CreateComputePipelines();
	CreateDescriptorSets();

	m_isInitialized = true;

	LOG_INFO("ComputeSplatRasterizer: Initialized successfully");
}

void ComputeSplatRasterizer::Resize(uint32_t screenWidth, uint32_t screenHeight)
{
	if (!m_isInitialized)
	{
		return;
	}

	uint32_t newTilesX = (screenWidth + m_tileConfig.tileSize - 1) / m_tileConfig.tileSize;
	uint32_t newTilesY = (screenHeight + m_tileConfig.tileSize - 1) / m_tileConfig.tileSize;

	if (newTilesX != m_tileConfig.tilesX || newTilesY != m_tileConfig.tilesY)
	{
		LOG_INFO("ComputeSplatRasterizer: Resizing from {}x{} to {}x{} tiles",
		         m_tileConfig.tilesX, m_tileConfig.tilesY, newTilesX, newTilesY);

		// Reallocate tile ranges buffer if tile count changed
		rhi::BufferDesc bufferDesc = {};
		bufferDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
		bufferDesc.resourceUsage   = rhi::ResourceUsage::Static;
		bufferDesc.size            = newTilesX * newTilesY * sizeof(int32_t) * 2;        // int2 per tile
		m_tileRanges               = m_device->CreateBuffer(bufferDesc);
	}

	m_tileConfig.Update(screenWidth, screenHeight);
}

void ComputeSplatRasterizer::CreateBuffers(uint32_t maxSplatCount)
{
	rhi::BufferDesc bufferDesc = {};
	bufferDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
	bufferDesc.resourceUsage   = rhi::ResourceUsage::Static;

	// Geometry buffer
	// struct Gaussian2D { float2 screenPos; float3 conic; float opacity; float4 color; float radius; float depth; }
	bufferDesc.size  = maxSplatCount * 48;        // 12 floats * 4 bytes = 48 bytes per Gaussian2D
	m_geometryBuffer = m_device->CreateBuffer(bufferDesc);

	// Tile keys buffer
	bufferDesc.size = m_maxTileInstances * sizeof(uint32_t);
	m_tileKeys      = m_device->CreateBuffer(bufferDesc);

	// Tile values buffer
	bufferDesc.size = m_maxTileInstances * sizeof(uint32_t);
	m_tileValues    = m_device->CreateBuffer(bufferDesc);

	// Tile ranges buffer
	bufferDesc.size = m_tileConfig.totalTiles * sizeof(int32_t) * 2;
	m_tileRanges    = m_device->CreateBuffer(bufferDesc);

	// Global counter
	bufferDesc.size = sizeof(uint32_t);
	m_globalCounter = m_device->CreateBuffer(bufferDesc);

	// Counter readback buffer
	rhi::BufferDesc readbackDesc           = {};
	readbackDesc.size                      = sizeof(uint32_t);
	readbackDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
	readbackDesc.resourceUsage             = rhi::ResourceUsage::Readback;
	readbackDesc.hints.persistently_mapped = true;
	m_counterReadback                      = m_device->CreateBuffer(readbackDesc);

	// Frame UBO buffer
	rhi::BufferDesc uboDesc           = {};
	uboDesc.size                      = sizeof(FrameUBO);
	uboDesc.usage                     = rhi::BufferUsage::UNIFORM | rhi::BufferUsage::TRANSFER_DST;
	uboDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	uboDesc.hints.persistently_mapped = true;
	m_frameUBO                        = m_device->CreateBuffer(uboDesc);

	LOG_INFO("ComputeSplatRasterizer: Buffers created - geometry: {} MB, keys: {} MB, values: {} MB",
	         (maxSplatCount * 48) / (1024.0 * 1024.0),
	         (m_maxTileInstances * 4) / (1024.0 * 1024.0),
	         (m_maxTileInstances * 4) / (1024.0 * 1024.0));
}

void ComputeSplatRasterizer::CreateComputePipelines()
{
	ShaderFactory shaderFactory(m_device, m_vfs);

	rhi::ShaderHandle preprocessShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/preprocess.comp.spv",
	    rhi::ShaderStage::COMPUTE);

	if (!preprocessShader)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to load preprocess.comp.spv");
		return;
	}

	// Create preprocess descriptor set layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: FrameUBO
		layoutDesc.bindings.push_back({0,
		                               rhi::DescriptorType::UNIFORM_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 1: positions
		layoutDesc.bindings.push_back({1,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 2: cov3DPacked
		layoutDesc.bindings.push_back({2,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 3: colors
		layoutDesc.bindings.push_back({3,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 4: shRest
		layoutDesc.bindings.push_back({4,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 5: meshIndices
		layoutDesc.bindings.push_back({5,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 6: modelMatrices
		layoutDesc.bindings.push_back({6,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 7: geometryBuffer
		layoutDesc.bindings.push_back({7,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 8: tileKeys
		layoutDesc.bindings.push_back({8,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 9: tileValues
		layoutDesc.bindings.push_back({9,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 10: globalCounter
		layoutDesc.bindings.push_back({10,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		m_preprocessLayout = m_device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create preprocess pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = preprocessShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_preprocessLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PreprocessPC);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		m_preprocessPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	LOG_INFO("ComputeSplatRasterizer: Compute pipelines created");
}

void ComputeSplatRasterizer::CreateDescriptorSets()
{
	m_preprocessDescriptorSet = m_device->CreateDescriptorSet(m_preprocessLayout.Get());

	// Binding 0: FrameUBO
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_frameUBO.Get();
		binding.type               = rhi::DescriptorType::UNIFORM_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(0, binding);
	}

	// Binding 7: geometryBuffer
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_geometryBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(7, binding);
	}

	// Binding 8: tileKeys
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileKeys.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(8, binding);
	}

	// Binding 9: tileValues
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileValues.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(9, binding);
	}

	// Binding 10: globalCounter
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_globalCounter.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(10, binding);
	}

	LOG_INFO("ComputeSplatRasterizer: Descriptor sets created");
}

void ComputeSplatRasterizer::RebindSceneDescriptors(const Scene &scene)
{
	const auto &gpuData = scene.GetGpuData();

	// Binding 1: positions
	if (gpuData.positions)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.positions.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(1, binding);
	}

	// Binding 2: covariances3D
	if (gpuData.covariances3D)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.covariances3D.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(2, binding);
	}

	// Binding 3: colors
	if (gpuData.colors)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.colors.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(3, binding);
	}

	// Binding 4: shRest
	if (gpuData.shRest)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.shRest.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(4, binding);
	}

	// Binding 5: meshIndices
	if (gpuData.meshIndices)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.meshIndices.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(5, binding);
	}

	// Binding 6: modelMatrices
	if (gpuData.modelMatrices)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = gpuData.modelMatrices.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(6, binding);
	}

	m_lastBoundScene = &scene;
}

void ComputeSplatRasterizer::RecordPreprocess(rhi::IRHICommandList *cmdList, const Scene &scene, const FrameUBO &frameUBO)
{
	if (!m_isInitialized)
	{
		LOG_ERROR("ComputeSplatRasterizer: Not initialized");
		return;
	}

	uint32_t splatCount = scene.GetTotalSplatCount();
	if (splatCount == 0)
	{
		return;
	}

	// Rebind scene descriptors if scene changed
	if (&scene != m_lastBoundScene)
	{
		RebindSceneDescriptors(scene);
	}

	// Update frame UBO
	void *uboPtr = m_frameUBO->Map();
	if (uboPtr)
	{
		memcpy(uboPtr, &frameUBO, sizeof(FrameUBO));
	}

	// Clear global counter to 0
	{
		uint32_t zero = 0;
		cmdList->FillBuffer(m_globalCounter.Get(), 0, sizeof(uint32_t), zero);

		rhi::BufferTransition transition = {};
		transition.buffer                = m_globalCounter.Get();
		transition.before                = rhi::ResourceState::CopyDestination;
		transition.after                 = rhi::ResourceState::ShaderReadWrite;
		cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute, {&transition, 1}, {}, {});
	}

	// Bind pipeline and descriptor set
	cmdList->SetPipeline(m_preprocessPipeline.Get());
	cmdList->BindDescriptorSet(0, m_preprocessDescriptorSet.Get());

	// Push constants
	PreprocessPC pc     = {};
	pc.numSplats        = splatCount;
	pc.tilesX           = m_tileConfig.tilesX;
	pc.tilesY           = m_tileConfig.tilesY;
	pc.tileSize         = m_tileConfig.tileSize;
	pc.nearPlane        = m_nearPlane;
	pc.farPlane         = m_farPlane;
	pc.maxTileInstances = m_maxTileInstances;
	pc._pad0            = 0;

	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
	                       {reinterpret_cast<const std::byte *>(&pc), sizeof(pc)});

	// Dispatch: one thread per splat
	uint32_t numWorkgroups = (splatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups, 1, 1);

	// Barrier to ensure compute completes
	rhi::BufferTransition transitions[4] = {};

	transitions[0].buffer = m_geometryBuffer.Get();
	transitions[0].before = rhi::ResourceState::ShaderReadWrite;
	transitions[0].after  = rhi::ResourceState::ShaderReadWrite;

	transitions[1].buffer = m_tileKeys.Get();
	transitions[1].before = rhi::ResourceState::ShaderReadWrite;
	transitions[1].after  = rhi::ResourceState::ShaderReadWrite;

	transitions[2].buffer = m_tileValues.Get();
	transitions[2].before = rhi::ResourceState::ShaderReadWrite;
	transitions[2].after  = rhi::ResourceState::ShaderReadWrite;

	transitions[3].buffer = m_globalCounter.Get();
	transitions[3].before = rhi::ResourceState::ShaderReadWrite;
	transitions[3].after  = rhi::ResourceState::CopySource;

	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {transitions, 4}, {}, {});

	// Copy counter to readback buffer
	rhi::BufferCopy copy = {};
	copy.srcOffset       = 0;
	copy.dstOffset       = 0;
	copy.size            = sizeof(uint32_t);
	cmdList->CopyBuffer(m_globalCounter.Get(), m_counterReadback.Get(), {&copy, 1});

	m_stats.activeSplats = splatCount;
}

uint32_t ComputeSplatRasterizer::ReadTileInstanceCount()
{
	if (!m_counterReadback)
	{
		return 0;
	}

	void *ptr = m_counterReadback->Map();
	if (!ptr)
	{
		return 0;
	}

	uint32_t count             = *static_cast<uint32_t *>(ptr);
	m_stats.totalTileInstances = count;

	if (m_stats.activeSplats > 0)
	{
		m_stats.avgTilesPerSplat = static_cast<float>(count) / static_cast<float>(m_stats.activeSplats);
	}

	return count;
}

ComputeSplatRasterizer::BufferInfo ComputeSplatRasterizer::GetBufferInfo() const
{
	BufferInfo info      = {};
	info.geometryBuffer  = m_geometryBuffer;
	info.tileKeys        = m_tileKeys;
	info.tileValues      = m_tileValues;
	info.tileRanges      = m_tileRanges;
	info.globalCounter   = m_globalCounter;
	info.counterReadback = m_counterReadback;
	return info;
}

}        // namespace msplat::engine
