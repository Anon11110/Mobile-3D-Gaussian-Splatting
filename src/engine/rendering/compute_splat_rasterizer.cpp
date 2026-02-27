#include "shaders/shaderio.h"
#include <algorithm>
#include <msplat/core/log.h>
#include <msplat/engine/rendering/compute_splat_rasterizer.h>
#include <msplat/engine/rendering/shader_factory.h>
#include <numeric>

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
	m_maxTileInstances = maxSplatCount * InitialTilesPerSplat;

	m_tileConfig.Update(screenWidth, screenHeight);

	LOG_INFO("ComputeSplatRasterizer: Initializing with {}x{} screen, {} tiles ({}x{}), max {} splats, max {} tile instances",
	         screenWidth, screenHeight, m_tileConfig.totalTiles,
	         m_tileConfig.tilesX, m_tileConfig.tilesY, maxSplatCount, m_maxTileInstances);

	CreateBuffers(maxSplatCount);
	CreateOutputImage();
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

		// Rebind ranges descriptor set with new buffer
		if (m_rangesDescriptorSet)
		{
			rhi::BufferBinding binding = {};
			binding.buffer             = m_tileRanges.Get();
			binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			m_rangesDescriptorSet->BindBuffer(1, binding);
		}
	}

	m_tileConfig.Update(screenWidth, screenHeight);

	CreateOutputImage();
	RebindRasterDescriptors();
}

void ComputeSplatRasterizer::CreateBuffers(uint32_t maxSplatCount)
{
	rhi::BufferDesc bufferDesc = {};
	bufferDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
#ifdef ENABLE_SORT_VERIFICATION
	bufferDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
#endif
	bufferDesc.resourceUsage = rhi::ResourceUsage::Static;

	// Geometry buffer: Gaussian2D per splat
	bufferDesc.size  = maxSplatCount * 48;
	m_geometryBuffer = m_device->CreateBuffer(bufferDesc);

	// Tile ranges buffer
	bufferDesc.size = m_tileConfig.totalTiles * sizeof(int32_t) * 2;
	m_tileRanges    = m_device->CreateBuffer(bufferDesc);

	// Global counter
	bufferDesc.size = sizeof(uint32_t);
	bufferDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
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

	// Sorted tile values output buffer
	rhi::BufferDesc sortedValuesDesc = {};
	sortedValuesDesc.size            = m_maxTileInstances * sizeof(uint32_t);
	sortedValuesDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
#ifdef ENABLE_SORT_VERIFICATION
	sortedValuesDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
#endif
	sortedValuesDesc.resourceUsage = rhi::ResourceUsage::Static;
	m_sortedTileValues             = m_device->CreateBuffer(sortedValuesDesc);

	// TileID and SplatID buffers
	{
		rhi::BufferDesc tileIDDesc = {};
		tileIDDesc.size            = m_maxTileInstances * sizeof(uint32_t);
		tileIDDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
		tileIDDesc.resourceUsage   = rhi::ResourceUsage::Static;
		m_tileTileIDs              = m_device->CreateBuffer(tileIDDesc);
		m_tileSplatIDs             = m_device->CreateBuffer(tileIDDesc);
	}

	// Indirect args buffer for GPU-driven sort dispatch
	{
		rhi::BufferDesc indirectArgsDesc = {};
		indirectArgsDesc.size            = 80;        // SortParams(32) + 4 DispatchIndirect(12 each = 48)
		indirectArgsDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::INDIRECT;
		indirectArgsDesc.resourceUsage   = rhi::ResourceUsage::Static;
		m_indirectArgsBuffer             = m_device->CreateBuffer(indirectArgsDesc);
	}

	// Create tile key sorter. Reuse current radix sort
	// The sorter internally creates: splatDepths (our tileKeys), splatIndicesOriginal (our tileValues),
	// sortPairsA/B (ping-pong uint2 key+index), histograms, blockSums
	// We pass m_sortedTileValues as the output buffer (sortIndicesB)
	m_tileSorter                     = container::make_unique<GpuSplatSorter>(m_device, m_vfs);
	rhi::IRHIBuffer *indirectArgsPtr = m_indirectArgsBuffer.Get();
	m_tileSorter->Initialize(m_maxTileInstances, m_sortedTileValues, 1, {&indirectArgsPtr, 1});

#ifdef ENABLE_SORT_VERIFICATION
	// Verification readback buffer for sorted pairs
	rhi::BufferDesc verifyDesc           = {};
	verifyDesc.size                      = m_maxTileInstances * sizeof(uint32_t) * 2;
	verifyDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
	verifyDesc.resourceUsage             = rhi::ResourceUsage::Readback;
	verifyDesc.hints.persistently_mapped = true;
	m_sortVerifyReadback                 = m_device->CreateBuffer(verifyDesc);
#endif

	// Transmittance stats buffer:
	{
		rhi::BufferDesc statsDesc  = {};
		statsDesc.size             = sizeof(uint32_t) * 3;        // totalEvals, actualEvals, earlyExitPixels
		statsDesc.usage            = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
		statsDesc.resourceUsage    = rhi::ResourceUsage::Static;
		m_transmittanceStatsBuffer = m_device->CreateBuffer(statsDesc);

		rhi::BufferDesc statsReadbackDesc           = {};
		statsReadbackDesc.size                      = sizeof(uint32_t) * 3;
		statsReadbackDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
		statsReadbackDesc.resourceUsage             = rhi::ResourceUsage::Readback;
		statsReadbackDesc.hints.persistently_mapped = true;
		m_transmittanceStatsReadback                = m_device->CreateBuffer(statsReadbackDesc);
	}

	LOG_INFO("ComputeSplatRasterizer: Buffers created - geometry: {:.1f} MB, tileKeys(sorter): {:.1f} MB, sortedValues: {:.1f} MB",
	         (maxSplatCount * 48) / (1024.0 * 1024.0),
	         (m_maxTileInstances * 4) / (1024.0 * 1024.0),
	         (m_maxTileInstances * 4) / (1024.0 * 1024.0));
}

void ComputeSplatRasterizer::CreateOutputImage()
{
	rhi::TextureDesc texDesc = {};
	texDesc.width            = m_tileConfig.screenWidth;
	texDesc.height           = m_tileConfig.screenHeight;
	texDesc.format           = rhi::TextureFormat::RGBA32_FLOAT;
	texDesc.isStorageImage   = true;
	texDesc.resourceUsage    = rhi::ResourceUsage::Static;

	m_outputImage = m_device->CreateTexture(texDesc);

	// Transition image from Undefined to CopySource immediately after creation
	{
		auto cmdList = m_device->CreateCommandList();
		cmdList->Begin();

		rhi::TextureTransition transition = {};
		transition.texture                = m_outputImage.Get();
		transition.before                 = rhi::ResourceState::Undefined;
		transition.after                  = rhi::ResourceState::CopySource;

		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
		                 {}, {&transition, 1}, {});

		cmdList->End();

		rhi::IRHICommandList *cmdListPtr = cmdList.Get();
		m_device->SubmitCommandLists({&cmdListPtr, 1});
		m_device->WaitIdle();
	}

	LOG_INFO("ComputeSplatRasterizer: Output image created ({}x{}, RGBA32F)",
	         texDesc.width, texDesc.height);
}

void ComputeSplatRasterizer::CreateComputePipelines()
{
	ShaderFactory shaderFactory(m_device, m_vfs);

	// --- Write sort indirect args pipeline ---
	{
		rhi::ShaderHandle writeArgsShader = shaderFactory.getOrCreateShader(
		    "shaders/compiled/write_sort_indirect_args_cs",
		    rhi::ShaderStage::COMPUTE);

		if (!writeArgsShader)
		{
			LOG_ERROR("ComputeSplatRasterizer: Failed to load write_sort_indirect_args.comp.spv");
			return;
		}

		rhi::DescriptorSetLayoutDesc layoutDesc = {};
		// Binding 0: globalCounter
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: indirectArgs
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		m_writeArgsLayout = m_device->CreateDescriptorSetLayout(layoutDesc);

		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = writeArgsShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_writeArgsLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(WriteArgsPC);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		m_writeArgsPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	// --- Preprocess pipeline ---
	m_preprocessShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/preprocess_cs",
	    rhi::ShaderStage::COMPUTE);

	if (!m_preprocessShader)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to load preprocess.comp.spv");
		return;
	}

	// Create preprocess descriptor set layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: FrameUBO
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::UNIFORM_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: positions
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: cov3DPacked
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 3: colors
		layoutDesc.bindings.push_back({3, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 4: shRest
		layoutDesc.bindings.push_back({4, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 5: meshIndices
		layoutDesc.bindings.push_back({5, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 6: modelMatrices
		layoutDesc.bindings.push_back({6, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 7: geometryBuffer
		layoutDesc.bindings.push_back({7, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 8: tileKeys
		layoutDesc.bindings.push_back({8, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 9: tileValues
		layoutDesc.bindings.push_back({9, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 10: globalCounter
		layoutDesc.bindings.push_back({10, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 11: tileTileIDs
		layoutDesc.bindings.push_back({11, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 12: tileSplatIDs
		layoutDesc.bindings.push_back({12, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		m_preprocessLayout = m_device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create preprocess pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = m_preprocessShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_preprocessLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PreprocessPC);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};
		pipelineDesc.specialization              = rhi::MakeSpecConstantU32(0, m_currentShDegree);

		m_preprocessPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	// --- Identify Ranges pipeline ---
	rhi::ShaderHandle rangesShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/identify_ranges_cs",
	    rhi::ShaderStage::COMPUTE);

	if (!rangesShader)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to load identify_ranges.comp.spv");
		return;
	}

	// Create ranges descriptor set layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: sortedTileKeys
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: tileRanges
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: sortParams (actual tile instance count)
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		m_rangesLayout = m_device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create ranges pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = rangesShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_rangesLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(RangesPC);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		m_rangesPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	// --- Rasterize pipeline ---
	rhi::ShaderHandle rasterShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/splat_raster_compute_cs",
	    rhi::ShaderStage::COMPUTE);

	if (!rasterShader)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to load splat_raster_compute.comp.spv");
		return;
	}

	// Create rasterize descriptor set layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: geometryBuffer
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: sortedTileValues
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: tileRanges
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 3: outputImage
		layoutDesc.bindings.push_back({3, rhi::DescriptorType::STORAGE_TEXTURE, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 4: transmittanceStats
		layoutDesc.bindings.push_back({4, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 5: tileSplatIDs (maps tile instance -> original splat index)
		layoutDesc.bindings.push_back({5, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		m_rasterLayout = m_device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create rasterize pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = rasterShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_rasterLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(RasterPC);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		m_rasterPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	// --- Rekey pipeline ---
	{
		rhi::ShaderHandle rekeyShader = shaderFactory.getOrCreateShader(
		    "shaders/compiled/rekey_tile_pairs_cs",
		    rhi::ShaderStage::COMPUTE);

		if (!rekeyShader)
		{
			LOG_ERROR("ComputeSplatRasterizer: Failed to load rekey_tile_pairs.comp.spv");
			return;
		}

		rhi::DescriptorSetLayoutDesc layoutDesc = {};
		// Binding 0: sortedPairs
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: tileTileIDs
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: sortParams (element count)
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		m_rekeyLayout = m_device->CreateDescriptorSetLayout(layoutDesc);

		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = rekeyShader.Get();
		pipelineDesc.descriptorSetLayouts     = {m_rekeyLayout.Get()};

		m_rekeyPipeline = m_device->CreateComputePipeline(pipelineDesc);
	}

	LOG_INFO("ComputeSplatRasterizer: Compute pipelines created (preprocess + identify_ranges + rasterize + rekey)");
}

void ComputeSplatRasterizer::RecreatePreprocessPipeline(uint32_t shDegree)
{
	if (!m_preprocessShader || !m_preprocessLayout)
		return;

	m_currentShDegree = shDegree;

	rhi::ComputePipelineDesc pipelineDesc = {};
	pipelineDesc.computeShader            = m_preprocessShader.Get();
	pipelineDesc.descriptorSetLayouts     = {m_preprocessLayout.Get()};

	rhi::PushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
	pushConstantRange.offset                 = 0;
	pushConstantRange.size                   = sizeof(PreprocessPC);
	pipelineDesc.pushConstantRanges          = {pushConstantRange};
	pipelineDesc.specialization              = rhi::MakeSpecConstantU32(0, m_currentShDegree);

	m_preprocessPipeline = m_device->CreateComputePipeline(pipelineDesc);

	LOG_INFO("ComputeSplatRasterizer: Recreated preprocess pipeline with SH degree {}", m_currentShDegree);
}

void ComputeSplatRasterizer::CreateDescriptorSets()
{
	auto sorterBuffers = m_tileSorter->GetBufferInfo();

	// --- Write args descriptor set ---
	m_writeArgsDescriptorSet = m_device->CreateDescriptorSet(m_writeArgsLayout.Get(), rhi::QueueType::COMPUTE);
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_globalCounter.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_writeArgsDescriptorSet->BindBuffer(0, binding);
	}
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_writeArgsDescriptorSet->BindBuffer(1, binding);
	}

	// --- Preprocess descriptor set ---
	m_preprocessDescriptorSet = m_device->CreateDescriptorSet(m_preprocessLayout.Get(), rhi::QueueType::COMPUTE);

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

	// Binding 8: tileKeys -> sorter's splatDepths
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.splatDepths.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(8, binding);
	}

	// Binding 9: tileValues -> sorter's splatIndicesOriginal
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.splatIndicesOriginal.Get();
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

	// Binding 11: tileTileIDs
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileTileIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(11, binding);
	}

	// Binding 12: tileSplatIDs
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileSplatIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(12, binding);
	}

	// --- Identify Ranges descriptor set ---
	m_rangesDescriptorSet = m_device->CreateDescriptorSet(m_rangesLayout.Get(), rhi::QueueType::COMPUTE);

	// Binding 0: sorted tile pairs (sortPairsB after 4 radix passes)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.sortPairsB.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rangesDescriptorSet->BindBuffer(0, binding);
	}

	// Binding 1: tileRanges
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileRanges.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rangesDescriptorSet->BindBuffer(1, binding);
	}

	// Binding 2: sortParams (indirect args buffer)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rangesDescriptorSet->BindBuffer(2, binding);
	}

	// --- Rasterize descriptor set ---
	m_rasterDescriptorSet = m_device->CreateDescriptorSet(m_rasterLayout.Get(), rhi::QueueType::COMPUTE);

	// Binding 0: geometryBuffer
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_geometryBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(0, binding);
	}

	// Binding 1: sortedTileValues
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_sortedTileValues.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(1, binding);
	}

	// Binding 2: tileRanges
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileRanges.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(2, binding);
	}

	// Binding 4: transmittanceStats
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_transmittanceStatsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(4, binding);
	}

	// Binding 5: tileSplatIDs (maps tile instance -> original splat index)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileSplatIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(5, binding);
	}

	// Binding 3: outputImage
	RebindRasterDescriptors();

	// --- Rekey descriptor set ---
	m_rekeyDescriptorSet = m_device->CreateDescriptorSet(m_rekeyLayout.Get(), rhi::QueueType::COMPUTE);

	// Binding 0: sortedPairs (sortPairsB)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.sortPairsB.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(0, binding);
	}

	// Binding 1: tileTileIDs
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileTileIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(1, binding);
	}

	// Binding 2: sortParams (indirect args buffer for element count)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(2, binding);
	}

	LOG_INFO("ComputeSplatRasterizer: Descriptor sets created (preprocess + ranges + rasterize)");
}

void ComputeSplatRasterizer::RebindRasterDescriptors()
{
	if (!m_rasterDescriptorSet || !m_outputImage)
	{
		return;
	}

	rhi::TextureBinding texBinding = {};
	texBinding.texture             = m_outputImage.Get();
	texBinding.type                = rhi::DescriptorType::STORAGE_TEXTURE;
	texBinding.layout              = rhi::ImageLayout::GENERAL;
	m_rasterDescriptorSet->BindTexture(3, texBinding);
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

	// Recreate preprocess pipeline if SH degree changed
	uint32_t sceneShDegree = scene.GetMaxShDegree();
	if (sceneShDegree != m_currentShDegree)
	{
		RecreatePreprocessPipeline(sceneShDegree);
	}

	// Update frame UBO
	void *uboPtr = m_frameUBO->Map();
	if (uboPtr)
	{
		memcpy(uboPtr, &frameUBO, sizeof(FrameUBO));
	}

	// Get sorter's key buffer (where preprocess writes tile keys)
	auto sorterBuffers = m_tileSorter->GetBufferInfo();

	// Fill tile keys buffer with 0xFFFFFFFF so unused entries sort to end
	cmdList->FillBuffer(sorterBuffers.splatDepths.Get(), 0,
	                    m_maxTileInstances * sizeof(uint32_t), 0xFFFFFFFF);

	// Clear global counter to 0
	cmdList->FillBuffer(m_globalCounter.Get(), 0, sizeof(uint32_t), 0);

	// Barriers: fill -> compute
	{
		rhi::BufferTransition transitions[2] = {};
		transitions[0].buffer                = sorterBuffers.splatDepths.Get();
		transitions[0].before                = rhi::ResourceState::CopyDestination;
		transitions[0].after                 = rhi::ResourceState::ShaderReadWrite;
		transitions[1].buffer                = m_globalCounter.Get();
		transitions[1].before                = rhi::ResourceState::CopyDestination;
		transitions[1].after                 = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute, {transitions, 2}, {}, {});
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

	// Post-preprocess barriers: geometry readable, counter readable for writeArgs,
	// tileTileIDs/tileSplatIDs readable for rekey and rasterizer
	{
		rhi::BufferTransition transitions[5] = {};

		transitions[0].buffer = m_geometryBuffer.Get();
		transitions[0].before = rhi::ResourceState::ShaderWrite;
		transitions[0].after  = rhi::ResourceState::GeneralRead;

		transitions[1].buffer = sorterBuffers.splatDepths.Get();
		transitions[1].before = rhi::ResourceState::ShaderWrite;
		transitions[1].after  = rhi::ResourceState::ShaderReadWrite;

		transitions[2].buffer = m_globalCounter.Get();
		transitions[2].before = rhi::ResourceState::ShaderReadWrite;
		transitions[2].after  = rhi::ResourceState::GeneralRead;

		transitions[3].buffer = m_tileTileIDs.Get();
		transitions[3].before = rhi::ResourceState::ShaderWrite;
		transitions[3].after  = rhi::ResourceState::GeneralRead;

		transitions[4].buffer = m_tileSplatIDs.Get();
		transitions[4].before = rhi::ResourceState::ShaderWrite;
		transitions[4].after  = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {transitions, 5}, {}, {});
	}

	// Write sort indirect args from GPU-side tile instance count
	{
		cmdList->SetPipeline(m_writeArgsPipeline.Get());
		cmdList->BindDescriptorSet(0, m_writeArgsDescriptorSet.Get());

		WriteArgsPC writeArgsPC      = {};
		writeArgsPC.maxTileInstances = m_maxTileInstances;
		cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
		                       {reinterpret_cast<const std::byte *>(&writeArgsPC), sizeof(writeArgsPC)});

		cmdList->Dispatch(1, 1, 1);

		// Barrier: indirect args written, needs to be usable as IndirectArgument + readable by shaders
		rhi::BufferTransition indirectTransition = {};
		indirectTransition.buffer                = m_indirectArgsBuffer.Get();
		indirectTransition.before                = rhi::ResourceState::ShaderWrite;
		indirectTransition.after                 = rhi::ResourceState::IndirectArgument;

		// Transition globalCounter for copy to readback
		rhi::BufferTransition counterTransition = {};
		counterTransition.buffer                = m_globalCounter.Get();
		counterTransition.before                = rhi::ResourceState::GeneralRead;
		counterTransition.after                 = rhi::ResourceState::CopySource;

		rhi::BufferTransition postWriteArgsTransitions[2] = {indirectTransition, counterTransition};
		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
		                 {postWriteArgsTransitions, 2}, {}, {});
	}

	// Copy counter to readback buffer
	rhi::BufferCopy copy = {};
	copy.srcOffset       = 0;
	copy.dstOffset       = 0;
	copy.size            = sizeof(uint32_t);
	cmdList->CopyBuffer(m_globalCounter.Get(), m_counterReadback.Get(), {&copy, 1});

	m_stats.activeSplats = splatCount;
}

void ComputeSplatRasterizer::RecordSort(rhi::IRHICommandList *cmdList)
{
	if (!m_tileSorter)
	{
		return;
	}

	// Two-pass LSD radix sort for 32-bit depth + 16-bit tileID:
	// 1. Pack keys (depth32) + values (self-index) into pairs
	// 2. Sort by depth32 (4 radix passes)
	// 3. Re-key: replace depth key with tileID in sorted pairs
	// 4. Sort by tileID (2 radix passes)
	// 5. Unpack sorted indices to output buffer

	m_tileSorter->PackPairsIndirect(cmdList, 0);
	m_tileSorter->SortPairsIndirect(cmdList, 0, 4);

	RecordRekey(cmdList);

	m_tileSorter->SortPairsIndirect(cmdList, 0, 2);
	m_tileSorter->UnpackPairsIndirect(cmdList, 0);
}

void ComputeSplatRasterizer::RecordRekey(rhi::IRHICommandList *cmdList)
{
	if (!m_rekeyPipeline)
	{
		return;
	}

	// After depth sort, sortPairsB contains (depthKey, valueIndex).
	// Replace pair.x with tileTileIDs[pair.y] so the second sort groups by tileID.

	cmdList->SetPipeline(m_rekeyPipeline.Get());
	cmdList->BindDescriptorSet(0, m_rekeyDescriptorSet.Get());
	cmdList->DispatchIndirect(m_indirectArgsBuffer.Get(), 68);        // pack/unpack dispatch offset

	// Barrier: sortPairsB written by rekey, needs to be readable for next sort pass
	auto                  sorterBuffers = m_tileSorter->GetBufferInfo();
	rhi::BufferTransition transition    = {};
	transition.buffer                   = sorterBuffers.sortPairsB.Get();
	transition.before                   = rhi::ResourceState::ShaderWrite;
	transition.after                    = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {&transition, 1}, {}, {});
}

void ComputeSplatRasterizer::RecordIdentifyRanges(rhi::IRHICommandList *cmdList)
{
	if (!m_rangesPipeline)
	{
		return;
	}

	auto sorterBuffers = m_tileSorter->GetBufferInfo();

	// Clear tile ranges to -1 (indicating empty tiles)
	cmdList->FillBuffer(m_tileRanges.Get(), 0,
	                    m_tileConfig.totalTiles * sizeof(int32_t) * 2, 0xFFFFFFFF);

	// Wait for tileRanges fill
	rhi::BufferTransition tileRangesTransition = {};
	tileRangesTransition.buffer                = m_tileRanges.Get();
	tileRangesTransition.before                = rhi::ResourceState::CopyDestination;
	tileRangesTransition.after                 = rhi::ResourceState::ShaderReadWrite;
	cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute, {&tileRangesTransition, 1}, {}, {});

	// Wait for sort output to be readable
	rhi::BufferTransition sortOutputTransition = {};
	sortOutputTransition.buffer                = sorterBuffers.sortPairsB.Get();
	sortOutputTransition.before                = rhi::ResourceState::ShaderReadWrite;
	sortOutputTransition.after                 = rhi::ResourceState::GeneralRead;
	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {&sortOutputTransition, 1}, {}, {});

	// Bind pipeline and descriptor set
	cmdList->SetPipeline(m_rangesPipeline.Get());
	cmdList->BindDescriptorSet(0, m_rangesDescriptorSet.Get());

	// Push constants: numTileInstances is now read from GPU buffer,
	// only numTiles (tile grid size) is needed from CPU
	RangesPC pc         = {};
	pc.numTileInstances = 0;
	pc.numTiles         = m_tileConfig.totalTiles;

	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
	                       {reinterpret_cast<const std::byte *>(&pc), sizeof(pc)});

	// DispatchIndirect: offset 68 = pack/unpack/identify_ranges dispatch
	// This dispatches ceil(actualTileInstances / 256) workgroups
	cmdList->DispatchIndirect(m_indirectArgsBuffer.Get(), 68);

	// Post-ranges barrier
	rhi::BufferTransition rangesTransition = {};
	rangesTransition.buffer                = m_tileRanges.Get();
	rangesTransition.before                = rhi::ResourceState::ShaderWrite;
	rangesTransition.after                 = rhi::ResourceState::GeneralRead;
	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {&rangesTransition, 1}, {}, {});
}

void ComputeSplatRasterizer::RecordRasterize(rhi::IRHICommandList *cmdList)
{
	if (!m_rasterPipeline || !m_outputImage)
	{
		return;
	}

	bool statsEnabled = (m_transmittanceStatsMode > 0);

	// Clear transmittance stats buffer when enabled
	if (statsEnabled && m_transmittanceStatsBuffer)
	{
		cmdList->FillBuffer(m_transmittanceStatsBuffer.Get(), 0, sizeof(uint32_t) * 3, 0);

		rhi::BufferTransition statsTransition = {};
		statsTransition.buffer                = m_transmittanceStatsBuffer.Get();
		statsTransition.before                = rhi::ResourceState::CopyDestination;
		statsTransition.after                 = rhi::ResourceState::ShaderReadWrite;
		cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Compute,
		                 {&statsTransition, 1}, {}, {});
	}

	// Pre-rasterize barriers: transition sorted values and output image
	{
		rhi::BufferTransition bufTransition = {};
		bufTransition.buffer                = m_sortedTileValues.Get();
		bufTransition.before                = rhi::ResourceState::ShaderReadWrite;
		bufTransition.after                 = rhi::ResourceState::GeneralRead;

		rhi::TextureTransition texTransition = {};
		texTransition.texture                = m_outputImage.Get();
		texTransition.before                 = rhi::ResourceState::CopySource;
		texTransition.after                  = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute,
		                 {&bufTransition, 1}, {&texTransition, 1}, {});
	}

	cmdList->SetPipeline(m_rasterPipeline.Get());
	cmdList->BindDescriptorSet(0, m_rasterDescriptorSet.Get());

	RasterPC pc               = {};
	pc.tilesX                 = m_tileConfig.tilesX;
	pc.tilesY                 = m_tileConfig.tilesY;
	pc.screenWidth            = m_tileConfig.screenWidth;
	pc.screenHeight           = m_tileConfig.screenHeight;
	pc.transmittanceStatsMode = m_transmittanceStatsMode;
	pc._rasterPad0            = 0;

	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0,
	                       {reinterpret_cast<const std::byte *>(&pc), sizeof(pc)});

	cmdList->Dispatch(m_tileConfig.tilesX, m_tileConfig.tilesY, 1);

	// Post-rasterize: copy stats to readback if enabled
	if (statsEnabled && m_transmittanceStatsBuffer)
	{
		rhi::BufferTransition statsTransition = {};
		statsTransition.buffer                = m_transmittanceStatsBuffer.Get();
		statsTransition.before                = rhi::ResourceState::ShaderReadWrite;
		statsTransition.after                 = rhi::ResourceState::CopySource;
		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Copy,
		                 {&statsTransition, 1}, {}, {});

		rhi::BufferCopy copy = {};
		copy.size            = sizeof(uint32_t) * 3;
		cmdList->CopyBuffer(m_transmittanceStatsBuffer.Get(), m_transmittanceStatsReadback.Get(), {&copy, 1});
	}

	// Transition output image for transfer/read
	rhi::TextureTransition outputTransition = {};
	outputTransition.texture                = m_outputImage.Get();
	outputTransition.before                 = rhi::ResourceState::ShaderReadWrite;
	outputTransition.after                  = rhi::ResourceState::CopySource;
	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Copy, {}, {&outputTransition, 1}, {});
}

void ComputeSplatRasterizer::ResizeTileBuffers(uint32_t newMaxTileInstances)
{
	LOG_INFO("ComputeSplatRasterizer: Resizing tile buffers from {} to {} instances ({:.1f} MB -> {:.1f} MB sort memory)",
	         m_maxTileInstances, newMaxTileInstances,
	         (m_maxTileInstances * 24.0) / (1024.0 * 1024.0),
	         (newMaxTileInstances * 24.0) / (1024.0 * 1024.0));

	m_maxTileInstances = newMaxTileInstances;

	// Recreate sorted tile values buffer
	rhi::BufferDesc sortedValuesDesc = {};
	sortedValuesDesc.size            = m_maxTileInstances * sizeof(uint32_t);
	sortedValuesDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
#ifdef ENABLE_SORT_VERIFICATION
	sortedValuesDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
#endif
	sortedValuesDesc.resourceUsage = rhi::ResourceUsage::Static;
	m_sortedTileValues             = m_device->CreateBuffer(sortedValuesDesc);

	// Recreate tileID and splatID buffers
	{
		rhi::BufferDesc tileIDDesc = {};
		tileIDDesc.size            = m_maxTileInstances * sizeof(uint32_t);
		tileIDDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
		tileIDDesc.resourceUsage   = rhi::ResourceUsage::Static;
		m_tileTileIDs              = m_device->CreateBuffer(tileIDDesc);
		m_tileSplatIDs             = m_device->CreateBuffer(tileIDDesc);
	}

	// Recreate indirect args buffer
	{
		rhi::BufferDesc indirectArgsDesc = {};
		indirectArgsDesc.size            = 80;
		indirectArgsDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::INDIRECT;
		indirectArgsDesc.resourceUsage   = rhi::ResourceUsage::Static;
		m_indirectArgsBuffer             = m_device->CreateBuffer(indirectArgsDesc);
	}

	// Recreate tile key sorter with new capacity
	m_tileSorter                     = container::make_unique<GpuSplatSorter>(m_device, m_vfs);
	rhi::IRHIBuffer *indirectArgsPtr = m_indirectArgsBuffer.Get();
	m_tileSorter->Initialize(m_maxTileInstances, m_sortedTileValues, 1, {&indirectArgsPtr, 1});

#ifdef ENABLE_SORT_VERIFICATION
	// Recreate verification readback buffer
	rhi::BufferDesc verifyDesc           = {};
	verifyDesc.size                      = m_maxTileInstances * sizeof(uint32_t);
	verifyDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
	verifyDesc.resourceUsage             = rhi::ResourceUsage::Readback;
	verifyDesc.hints.persistently_mapped = true;
	m_sortVerifyReadback                 = m_device->CreateBuffer(verifyDesc);
#endif

	// Invalidate debug buffers
	m_dbgKeysReadback = nullptr;
	m_dbgValsReadback = nullptr;
	m_dbgKeysStaging  = nullptr;
	m_dbgValsStaging  = nullptr;

	// Rebind descriptor sets with new sorter buffers
	auto sorterBuffers = m_tileSorter->GetBufferInfo();

	// Preprocess: binding 8 (tileKeys -> sorter's splatDepths)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.splatDepths.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(8, binding);
	}

	// Preprocess: binding 9 (tileValues -> sorter's splatIndicesOriginal)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.splatIndicesOriginal.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(9, binding);
	}

	// Ranges: binding 0 (sortedTilePairs -> sorter's sortPairsB)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.sortPairsB.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rangesDescriptorSet->BindBuffer(0, binding);
	}

	// Ranges: binding 2 (sortParams -> indirect args buffer)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rangesDescriptorSet->BindBuffer(2, binding);
	}

	// WriteArgs: rebind with new buffers
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_globalCounter.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_writeArgsDescriptorSet->BindBuffer(0, binding);
	}
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_writeArgsDescriptorSet->BindBuffer(1, binding);
	}

	// Rasterize: binding 1 (sortedTileValues)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_sortedTileValues.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(1, binding);
	}

	// Rasterize: binding 5 (tileSplatIDs)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileSplatIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rasterDescriptorSet->BindBuffer(5, binding);
	}

	// Preprocess: bindings 11-12 (tileTileIDs, tileSplatIDs)
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileTileIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(11, binding);
	}
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileSplatIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_preprocessDescriptorSet->BindBuffer(12, binding);
	}

	// Rekey: rebind with new buffers
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = sorterBuffers.sortPairsB.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(0, binding);
	}
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_tileTileIDs.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(1, binding);
	}
	{
		rhi::BufferBinding binding = {};
		binding.buffer             = m_indirectArgsBuffer.Get();
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		m_rekeyDescriptorSet->BindBuffer(2, binding);
	}

	LOG_INFO("ComputeSplatRasterizer: Tile buffers resized successfully (new max: {} instances)", m_maxTileInstances);
}

void ComputeSplatRasterizer::Render(
    rhi::IRHICommandList *cmdList, const Scene &scene, const FrameUBO &frameUBO)
{
	// Check previous frame's tile count and resize buffers if overflow occurred
	if (m_hasRenderedOneFrame)
	{
		uint32_t prevCount = ReadTileInstanceCount();
		if (prevCount > m_maxTileInstances)
		{
			uint32_t newMax = prevCount * 2;        // 2x headroom to avoid frequent resizes
			LOG_WARNING("ComputeSplatRasterizer: Tile buffer overflow detected ({} > {}), resizing to {}",
			            prevCount, m_maxTileInstances, newMax);

			cmdList->End();
			rhi::IRHICommandList *ptr = cmdList;
			m_device->SubmitCommandLists({&ptr, 1});
			m_device->WaitIdle();

			ResizeTileBuffers(newMax);

			cmdList->Begin();
		}
	}

	// Preprocess stage
	if (m_onPreprocessTimestamp)
		m_onPreprocessTimestamp(cmdList, true);
	RecordPreprocess(cmdList, scene, frameUBO);
	if (m_onPreprocessTimestamp)
		m_onPreprocessTimestamp(cmdList, false);

	// Sort stage
	if (m_cpuSortDebugEnabled)
	{
		// Submit preprocess commands, wait for completion, CPU sort, restart
		cmdList->End();
		rhi::IRHICommandList *ptr = cmdList;
		m_device->SubmitCommandLists({&ptr, 1});
		m_device->WaitIdle();

		PerformCPUSort();

		cmdList->Begin();
	}
	else
	{
		if (m_onSortTimestamp)
			m_onSortTimestamp(cmdList, true);
		RecordSort(cmdList);
		if (m_onSortTimestamp)
			m_onSortTimestamp(cmdList, false);
	}

	// Identify ranges stage
	if (m_onRangesTimestamp)
		m_onRangesTimestamp(cmdList, true);
	RecordIdentifyRanges(cmdList);
	if (m_onRangesTimestamp)
		m_onRangesTimestamp(cmdList, false);

	// Rasterize stage
	if (m_onRasterTimestamp)
		m_onRasterTimestamp(cmdList, true);
	RecordRasterize(cmdList);
	if (m_onRasterTimestamp)
		m_onRasterTimestamp(cmdList, false);

	m_hasRenderedOneFrame = true;
}

void ComputeSplatRasterizer::PerformCPUSort()
{
	LOG_WARNING("ComputeSplatRasterizer: CPU sort debug not supported with 32 bit depth sort");
	return;

	auto     sorterBuffers = m_tileSorter->GetBufferInfo();
	uint32_t bufSize       = m_maxTileInstances * sizeof(uint32_t);
	uint32_t pairBufSize   = m_maxTileInstances * sizeof(uint32_t) * 2;

	if (!m_dbgKeysReadback)
	{
		rhi::BufferDesc rbDesc           = {};
		rbDesc.size                      = bufSize;
		rbDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
		rbDesc.resourceUsage             = rhi::ResourceUsage::Readback;
		rbDesc.hints.persistently_mapped = true;
		m_dbgKeysReadback                = m_device->CreateBuffer(rbDesc);
		m_dbgValsReadback                = m_device->CreateBuffer(rbDesc);

		rhi::BufferDesc stageDesc           = {};
		stageDesc.size                      = pairBufSize;
		stageDesc.usage                     = rhi::BufferUsage::TRANSFER_SRC;
		stageDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
		stageDesc.hints.persistently_mapped = true;
		m_dbgKeysStaging                    = m_device->CreateBuffer(stageDesc);

		stageDesc.size   = bufSize;
		m_dbgValsStaging = m_device->CreateBuffer(stageDesc);

		LOG_INFO("ComputeSplatRasterizer: CPU sort debug buffers allocated ({:.1f} MB)",
		         (bufSize * 4) / (1024.0 * 1024.0));
	}

	m_stats.totalTileInstances = ReadTileInstanceCount();
	uint32_t count             = std::min(m_stats.totalTileInstances, m_maxTileInstances);

	LOG_INFO("ComputeSplatRasterizer: CPU sort - {} tile instances", count);

	// Copy keys (splatDepths) and values (splatIndicesOriginal) from GPU to readback buffers
	{
		auto  cmdHandle = m_device->CreateCommandList();
		auto *cmd       = cmdHandle.Get();
		cmd->Begin();

		rhi::BufferCopy copy = {};
		copy.size            = bufSize;
		cmd->CopyBuffer(sorterBuffers.splatDepths.Get(), m_dbgKeysReadback.Get(), {&copy, 1});
		cmd->CopyBuffer(sorterBuffers.splatIndicesOriginal.Get(), m_dbgValsReadback.Get(), {&copy, 1});

		cmd->End();
		rhi::IRHICommandList *ptr = cmd;
		m_device->SubmitCommandLists({&ptr, 1});
		m_device->WaitIdle();
	}

	// Map readback buffers
	auto *keys = static_cast<uint32_t *>(m_dbgKeysReadback->Map());
	auto *vals = static_cast<uint32_t *>(m_dbgValsReadback->Map());

	if (!keys || !vals)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to map debug readback buffers");
		if (keys)
			m_dbgKeysReadback->Unmap();
		if (vals)
			m_dbgValsReadback->Unmap();
		return;
	}

	// Build sort indices and sort by key
	std::vector<uint32_t> indices(m_maxTileInstances);
	std::iota(indices.begin(), indices.end(), 0u);
	std::sort(indices.begin(), indices.end(), [keys](uint32_t a, uint32_t b) { return keys[a] < keys[b]; });

	// Write sorted results to staging buffers
	auto *sortedPairs = static_cast<uint32_t *>(m_dbgKeysStaging->Map());
	auto *sortedVals  = static_cast<uint32_t *>(m_dbgValsStaging->Map());

	if (!sortedPairs || !sortedVals)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to map debug staging buffers");
		m_dbgKeysReadback->Unmap();
		m_dbgValsReadback->Unmap();
		if (sortedPairs)
			m_dbgKeysStaging->Unmap();
		if (sortedVals)
			m_dbgValsStaging->Unmap();
		return;
	}

	for (uint32_t i = 0; i < m_maxTileInstances; i++)
	{
		sortedPairs[i * 2]     = keys[indices[i]];
		sortedPairs[i * 2 + 1] = vals[indices[i]];
		sortedVals[i]          = vals[indices[i]];
	}

	m_dbgKeysReadback->Unmap();
	m_dbgValsReadback->Unmap();

	// Upload sorted results to GPU: sortPairsB (for identify_ranges) and m_sortedTileValues (for rasterize)
	{
		auto  cmdHandle = m_device->CreateCommandList();
		auto *cmd       = cmdHandle.Get();
		cmd->Begin();

		rhi::BufferCopy copy = {};
		copy.size            = pairBufSize;
		cmd->CopyBuffer(m_dbgKeysStaging.Get(), sorterBuffers.sortPairsB.Get(), {&copy, 1});

		rhi::BufferCopy valsCopy = {};
		valsCopy.size            = bufSize;
		cmd->CopyBuffer(m_dbgValsStaging.Get(), m_sortedTileValues.Get(), {&valsCopy, 1});

		cmd->End();
		rhi::IRHICommandList *ptr = cmd;
		m_device->SubmitCommandLists({&ptr, 1});
		m_device->WaitIdle();
	}
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

#ifdef ENABLE_SORT_VERIFICATION
bool ComputeSplatRasterizer::VerifySortOrder()
{
	if (!m_tileSorter || !m_sortVerifyReadback)
	{
		return false;
	}

	// Block GPU to ensure sort is complete
	m_device->WaitIdle();

	auto sorterBuffers = m_tileSorter->GetBufferInfo();

	// Copy sorted keys to readback buffer using a temporary command list
	auto  cmdListHandle = m_device->CreateCommandList();
	auto *cmdList       = cmdListHandle.Get();
	cmdList->Begin();

	rhi::BufferCopy copy = {};
	copy.srcOffset       = 0;
	copy.dstOffset       = 0;
	copy.size            = m_maxTileInstances * sizeof(uint32_t) * 2;
	cmdList->CopyBuffer(sorterBuffers.sortPairsB.Get(), m_sortVerifyReadback.Get(), {&copy, 1});

	cmdList->End();

	rhi::IRHICommandList *cmdListPtr = cmdList;
	m_device->SubmitCommandLists({&cmdListPtr, 1});
	m_device->WaitIdle();

	// Read back and verify
	void *ptr = m_sortVerifyReadback->Map();
	if (!ptr)
	{
		LOG_ERROR("ComputeSplatRasterizer: Failed to map verification buffer");
		return false;
	}

	const uint32_t *pairsRaw          = static_cast<const uint32_t *>(ptr);
	uint32_t        tileInstanceCount = m_stats.totalTileInstances;

	if (tileInstanceCount == 0)
	{
		LOG_INFO("ComputeSplatRasterizer: VerifySortOrder - no tile instances to verify");
		return true;
	}

	// Clamp to buffer size. The atomic counter can exceed maxTileInstances because
	// wave-level allocation happens before per-thread bounds checking in the preprocess shader
	/// @todo: capacity tuning later
	if (tileInstanceCount > m_maxTileInstances)
	{
		LOG_WARNING("ComputeSplatRasterizer: Tile instance count ({}) exceeds max ({}), clamping for verification",
		            tileInstanceCount, m_maxTileInstances);
		tileInstanceCount = m_maxTileInstances;
	}

	// Verify keys are in ascending order (only check real entries, not 0xFFFFFFFF padding)
	uint32_t outOfOrder  = 0;
	uint32_t firstBadIdx = 0;
	for (uint32_t i = 1; i < tileInstanceCount && i < m_maxTileInstances; ++i)
	{
		if (pairsRaw[i * 2] < pairsRaw[(i - 1) * 2])
		{
			if (outOfOrder == 0)
			{
				firstBadIdx = i;
			}
			outOfOrder++;
		}
	}

	if (outOfOrder > 0)
	{
		LOG_ERROR("ComputeSplatRasterizer: Sort verification FAILED - {} out-of-order pairs (first at index {}): "
		          "key[{}]=0x{:08X} > key[{}]=0x{:08X}",
		          outOfOrder, firstBadIdx,
		          firstBadIdx - 1, pairsRaw[(firstBadIdx - 1) * 2],
		          firstBadIdx, pairsRaw[firstBadIdx * 2]);
		return false;
	}

	// Log some stats about the sorted data
	uint32_t firstKey = pairsRaw[0];
	uint32_t lastKey  = pairsRaw[(tileInstanceCount - 1) * 2];
	LOG_INFO("ComputeSplatRasterizer: Sort verification PASSED - {} tile instances, "
	         "keys: [0x{:08X} .. 0x{:08X}], tile range: [{} .. {}]",
	         tileInstanceCount, firstKey, lastKey,
	         firstKey >> 16, lastKey >> 16);

	return true;
}
#endif

ComputeSplatRasterizer::BufferInfo ComputeSplatRasterizer::GetBufferInfo() const
{
	BufferInfo info      = {};
	info.geometryBuffer  = m_geometryBuffer;
	info.tileRanges      = m_tileRanges;
	info.globalCounter   = m_globalCounter;
	info.counterReadback = m_counterReadback;

	if (m_tileSorter)
	{
		auto sorterBuffers    = m_tileSorter->GetBufferInfo();
		info.tileKeys         = sorterBuffers.splatDepths;
		info.tileValues       = sorterBuffers.splatIndicesOriginal;
		info.sortedTileKeys   = sorterBuffers.sortPairsB;
		info.sortedTileValues = m_sortedTileValues;
	}

	return info;
}

void ComputeSplatRasterizer::SetSortMethod(int method)
{
	if (m_tileSorter)
	{
		m_tileSorter->SetSortMethod(static_cast<GpuSplatSorter::SortMethod>(method));
	}
}

int ComputeSplatRasterizer::GetSortMethod() const
{
	if (m_tileSorter)
	{
		return static_cast<int>(m_tileSorter->GetSortMethod());
	}
	return 0;
}

void ComputeSplatRasterizer::SetShaderVariant(int variant)
{
	if (m_tileSorter)
	{
		m_tileSorter->SetShaderVariant(static_cast<GpuSplatSorter::ShaderVariant>(variant));
	}
}

int ComputeSplatRasterizer::GetShaderVariant() const
{
	if (m_tileSorter)
	{
		return static_cast<int>(m_tileSorter->GetShaderVariant());
	}
	return 0;
}

ComputeSplatRasterizer::TransmittanceStats ComputeSplatRasterizer::ReadTransmittanceStats()
{
	TransmittanceStats result = {};

	if (!m_transmittanceStatsReadback || m_transmittanceStatsMode == 0)
	{
		return result;
	}

	void *ptr = m_transmittanceStatsReadback->Map();
	if (!ptr)
	{
		return result;
	}

	const auto *data         = static_cast<const uint32_t *>(ptr);
	result.totalEvaluations  = data[0];
	result.actualEvaluations = data[1];
	result.earlyExitPixels   = data[2];

	if (result.totalEvaluations > 0)
	{
		uint64_t saved        = result.totalEvaluations - result.actualEvaluations;
		result.savingsPercent = static_cast<float>(saved) * 100.0f / static_cast<float>(result.totalEvaluations);
	}

	m_stats.transmittance = result;
	return result;
}

}        // namespace msplat::engine
