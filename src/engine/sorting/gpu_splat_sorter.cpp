#include <algorithm>
#include <cstring>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <msplat/engine/rendering/shader_factory.h>
#include <msplat/engine/sorting/gpu_splat_sorter.h>

namespace msplat::engine
{

GpuSplatSorter::GpuSplatSorter(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs) :
    device(device), vfs(vfs), totalSplatCount(0), isInitialized(false)
{
}

void GpuSplatSorter::Initialize(uint32_t totalSplatCount, rhi::BufferHandle outputBuffer, uint32_t pipelineDepth,
                                container::span<rhi::IRHIBuffer *const> indirectArgsBuffers)
{
	// If already initialized, only process newly-provided indirect args
	if (isInitialized)
	{
		if (!indirectArgsBuffers.empty() && indirectArgsBufferPtrs.empty())
		{
			indirectArgsBufferPtrs.resize(indirectArgsBuffers.size());
			for (size_t i = 0; i < indirectArgsBuffers.size(); ++i)
			{
				indirectArgsBufferPtrs[i] = indirectArgsBuffers[i];
			}
			CreateIndirectComputePipelines();
			CreateIndirectDescriptorSets();
		}
		return;
	}

	this->totalSplatCount   = totalSplatCount;
	this->outputBufferCount = pipelineDepth;

	rhi::BufferDesc bufferDesc = {};
	bufferDesc.usage           = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST;
#ifdef ENABLE_SORT_VERIFICATION
	bufferDesc.usage |= rhi::BufferUsage::TRANSFER_SRC;
#endif
	bufferDesc.resourceUsage = rhi::ResourceUsage::Static;

	// Depth keys buffer
	bufferDesc.size = totalSplatCount * sizeof(uint32_t);
	splatDepths     = device->CreateBuffer(bufferDesc);

	// Original indices buffer
	bufferDesc.size      = totalSplatCount * sizeof(uint32_t);
	splatIndicesOriginal = device->CreateBuffer(bufferDesc);

	// Sort pair buffers (ping-pong)
	// With RadixPasses=4, the final pass writes to buffer B.
	// Use the caller's output buffer as primary output so results are written directly.
	bufferDesc.size = totalSplatCount * sizeof(uint32_t) * 2;
	sortPairsA      = device->CreateBuffer(bufferDesc);
	sortPairsB      = device->CreateBuffer(bufferDesc);

	// Output index buffers: [0]=primary (caller-provided), [1..K-1]=alternates for pipelined async compute
	bufferDesc.size = totalSplatCount * sizeof(uint32_t);
	outputBuffers.resize(outputBufferCount);
	outputBuffers[0] = std::move(outputBuffer);
	for (uint32_t i = 1; i < outputBufferCount; ++i)
	{
		outputBuffers[i] = device->CreateBuffer(bufferDesc);
	}

	// Histogram buffer for radix sort
	// Each workgroup generates a histogram with 256 bins for each of the 4 passes
	uint32_t numWorkgroups = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
	bufferDesc.size        = RadixPasses * numWorkgroups * RadixSortBins * sizeof(uint32_t);
	histograms             = device->CreateBuffer(bufferDesc);

	// Block sums buffer for hierarchical scan
	// We need space for the maximum number of workgroups we'll use for scanning
	// For the scan passes, we process WORKGROUP_SIZE * ElementPerThread elements per workgroup
	uint32_t elementsPerWorkgroup = WorkgroupSize * ElementPerThread;
	uint32_t numWorkgroupsForScan = (numWorkgroups * RadixSortBins + elementsPerWorkgroup - 1) / elementsPerWorkgroup;
	bufferDesc.size               = numWorkgroupsForScan * sizeof(uint32_t);
	blockSums                     = device->CreateBuffer(bufferDesc);

	// Camera UBO (host-visible for frequent updates)
	bufferDesc.usage                     = rhi::BufferUsage::UNIFORM | rhi::BufferUsage::TRANSFER_DST;
	bufferDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	bufferDesc.hints.persistently_mapped = true;
	bufferDesc.hints.prefer_device_local = false;
	bufferDesc.size                      = sizeof(math::mat4);        // For view matrix
	cameraUBO                            = device->CreateBuffer(bufferDesc);

	// Reset to device local for subsequent buffers
	bufferDesc.resourceUsage             = rhi::ResourceUsage::Static;
	bufferDesc.hints.persistently_mapped = false;
	bufferDesc.hints.prefer_device_local = true;

	CreateInitialIndicesBuffer(totalSplatCount);

	CreateComputePipelines();

	CreateDescriptorSets();

	// Initialize cache to match the buffer bound in CreateDescriptorSets
	lastBoundOutputBuffer = outputBuffers[0].Get();

	// Create timestamp query pool for GPU timing
	rhi::QueryPoolDesc timestampDesc{};
	timestampDesc.queryType  = rhi::QueryType::TIMESTAMP;
	timestampDesc.queryCount = 2 * timingFrameLatency;        // begin + end per frame
	timestampQueryPool       = device->CreateQueryPool(timestampDesc);

	if (timestampQueryPool)
	{
		timestampPeriod = device->GetTimestampPeriod();
		LOG_INFO("GpuSplatSorter: GPU timing enabled, period = {} ns", timestampPeriod);
	}

	// Initialize indirect dispatch pipelines and descriptor sets
	if (!indirectArgsBuffers.empty())
	{
		indirectArgsBufferPtrs.resize(indirectArgsBuffers.size());
		for (size_t i = 0; i < indirectArgsBuffers.size(); ++i)
		{
			indirectArgsBufferPtrs[i] = indirectArgsBuffers[i];
		}
		CreateIndirectComputePipelines();
		CreateIndirectDescriptorSets();
	}

	isInitialized = true;
}

void GpuSplatSorter::CreateInitialIndicesBuffer(uint32_t totalSplatCount)
{
	// Create staging buffer with initial indices (0, 1, 2, ...)
	container::vector<uint32_t> initialIndices;
	initialIndices.resize(totalSplatCount);
	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		initialIndices[i] = i;
	}

	// Create staging buffer
	rhi::BufferDesc stagingDesc           = {};
	stagingDesc.size                      = totalSplatCount * sizeof(uint32_t);
	stagingDesc.usage                     = rhi::BufferUsage::TRANSFER_SRC;
	stagingDesc.resourceUsage             = rhi::ResourceUsage::DynamicUpload;
	stagingDesc.hints.persistently_mapped = true;
	stagingDesc.initialData               = initialIndices.data();

	rhi::BufferHandle stagingBuffer = device->CreateBuffer(stagingDesc);

	// Copy from staging to device buffer
	rhi::CommandListHandle cmdList = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	cmdList->Begin();

	rhi::BufferCopy copyRegion = {};
	copyRegion.size            = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(stagingBuffer.Get(), splatIndicesOriginal.Get(), {&copyRegion, 1});

	cmdList->End();

	rhi::FenceHandle fence = device->CreateFence(false);

	// Submit the command list
	rhi::IRHICommandList *cmdListPtr = cmdList.Get();
	device->SubmitCommandLists({&cmdListPtr, 1}, rhi::QueueType::GRAPHICS, nullptr, nullptr, fence.Get());

	fence->Wait(UINT64_MAX);
}

void GpuSplatSorter::CreateComputePipelines()
{
	ShaderFactory shaderFactory(device, vfs);

	rhi::ShaderHandle depthCalcShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/depth_calc_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle packPairsShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/sort_pairs_pack_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle unpackIndicesShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/sort_pairs_unpack_cs",
	    rhi::ShaderStage::COMPUTE);

	// Portable shaders
	rhi::ShaderHandle histogramShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_histogram_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle radixPrefixScanShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_prefix_scan_cs",
	    rhi::ShaderStage::COMPUTE);

	// Subgroup-optimized shaders
	rhi::ShaderHandle histogramSubgroupShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_histogram_subgroup_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle radixPrefixScanSubgroupShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_prefix_scan_subgroup_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterPairsShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_pairs_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterPairsPrescanShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_pairs_prescan_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterUnpackShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_unpack_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterUnpackPrescanShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_unpack_prescan_cs",
	    rhi::ShaderStage::COMPUTE);

	// Create descriptor set layouts
	// Depth calculation layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Positions buffer
		layoutDesc.bindings.push_back({0,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 1: Output depth keys
		layoutDesc.bindings.push_back({1,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 2: Camera UBO
		layoutDesc.bindings.push_back({2,
		                               rhi::DescriptorType::UNIFORM_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 3: Per-splat mesh indices
		layoutDesc.bindings.push_back({3,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 4: Per-mesh model matrices
		layoutDesc.bindings.push_back({4,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		depthCalcSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Pack pairs layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input depth keys
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: Input splat indices
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: Output pairs
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		packPairsSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Unpack indices layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input pairs
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: Output indices
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		unpackIndicesSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Histogram layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input pairs
		layoutDesc.bindings.push_back({0,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 1: Output histograms
		layoutDesc.bindings.push_back({1,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		histogramSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Scan layout for global prefix sum
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Data buffer (histograms or block sums)
		layoutDesc.bindings.push_back({0,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 1: Block sums buffer
		layoutDesc.bindings.push_back({1,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		scanSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Scatter pairs layout for prescan method
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input pairs
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: Output pairs
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: Scanned histograms
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		scatterPairsSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Scatter pairs layout for integrated scan method
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input pairs
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 1: Output pairs
		layoutDesc.bindings.push_back({1, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		// Binding 2: Raw histograms (not scanned)
		layoutDesc.bindings.push_back({2, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});

		scatterPairsIntegratedSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create compute pipelines
	// Depth calculation pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = depthCalcShader.Get();
		pipelineDesc.descriptorSetLayouts     = {depthCalcSetLayout.Get()};

		// Push constants for depth calculation
		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(DepthCalcPushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		depthCalcPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Pack pairs pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = packPairsShader.Get();
		pipelineDesc.descriptorSetLayouts     = {packPairsSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(uint32_t);        // numElements only
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		packPairsPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Unpack indices pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = unpackIndicesShader.Get();
		pipelineDesc.descriptorSetLayouts     = {unpackIndicesSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(uint32_t);        // numElements only
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		unpackIndicesPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Portable histogram pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = histogramShader.Get();
		pipelineDesc.descriptorSetLayouts     = {histogramSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		histogramPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Subgroup-optimized histogram pipeline
	if (histogramSubgroupShader)
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = histogramSubgroupShader.Get();
		pipelineDesc.descriptorSetLayouts     = {histogramSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		histogramSubgroupPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Portable radix prefix scan pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = radixPrefixScanShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scanSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(ScanPushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		radixPrefixScanPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Subgroup-optimized radix prefix scan pipeline
	if (radixPrefixScanSubgroupShader)
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = radixPrefixScanSubgroupShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scanSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(ScanPushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		radixPrefixScanSubgroupPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter pairs pipeline with integrated prefix sum
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterPairsShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsIntegratedSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		scatterPairsPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter pairs pipeline with prescan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterPairsPrescanShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		scatterPairsPrescanPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter unpack pipeline with integrated prefix sum
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterUnpackShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsIntegratedSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		scatterUnpackPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter unpack pipeline with prescan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterUnpackPrescanShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		scatterUnpackPrescanPipeline = device->CreateComputePipeline(pipelineDesc);
	}
}

void GpuSplatSorter::CreateDescriptorSets()
{
	// Create descriptor set for depth calculation
	{
		depthCalcDescriptorSet = device->CreateDescriptorSet(depthCalcSetLayout.Get(), rhi::QueueType::COMPUTE);

		// We'll update the positions buffer binding dynamically in Sort()
		// Set up the other bindings here

		// Binding 1: Output pairs
		rhi::BufferBinding pairsBinding = {};
		pairsBinding.buffer             = sortPairsB.Get();
		pairsBinding.offset             = 0;
		pairsBinding.range              = 0;        // Whole buffer
		pairsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		depthCalcDescriptorSet->BindBuffer(1, pairsBinding);

		// Binding 2: Camera UBO
		rhi::BufferBinding cameraBinding = {};
		cameraBinding.buffer             = cameraUBO.Get();
		cameraBinding.offset             = 0;
		cameraBinding.range              = sizeof(math::mat4);
		cameraBinding.type               = rhi::DescriptorType::UNIFORM_BUFFER;
		depthCalcDescriptorSet->BindBuffer(2, cameraBinding);
	}

	// Create descriptor set for pack pairs
	{
		packPairsDescriptorSet = device->CreateDescriptorSet(packPairsSetLayout.Get(), rhi::QueueType::COMPUTE);

		rhi::BufferBinding keysBinding = {};
		keysBinding.buffer             = splatDepths.Get();
		keysBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		packPairsDescriptorSet->BindBuffer(0, keysBinding);

		rhi::BufferBinding indicesBinding = {};
		indicesBinding.buffer             = splatIndicesOriginal.Get();
		indicesBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		packPairsDescriptorSet->BindBuffer(1, indicesBinding);

		// Pack always writes to sortPairsA (pass 0 scatter reads from A when useA=true...
		// but actually pass 0 outputs to A, so pack must write to the INPUT of pass 0).
		// Pass 0 (useA=true): reads from opposite = B input would be wrong.
		// Actually with packed pairs: pack -> sortPairsB, pass 0 reads B writes A,
		// pass 1 reads A writes B, pass 2 reads B writes A, pass 3 reads A writes B.
		// Final result in B. Unpack reads B.
		rhi::BufferBinding pairsBinding = {};
		pairsBinding.buffer             = sortPairsB.Get();        // Pack into B, pass 0 reads from B
		pairsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		packPairsDescriptorSet->BindBuffer(2, pairsBinding);
	}

	// Create descriptor sets for unpack indices (one per output buffer)
	unpackIndicesDescriptorSets.resize(outputBufferCount);
	for (uint32_t bufferIdx = 0; bufferIdx < outputBufferCount; ++bufferIdx)
	{
		unpackIndicesDescriptorSets[bufferIdx] = device->CreateDescriptorSet(unpackIndicesSetLayout.Get(), rhi::QueueType::COMPUTE);

		// Binding 0: Final sorted pairs (always in sortPairsB after 4 passes)
		rhi::BufferBinding pairsBinding = {};
		pairsBinding.buffer             = sortPairsB.Get();
		pairsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		unpackIndicesDescriptorSets[bufferIdx]->BindBuffer(0, pairsBinding);

		// Binding 1: Output indices
		rhi::BufferBinding indicesBinding = {};
		indicesBinding.buffer             = outputBuffers[bufferIdx].Get();
		indicesBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		unpackIndicesDescriptorSets[bufferIdx]->BindBuffer(1, indicesBinding);
	}

	// Create descriptor sets for histogram (one per pass)
	{
		uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			histogramDescriptorSets[pass] = device->CreateDescriptorSet(histogramSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input pairs
			// Ping-pong: pack->B, pass 0 writes A, pass 1 writes B, etc.
			// Histogram reads from previous scatter output:
			// Pass 0: reads B (packed initial data)
			// Pass 1: reads A (scatter pass 0 output)
			// Pass 2: reads B (scatter pass 1 output)
			// Pass 3: reads A (scatter pass 2 output)
			rhi::BufferBinding inputBinding = {};
			bool               readFromA    = (pass % 2 != 0);        // odd passes read A, even passes read B
			inputBinding.buffer             = readFromA ? sortPairsA.Get() : sortPairsB.Get();
			inputBinding.offset             = 0;
			inputBinding.range              = 0;
			inputBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(0, inputBinding);

			// Binding 1: Output histograms
			rhi::BufferBinding histogramBinding = {};
			histogramBinding.buffer             = histograms.Get();
			histogramBinding.offset             = pass * histogramSizePerPass;
			histogramBinding.range              = histogramSizePerPass;
			histogramBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(1, histogramBinding);
		}
	}

	// Create descriptor sets for scan operations (one per radix pass)
	{
		uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			scanDescriptorSets[pass] = device->CreateDescriptorSet(scanSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Data buffer (histograms for this pass)
			rhi::BufferBinding dataBinding = {};
			dataBinding.buffer             = histograms.Get();
			dataBinding.offset             = pass * histogramSizePerPass;
			dataBinding.range              = histogramSizePerPass;
			dataBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scanDescriptorSets[pass]->BindBuffer(0, dataBinding);

			// Binding 1: Block sums buffer
			rhi::BufferBinding blockSumsBinding = {};
			blockSumsBinding.buffer             = blockSums.Get();
			blockSumsBinding.offset             = 0;
			blockSumsBinding.range              = 0;
			blockSumsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scanDescriptorSets[pass]->BindBuffer(1, blockSumsBinding);
		}
	}

	// Create descriptor set for scanning block sums (used in Pass 3 of scan)
	{
		scanBlockSumsDescriptorSet = device->CreateDescriptorSet(scanSetLayout.Get(), rhi::QueueType::COMPUTE);

		// Bind BlockSums as both input and output for this pass
		rhi::BufferBinding blockSumDataBinding = {};
		blockSumDataBinding.buffer             = blockSums.Get();
		blockSumDataBinding.offset             = 0;
		blockSumDataBinding.range              = 0;
		blockSumDataBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		scanBlockSumsDescriptorSet->BindBuffer(0, blockSumDataBinding);
		scanBlockSumsDescriptorSet->BindBuffer(1, blockSumDataBinding);
	}

	// Create descriptor sets for prescan scatter operations (one per pass)
	{
		uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			// Ping-pong: pack->B, pass 0 reads B writes A, pass 1 reads A writes B, etc.
			bool writeToA = (pass % 2 == 0);

			scatterPairsPrescanDescriptorSets[pass] = device->CreateDescriptorSet(scatterPairsSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input pairs
			rhi::BufferBinding pairsInBinding = {};
			pairsInBinding.buffer             = writeToA ? sortPairsB.Get() : sortPairsA.Get();
			pairsInBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsPrescanDescriptorSets[pass]->BindBuffer(0, pairsInBinding);

			// Binding 1: Output pairs
			rhi::BufferBinding pairsOutBinding = {};
			pairsOutBinding.buffer             = writeToA ? sortPairsA.Get() : sortPairsB.Get();
			pairsOutBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsPrescanDescriptorSets[pass]->BindBuffer(1, pairsOutBinding);

			// Binding 2: Scanned histograms
			rhi::BufferBinding histBinding = {};
			histBinding.buffer             = histograms.Get();
			histBinding.offset             = pass * histogramSizePerPass;
			histBinding.range              = histogramSizePerPass;
			histBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsPrescanDescriptorSets[pass]->BindBuffer(2, histBinding);
		}
	}

	// Create descriptor sets for integrated scan scatter operations (one per pass)
	{
		uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			bool writeToA = (pass % 2 == 0);

			scatterPairsIntegratedDescriptorSets[pass] = device->CreateDescriptorSet(scatterPairsIntegratedSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input pairs
			rhi::BufferBinding pairsInBinding = {};
			pairsInBinding.buffer             = writeToA ? sortPairsB.Get() : sortPairsA.Get();
			pairsInBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsIntegratedDescriptorSets[pass]->BindBuffer(0, pairsInBinding);

			// Binding 1: Output pairs
			rhi::BufferBinding pairsOutBinding = {};
			pairsOutBinding.buffer             = writeToA ? sortPairsA.Get() : sortPairsB.Get();
			pairsOutBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsIntegratedDescriptorSets[pass]->BindBuffer(1, pairsOutBinding);

			// Binding 2: Raw histograms (not scanned)
			rhi::BufferBinding histBinding = {};
			histBinding.buffer             = histograms.Get();
			histBinding.offset             = pass * histogramSizePerPass;
			histBinding.range              = histogramSizePerPass;
			histBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsIntegratedDescriptorSets[pass]->BindBuffer(2, histBinding);
		}
	}

	// Create descriptor sets for scatter unpack (only for final pass writes indices directly)
	{
		uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t finalPass            = RadixPasses - 1;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		scatterUnpackPrescanDescriptorSets.resize(outputBufferCount);
		scatterUnpackIntegratedDescriptorSets.resize(outputBufferCount);

		for (uint32_t bufferIdx = 0; bufferIdx < outputBufferCount; ++bufferIdx)
		{
			scatterUnpackPrescanDescriptorSets[bufferIdx] = device->CreateDescriptorSet(scatterPairsSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input pairs (pass 3 reads from A, since pass 3 writeToA = false)
			rhi::BufferBinding pairsInBinding = {};
			pairsInBinding.buffer             = sortPairsA.Get();
			pairsInBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterUnpackPrescanDescriptorSets[bufferIdx]->BindBuffer(0, pairsInBinding);

			// Binding 1: Output indices
			rhi::BufferBinding indicesOutBinding = {};
			indicesOutBinding.buffer             = outputBuffers[bufferIdx].Get();
			indicesOutBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterUnpackPrescanDescriptorSets[bufferIdx]->BindBuffer(1, indicesOutBinding);

			// Binding 2: Scanned histograms for pass 3
			rhi::BufferBinding histBinding = {};
			histBinding.buffer             = histograms.Get();
			histBinding.offset             = finalPass * histogramSizePerPass;
			histBinding.range              = histogramSizePerPass;
			histBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterUnpackPrescanDescriptorSets[bufferIdx]->BindBuffer(2, histBinding);

			scatterUnpackIntegratedDescriptorSets[bufferIdx] = device->CreateDescriptorSet(scatterPairsIntegratedSetLayout.Get(), rhi::QueueType::COMPUTE);
			scatterUnpackIntegratedDescriptorSets[bufferIdx]->BindBuffer(0, pairsInBinding);
			scatterUnpackIntegratedDescriptorSets[bufferIdx]->BindBuffer(1, indicesOutBinding);
			scatterUnpackIntegratedDescriptorSets[bufferIdx]->BindBuffer(2, histBinding);
		}
	}
}

void GpuSplatSorter::Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera)
{
	if (!isInitialized)
	{
		return;
	}

	// Reset and record begin timestamp
	uint32_t frameSlot       = timingFrameIndex % timingFrameLatency;
	uint32_t timestampOffset = frameSlot * 2;        // 2 timestamps per frame

	if (timestampQueryPool)
	{
		cmdList->ResetQueryPool(timestampQueryPool.Get(), timestampOffset, 2);
		cmdList->WriteTimestamp(timestampQueryPool.Get(), timestampOffset, rhi::StageMask::ComputeShader);
	}

	// Upload camera data
	math::mat4 viewMatrix = camera.GetViewMatrix();
	device->UpdateBuffer(cameraUBO.Get(), &viewMatrix, sizeof(math::mat4), 0);

#ifdef ENABLE_SORT_VERIFICATION
	lastViewMatrix = viewMatrix;
#endif

	// Update depth calc descriptor set - only bind if buffer changed to avoid in-use validation errors
	const Scene::GpuData &gpuData         = scene.GetGpuData();
	rhi::IRHIBuffer      *positionsBuffer = gpuData.positions.Get();

	if (positionsBuffer != lastBoundPositionsBuffer)
	{
		// Binding 0: Positions buffer
		rhi::BufferBinding positionsBinding = {};
		positionsBinding.buffer             = positionsBuffer;
		positionsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		depthCalcDescriptorSet->BindBuffer(0, positionsBinding);
		lastBoundPositionsBuffer = positionsBuffer;

		// Binding 3: Per-splat mesh indices
		if (gpuData.meshIndices)
		{
			rhi::BufferBinding meshIndicesBinding = {};
			meshIndicesBinding.buffer             = gpuData.meshIndices.Get();
			meshIndicesBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			depthCalcDescriptorSet->BindBuffer(3, meshIndicesBinding);
		}

		// Binding 4: Per-mesh model matrices
		if (gpuData.modelMatrices)
		{
			rhi::BufferBinding modelMatricesBinding = {};
			modelMatricesBinding.buffer             = gpuData.modelMatrices.Get();
			modelMatricesBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			depthCalcDescriptorSet->BindBuffer(4, modelMatricesBinding);
		}
	}

	RecordDepthCalculation(cmdList, scene, camera);

	// Sort with inline unpack: final pass writes indices directly to output buffer
#ifdef ENABLE_SORT_VERIFICATION
	lastSortUsedInlineUnpack = true;
#endif
	if (sortMethod == SortMethod::IntegratedScan)
	{
		RecordRadixSortIntegrated(cmdList, true);
	}
	else
	{
		RecordRadixSortPrescan(cmdList, true);
	}

	// Record end timestamp for GPU timing
	if (timestampQueryPool)
	{
		cmdList->WriteTimestamp(timestampQueryPool.Get(), timestampOffset + 1, rhi::StageMask::ComputeShader);
	}

	timingFrameIndex++;
}

void GpuSplatSorter::SortOnly(rhi::IRHICommandList *cmdList)
{
	if (!isInitialized)
	{
		return;
	}

	// Barrier: ensure external writes to splatDepths/splatIndicesOriginal are visible
	{
		rhi::BufferTransition preBarriers[2] = {};
		preBarriers[0].buffer                = splatDepths.Get();
		preBarriers[0].before                = rhi::ResourceState::ShaderReadWrite;
		preBarriers[0].after                 = rhi::ResourceState::GeneralRead;

		preBarriers[1].buffer = splatIndicesOriginal.Get();
		preBarriers[1].before = rhi::ResourceState::ShaderReadWrite;
		preBarriers[1].after  = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {preBarriers, 2}, {}, {});
	}

	RecordPackPairs(cmdList);

	// SortOnly uses separate pack/unpack, no inline unpack
#ifdef ENABLE_SORT_VERIFICATION
	lastSortUsedInlineUnpack = false;
#endif
	if (sortMethod == SortMethod::IntegratedScan)
	{
		RecordRadixSortIntegrated(cmdList);
	}
	else
	{
		RecordRadixSortPrescan(cmdList);
	}

	RecordUnpackIndices(cmdList);
}

void GpuSplatSorter::RecordDepthCalculation(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera)
{
	if (!depthCalcPipeline)
	{
		return;
	}

	cmdList->SetPipeline(depthCalcPipeline.Get());
	cmdList->BindDescriptorSet(0, depthCalcDescriptorSet.Get());

	DepthCalcPushConstants depthCalcPC = {};
	depthCalcPC.numElements            = totalSplatCount;
	depthCalcPC.sortAscending          = sortAscending ? 1 : 0;
	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0, {reinterpret_cast<const std::byte *>(&depthCalcPC), sizeof(DepthCalcPushConstants)});

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups);

	// Barrier: depth_calc writes uint2 pairs directly to sortPairsB
	rhi::BufferTransition transition = {};
	transition.buffer                = sortPairsB.Get();
	transition.before                = rhi::ResourceState::ShaderWrite;
	transition.after                 = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
	    rhi::PipelineScope::Compute,
	    rhi::PipelineScope::Compute,
	    {&transition, 1},
	    {},
	    {});
}

void GpuSplatSorter::RecordPackPairs(rhi::IRHICommandList *cmdList)
{
	cmdList->SetPipeline(packPairsPipeline.Get());
	cmdList->BindDescriptorSet(0, packPairsDescriptorSet.Get());

	uint32_t numElements = totalSplatCount;
	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0, {reinterpret_cast<const std::byte *>(&numElements), sizeof(uint32_t)});

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups);

	// Barrier: sortPairsB written by pack, needs to be readable for pass 0 histogram + scatter
	rhi::BufferTransition transition = {};
	transition.buffer                = sortPairsB.Get();
	transition.before                = rhi::ResourceState::ShaderWrite;
	transition.after                 = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
	    rhi::PipelineScope::Compute,
	    rhi::PipelineScope::Compute,
	    {&transition, 1},
	    {},
	    {});
}

void GpuSplatSorter::RecordUnpackIndices(rhi::IRHICommandList *cmdList)
{
	cmdList->SetPipeline(unpackIndicesPipeline.Get());
	cmdList->BindDescriptorSet(0, unpackIndicesDescriptorSets[activeOutputBufferIndex].Get());

	uint32_t numElements = totalSplatCount;
	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0, {reinterpret_cast<const std::byte *>(&numElements), sizeof(uint32_t)});

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups);

	// Barrier: output indices buffer written, needs to be readable for rendering
	rhi::BufferTransition transition = {};
	transition.buffer                = outputBuffers[activeOutputBufferIndex].Get();
	transition.before                = rhi::ResourceState::ShaderWrite;
	transition.after                 = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
	    rhi::PipelineScope::Compute,
	    rhi::PipelineScope::Compute,
	    {&transition, 1},
	    {},
	    {});
}

void GpuSplatSorter::RecordRadixSortPrescan(rhi::IRHICommandList *cmdList, bool inlineUnpack)
{
	if (!histogramPipeline || !radixPrefixScanPipeline || !scatterPairsPrescanPipeline)
	{
		LOG_WARNING("Required pipelines not created");
		return;
	}

	// Select pipelines based on shader variant
	rhi::IRHIPipeline *activeHistogramPipeline = histogramPipeline.Get();
	rhi::IRHIPipeline *activeScanPipeline      = radixPrefixScanPipeline.Get();

	if (shaderVariant == ShaderVariant::SubgroupOptimized)
	{
		if (histogramSubgroupPipeline && radixPrefixScanSubgroupPipeline)
		{
			activeHistogramPipeline = histogramSubgroupPipeline.Get();
			activeScanPipeline      = radixPrefixScanSubgroupPipeline.Get();
		}
		else
		{
			LOG_WARNING("Subgroup-optimized shaders not available, falling back to portable");
		}
	}

	uint32_t numWorkgroups = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);

	// Calculate how many blocks each workgroup needs to process
	uint32_t elementsPerWorkgroup  = (totalSplatCount + numWorkgroups - 1) / numWorkgroups;
	uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

	// Calculate scan parameters
	uint32_t elementsPerScanWorkgroup = WorkgroupSize * ElementPerThread;
	uint32_t numScanElements          = numWorkgroups * RadixSortBins;
	uint32_t numScanWorkgroups        = (numScanElements + elementsPerScanWorkgroup - 1) / elementsPerScanWorkgroup;

	// Size of histogram data for one pass
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

	// 8 bits per radix pass
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8;        // 0, 8, 16, 24

		PushConstants pushConstants         = {};
		pushConstants.numElements           = totalSplatCount;
		pushConstants.shift                 = shift;
		pushConstants.numWorkgroups         = numWorkgroups;
		pushConstants.numBlocksPerWorkgroup = numBlocksPerWorkgroup;

		// --- Pass 1: HISTOGRAM ---
		cmdList->SetPipeline(activeHistogramPipeline);
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer                = histograms.Get();
		histogramWriteTransition.before                = rhi::ResourceState::ShaderWrite;
		histogramWriteTransition.after                 = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&histogramWriteTransition, 1},
		    {},
		    {});

		// --- Pass 2: SCAN BLOCKS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());

		ScanPushConstants scanPushConstants = {};
		scanPushConstants.numElements       = numScanElements;
		scanPushConstants.passType          = 0;

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&scanPushConstants), sizeof(ScanPushConstants)});
		cmdList->Dispatch(numScanWorkgroups);

		// Barrier: Ensure block scan and block sum writes are finished
		{
			rhi::BufferTransition scanBlockTransitions[2];
			scanBlockTransitions[0].buffer = histograms.Get();
			scanBlockTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
			scanBlockTransitions[0].after  = rhi::ResourceState::ShaderReadWrite;

			scanBlockTransitions[1].buffer = blockSums.Get();
			scanBlockTransitions[1].before = rhi::ResourceState::ShaderReadWrite;
			scanBlockTransitions[1].after  = rhi::ResourceState::ShaderReadWrite;

			cmdList->Barrier(
			    rhi::PipelineScope::Compute,
			    rhi::PipelineScope::Compute,
			    {scanBlockTransitions, 2},
			    {},
			    {});
		}

		// --- Pass 3: SCAN BLOCK SUMS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanBlockSumsDescriptorSet.Get());

		scanPushConstants.numElements = numScanWorkgroups;
		scanPushConstants.passType    = 1;        // Scan block sums

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&scanPushConstants), sizeof(ScanPushConstants)});
		cmdList->Dispatch(1);        // Single workgroup

		// Barrier: Ensure scan of block sums is finished
		rhi::BufferTransition blockSumScanTransition = {};
		blockSumScanTransition.buffer                = blockSums.Get();
		blockSumScanTransition.before                = rhi::ResourceState::ShaderReadWrite;
		blockSumScanTransition.after                 = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&blockSumScanTransition, 1},
		    {},
		    {});

		// --- Pass 4: ADD OFFSETS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());

		scanPushConstants.numElements = numScanElements;
		scanPushConstants.passType    = 2;

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&scanPushConstants), sizeof(ScanPushConstants)});
		cmdList->Dispatch(numScanWorkgroups);

		// Barrier: Ensure final offsets are written
		{
			rhi::BufferTransition addOffsetTransitions[2];
			addOffsetTransitions[0].buffer = histograms.Get();
			addOffsetTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
			addOffsetTransitions[0].after  = rhi::ResourceState::GeneralRead;

			addOffsetTransitions[1].buffer = blockSums.Get();
			addOffsetTransitions[1].before = rhi::ResourceState::GeneralRead;
			addOffsetTransitions[1].after  = rhi::ResourceState::ShaderReadWrite;

			cmdList->Barrier(
			    rhi::PipelineScope::Compute,
			    rhi::PipelineScope::Compute,
			    {addOffsetTransitions, 2},
			    {},
			    {});
		}

		// --- Pass 5: SCATTER ---
		bool isFinalPass = (pass == RadixPasses - 1);

		if (isFinalPass && inlineUnpack)
		{
			// Final pass with inline unpack, writing indices directly to output buffer
			cmdList->SetPipeline(scatterUnpackPrescanPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterUnpackPrescanDescriptorSets[activeOutputBufferIndex].Get());
		}
		else
		{
			cmdList->SetPipeline(scatterPairsPrescanPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterPairsPrescanDescriptorSets[pass].Get());
		}

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier after scatter to ensure write completion
		{
			bool writeToA = (pass % 2 == 0);

			if (isFinalPass && inlineUnpack)
			{
				// Final pass wrote to the output indices buffer
				rhi::IRHIBuffer      *outputBuffer = outputBuffers[activeOutputBufferIndex].Get();
				rhi::BufferTransition transition   = {};
				transition.buffer                  = outputBuffer;
				transition.before                  = rhi::ResourceState::ShaderWrite;
				transition.after                   = rhi::ResourceState::GeneralRead;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {&transition, 1},
				    {},
				    {});
			}
			else
			{
				rhi::BufferTransition scatterTransitions[2];

				// Transition the pair buffer that was written to
				scatterTransitions[0].buffer = writeToA ? sortPairsA.Get() : sortPairsB.Get();
				scatterTransitions[0].before = rhi::ResourceState::ShaderWrite;
				scatterTransitions[0].after  = rhi::ResourceState::GeneralRead;

				// Transition histogram back to write-enabled for next pass
				scatterTransitions[1].buffer = histograms.Get();
				scatterTransitions[1].before = rhi::ResourceState::GeneralRead;
				scatterTransitions[1].after  = rhi::ResourceState::ShaderWrite;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {scatterTransitions, 2},
				    {},
				    {});
			}
		}
	}
}

void GpuSplatSorter::RecordRadixSortIntegrated(rhi::IRHICommandList *cmdList, bool inlineUnpack)
{
	if (!histogramPipeline || !scatterPairsPipeline)
	{
		LOG_WARNING("Required pipelines for integrated scan method not created");
		return;
	}

	// Select histogram pipeline based on shader variant
	rhi::IRHIPipeline *activeHistogramPipeline = histogramPipeline.Get();

	if (shaderVariant == ShaderVariant::SubgroupOptimized && histogramSubgroupPipeline)
	{
		activeHistogramPipeline = histogramSubgroupPipeline.Get();
	}

	uint32_t numWorkgroups = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);

	uint32_t elementsPerWorkgroup  = (totalSplatCount + numWorkgroups - 1) / numWorkgroups;
	uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

	// 8 bits per radix pass
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8;        // 0, 8, 16, 24

		PushConstants pushConstants         = {};
		pushConstants.numElements           = totalSplatCount;
		pushConstants.shift                 = shift;
		pushConstants.numWorkgroups         = numWorkgroups;
		pushConstants.numBlocksPerWorkgroup = numBlocksPerWorkgroup;

		// --- Pass 1: HISTOGRAM (raw counts, not scanned) ---
		cmdList->SetPipeline(activeHistogramPipeline);
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer                = histograms.Get();
		histogramWriteTransition.before                = rhi::ResourceState::ShaderWrite;
		histogramWriteTransition.after                 = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&histogramWriteTransition, 1},
		    {},
		    {});

		// --- Pass 2: SCATTER with integrated prefix sum ---
		bool isFinalPass = (pass == RadixPasses - 1);

		if (isFinalPass && inlineUnpack)
		{
			// Final pass with inline unpack, writing indices directly to output buffer
			cmdList->SetPipeline(scatterUnpackPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterUnpackIntegratedDescriptorSets[activeOutputBufferIndex].Get());
		}
		else
		{
			cmdList->SetPipeline(scatterPairsPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterPairsIntegratedDescriptorSets[pass].Get());
		}

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier after scatter to ensure write completion
		{
			bool writeToA = (pass % 2 == 0);

			if (isFinalPass && inlineUnpack)
			{
				// Final pass wrote to the output indices buffer
				rhi::IRHIBuffer      *outputBuffer = outputBuffers[activeOutputBufferIndex].Get();
				rhi::BufferTransition transition   = {};
				transition.buffer                  = outputBuffer;
				transition.before                  = rhi::ResourceState::ShaderWrite;
				transition.after                   = rhi::ResourceState::GeneralRead;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {&transition, 1},
				    {},
				    {});
			}
			else
			{
				rhi::BufferTransition scatterTransitions[2];

				// Transition the pair buffer that was written to
				scatterTransitions[0].buffer = writeToA ? sortPairsA.Get() : sortPairsB.Get();
				scatterTransitions[0].before = rhi::ResourceState::ShaderWrite;
				scatterTransitions[0].after  = rhi::ResourceState::GeneralRead;

				// Transition histogram back to write-enabled for next pass
				scatterTransitions[1].buffer = histograms.Get();
				scatterTransitions[1].before = rhi::ResourceState::GeneralRead;
				scatterTransitions[1].after  = rhi::ResourceState::ShaderWrite;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {scatterTransitions, 2},
				    {},
				    {});
			}
		}
	}
}

rhi::BufferHandle GpuSplatSorter::GetSortedIndices() const
{
	// With pack/unpack, the final sorted indices are always unpacked to the active output buffer
	return outputBuffers[activeOutputBufferIndex];
}

rhi::BufferHandle GpuSplatSorter::GetOutputBuffer(uint32_t index) const
{
	if (index < outputBuffers.size())
	{
		return outputBuffers[index];
	}
	return {};
}

uint32_t GpuSplatSorter::GetOutputBufferCount() const
{
	return outputBufferCount;
}

GpuSplatSorter::BufferInfo GpuSplatSorter::GetBufferInfo() const
{
	return {splatDepths, splatIndicesOriginal, sortPairsA, sortPairsB,
	        outputBuffers, histograms,
	        blockSums, cameraUBO};
}

void GpuSplatSorter::SetOutputBuffer(rhi::BufferHandle outputBuffer)
{
	// Switch activeOutputBufferIndex to select which unpack descriptor set to use.
	rhi::IRHIBuffer *targetBuffer = outputBuffer.Get();

	// Skip if already using this buffer
	if (targetBuffer == lastBoundOutputBuffer)
	{
		return;
	}

	lastBoundOutputBuffer = targetBuffer;

	for (uint32_t i = 0; i < outputBuffers.size(); ++i)
	{
		if (targetBuffer == outputBuffers[i].Get())
		{
			activeOutputBufferIndex = i;
			return;
		}
	}

	// External buffer: store as primary (index 0) for backward compatibility
	outputBuffers[0]        = std::move(outputBuffer);
	activeOutputBufferIndex = 0;
}

#ifdef ENABLE_SORT_VERIFICATION
void GpuSplatSorter::PrepareVerification(rhi::IRHICommandList *cmdList)
{
	if (!isInitialized)
	{
		LOG_WARNING("Cannot verify sorting - sorter not initialized");
		return;
	}

	// Create readback buffers for verification
	rhi::BufferDesc readbackDesc           = {};
	readbackDesc.size                      = totalSplatCount * sizeof(uint32_t);
	readbackDesc.usage                     = rhi::BufferUsage::TRANSFER_DST;
	readbackDesc.resourceUsage             = rhi::ResourceUsage::Readback;
	readbackDesc.hints.persistently_mapped = true;
	readbackDesc.hints.prefer_device_local = false;

	// Create buffers for depths, sorted keys, and sorted indices
	verificationDepths        = device->CreateBuffer(readbackDesc);
	verificationSortedKeys    = device->CreateBuffer(readbackDesc);
	verificationSortedIndices = device->CreateBuffer(readbackDesc);

	// Barrier: make sort outputs visible for transfer reads
	rhi::IRHIBuffer      *finalPairsBuffer = lastSortUsedInlineUnpack ? sortPairsA.Get() : sortPairsB.Get();
	rhi::IRHIBuffer      *activeOutput     = outputBuffers[activeOutputBufferIndex].Get();
	rhi::BufferTransition preBarriers[4];
	preBarriers[0] = {splatDepths.Get(), rhi::ResourceState::ShaderReadWrite, rhi::ResourceState::CopySource};
	preBarriers[1] = {finalPairsBuffer, rhi::ResourceState::ShaderReadWrite, rhi::ResourceState::CopySource};
	preBarriers[2] = {activeOutput, rhi::ResourceState::ShaderReadWrite, rhi::ResourceState::CopySource};
	preBarriers[3] = {histograms.Get(), rhi::ResourceState::ShaderReadWrite, rhi::ResourceState::CopySource};
	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Copy, {preBarriers, 4}, {}, {});

	// Copy depth keys (original unsorted)
	rhi::BufferCopy copyRegion = {};
	copyRegion.size            = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(splatDepths.Get(), verificationDepths.Get(), {&copyRegion, 1});

	// Copy sorted pairs from final pair buffer
	// - With inline unpack (Sort path): final pairs are in sortPairsA
	// - Without inline unpack (SortOnly path): final pairs are in sortPairsB
	readbackDesc.size                    = totalSplatCount * sizeof(uint32_t) * 2;        // uint2 pairs
	auto            verificationPairsBuf = device->CreateBuffer(readbackDesc);
	rhi::BufferCopy pairCopy             = {};
	pairCopy.size                        = totalSplatCount * sizeof(uint32_t) * 2;
	cmdList->CopyBuffer(finalPairsBuffer, verificationPairsBuf.Get(), {&pairCopy, 1});

	cmdList->CopyBuffer(activeOutput, verificationSortedIndices.Get(), {&copyRegion, 1});

	// Extract keys from pairs during CheckVerificationResults
	// Store the pairs readback buffer temporarily in verificationSortedKeys (reused)
	verificationSortedKeys = std::move(verificationPairsBuf);

	// Copy histogram data for verification
	uint32_t numWorkgroups        = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);
	uint32_t totalHistogramSize   = RadixPasses * histogramSizePerPass;

	readbackDesc.size     = totalHistogramSize;
	verificationHistogram = device->CreateBuffer(readbackDesc);

	rhi::BufferCopy histogramCopy = {};
	histogramCopy.size            = totalHistogramSize;
	cmdList->CopyBuffer(histograms.Get(), verificationHistogram.Get(), {&histogramCopy, 1});

	// Transition readback buffers for CPU access and restore source buffers for rendering
	{
		rhi::BufferTransition transitions[5];

		// Readback buffers: CopyDestination -> GeneralRead
		transitions[0] = {verificationDepths.Get(), rhi::ResourceState::CopyDestination, rhi::ResourceState::GeneralRead};
		transitions[1] = {verificationSortedKeys.Get(), rhi::ResourceState::CopyDestination, rhi::ResourceState::GeneralRead};
		transitions[2] = {verificationSortedIndices.Get(), rhi::ResourceState::CopyDestination, rhi::ResourceState::GeneralRead};
		transitions[3] = {verificationHistogram.Get(), rhi::ResourceState::CopyDestination, rhi::ResourceState::GeneralRead};

		// Output buffer: CopySource -> GeneralRead (needed for rendering later this frame)
		transitions[4] = {activeOutput, rhi::ResourceState::CopySource, rhi::ResourceState::GeneralRead};

		cmdList->Barrier(rhi::PipelineScope::Copy, rhi::PipelineScope::Graphics, {transitions, 5}, {}, {});
	}

	LOG_INFO("Verification prepared - depths, sorted results, and histogram will be checked after GPU work completes");
}

bool GpuSplatSorter::CheckVerificationResults(const container::vector<math::vec3> *testPositions)
{
	if (!verificationDepths)
	{
		LOG_WARNING("No verification data available - call PrepareVerification first");
		return false;
	}

	if (!testPositions || testPositions->size() != totalSplatCount)
	{
		LOG_ERROR("Test positions not provided or size mismatch. Expected {} positions", totalSplatCount);
		verificationDepths = nullptr;
		return false;
	}

	uint32_t *depthKeys = static_cast<uint32_t *>(verificationDepths->Map());
	if (!depthKeys)
	{
		LOG_ERROR("Failed to map verification buffer");
		verificationDepths = nullptr;
		return false;
	}

	LOG_INFO("=== Depth Calculation Verification Results ===");
	LOG_INFO("Total splats: {}", totalSplatCount);

	auto floatToSortableUint = [](float val) -> uint32_t {
		uint32_t u    = *reinterpret_cast<uint32_t *>(&val);
		uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
		return 0xFFFFFFFFu - (u ^ mask);
	};

	// Reverse function to convert sortable uint back to float for debugging
	auto sortableUintToFloat = [](uint32_t key) -> float {
		// Reverse: u ^ mask = 0xFFFFFFFF - key, then u = (0xFFFFFFFF - key) ^ mask
		// For negative floats: key was computed as 0xFFFFFFFF - (u ^ 0xFFFFFFFF) = ~(~u) = u... wait
		uint32_t invKey = 0xFFFFFFFFu - key;
		// If sign bit of result would be set, mask was 0xFFFFFFFF
		// If sign bit of result would be clear, mask was 0x80000000
		uint32_t u1 = invKey ^ 0x80000000u;        // for positive floats
		uint32_t u2 = invKey ^ 0xFFFFFFFFu;        // for negative floats
		// Use the one that has consistent sign
		uint32_t u = (u1 & 0x80000000u) ? u2 : u1;
		return *reinterpret_cast<float *>(&u);
	};

	container::vector<uint32_t> cpuDepthKeys;
	cpuDepthKeys.resize(totalSplatCount);

	bool     allCorrect           = true;
	uint32_t incorrectCount       = 0;
	uint32_t withinToleranceCount = 0;

	// Allow small floating-point precision differences between CPU and GPU
	constexpr uint32_t maxUlpDifference = 256;

	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		// Expected depth for test data using actual test position
		math::vec3 worldPos      = (*testPositions)[i];
		math::vec4 viewPos       = lastViewMatrix * math::vec4(worldPos, 1.0f);
		float      expectedDepth = -viewPos.z;        // depth = -viewPos.z (same as GPU)

		uint32_t expectedKey = floatToSortableUint(expectedDepth);
		uint32_t gpuKey      = depthKeys[i];

		// Store CPU-calculated key for histogram verification
		cpuDepthKeys[i] = gpuKey;

		// Check if keys are exactly equal or within floating-point tolerance
		uint32_t keyDiff           = (gpuKey > expectedKey) ? (gpuKey - expectedKey) : (expectedKey - gpuKey);
		bool     isExact           = (gpuKey == expectedKey);
		bool     isWithinTolerance = (keyDiff <= maxUlpDifference);

		if (!isWithinTolerance)
		{
			allCorrect = false;
			incorrectCount++;

			if (incorrectCount <= 10)
			{
				float gpuDepth  = sortableUintToFloat(gpuKey);
				float depthDiff = gpuDepth - expectedDepth;
				LOG_INFO("  Splat[{}]: GPU Key={:#010x}, Expected Key={:#010x}, KeyDiff={}", i, gpuKey, expectedKey, keyDiff);
				LOG_INFO("    GPU Depth={:.6f}, CPU Depth={:.6f}, DepthDiff={:.6f}, WorldZ={:.2f}",
				         gpuDepth, expectedDepth, depthDiff, worldPos.z);
			}
		}
		else if (!isExact)
		{
			withinToleranceCount++;
		}
	}

	if (allCorrect)
	{
		if (withinToleranceCount > 0)
		{
			LOG_INFO("All {} depth keys are correct ✓ ({} within FP tolerance)", totalSplatCount, withinToleranceCount);
		}
		else
		{
			LOG_INFO("All {} depth keys are correct ✓", totalSplatCount);
		}
	}
	else
	{
		LOG_ERROR("Found {} incorrect depth keys out of {} (tolerance={} ULPs)", incorrectCount, totalSplatCount, maxUlpDifference);

		LOG_INFO("Sample of correct values for reference:");
		for (uint32_t i = 0; i < std::min<uint32_t>(5, totalSplatCount); ++i)
		{
			math::vec3 worldPos      = (*testPositions)[i];
			math::vec4 viewPos       = lastViewMatrix * math::vec4(worldPos, 1.0f);
			float      expectedDepth = -viewPos.z;
			uint32_t   expectedKey   = floatToSortableUint(expectedDepth);
			uint32_t   gpuKey        = depthKeys[i];

			if (gpuKey == expectedKey)
			{
				LOG_INFO("  Splat[{}]: GPU Key={:#010x}, Depth={:.2f} ✓",
				         i, gpuKey, expectedDepth);
			}
		}
	}

	LOG_INFO("");
	LOG_INFO("Note: Splats are in original order (not sorted yet)");
	LOG_INFO("Verifying only that depth calculation is correct for each splat");

	verificationDepths->Unmap();

	// Initialize CPU sorted keys/indices for progressive sorting
	container::vector<uint32_t> cpuSortedKeys = cpuDepthKeys;
	container::vector<uint32_t> cpuSortedIndices;
	cpuSortedIndices.resize(totalSplatCount);
	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		cpuSortedIndices[i] = i;
	}

	container::vector<uint32_t> keysBeforePass4;
	keysBeforePass4.resize(totalSplatCount);

	bool histogramCorrect = true;
	if (verificationHistogram)
	{
		LOG_INFO("");
		if (sortMethod == SortMethod::Prescan)
		{
			LOG_INFO("=== Scanned Histogram Verification (Prefix Sums) ===");
			LOG_INFO("Note: After scan passes, histograms contain exclusive prefix sums, not counts");
		}
		else
		{
			LOG_INFO("=== Raw Histogram Verification (Counts) ===");
			LOG_INFO("Note: IntegratedScan keeps raw histogram counts, computes prefix sums inline");
		}

		uint32_t  numWorkgroups = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t *histogramData = static_cast<uint32_t *>(verificationHistogram->Map());

		container::vector<uint32_t> tempKeys;
		tempKeys.resize(totalSplatCount);
		container::vector<uint32_t> tempIndices;
		tempIndices.resize(totalSplatCount);

		if (histogramData)
		{
			for (uint32_t pass = 0; pass < RadixPasses; ++pass)
			{
				uint32_t shift = pass * 8;
				LOG_INFO("");
				LOG_INFO("Pass {} - Histogram results (shift={}, bits {}-{}):",
				         pass + 1, shift, shift, shift + 7);
				LOG_INFO("Number of workgroups: {}", numWorkgroups);

				// Calculate expected histogram for this pass
				container::vector<uint32_t> expectedHistogram;
				expectedHistogram.resize(RadixSortBins);
				for (uint32_t i = 0; i < RadixSortBins; ++i)
				{
					expectedHistogram[i] = 0;
				}

				// Count elements in each bin based on appropriate keys for this pass
				for (uint32_t i = 0; i < totalSplatCount; ++i)
				{
					// For pass 0, use original keys; for later passes, use sorted keys from previous pass
					uint32_t depthKey = (pass == 0) ? cpuDepthKeys[i] : cpuSortedKeys[i];

					// Extract the relevant 8 bits for this pass
					uint32_t bin = (depthKey >> shift) & (RadixSortBins - 1);
					expectedHistogram[bin]++;
				}

				// Combine histograms from all workgroups for this pass
				container::vector<uint32_t> combinedGpuHistogram;
				combinedGpuHistogram.resize(RadixSortBins);
				for (uint32_t i = 0; i < RadixSortBins; ++i)
				{
					combinedGpuHistogram[i] = 0;
				}

				// Histogram data layout: [bin0: WG0..WGn | bin1: WG0..WGn | ...]
				uint32_t passOffset = pass * numWorkgroups * RadixSortBins;
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
					{
						uint32_t idx = passOffset + bin * numWorkgroups + wg;
						combinedGpuHistogram[bin] += histogramData[idx];
					}
				}

				LOG_INFO("Verifying scanned histogram (prefix sums):");

				// Calculate expected prefix sums from the histogram counts
				container::vector<uint32_t> expectedPrefixSums;
				expectedPrefixSums.resize(numWorkgroups * RadixSortBins);

				// First, gather all histogram counts in the order they're stored in GPU memory
				container::vector<uint32_t> allHistogramCounts;
				allHistogramCounts.resize(numWorkgroups * RadixSortBins);

				// Build the full histogram array (all workgroups for this pass)
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
					{
						allHistogramCounts[bin * numWorkgroups + wg] = 0;
					}
				}

				// Distribute elements to workgroups as the GPU does
				uint32_t elementsPerWorkgroup  = (totalSplatCount + numWorkgroups - 1) / numWorkgroups;
				uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

				// Debug: Track elements in specific bins for WG[0]
				uint32_t debugWg0Bin51Count = 0;
				uint32_t debugWg0Bin52Count = 0;
				uint32_t debugAllBin51Count = 0;

				// Each workgroup processes numBlocksPerWorkgroup blocks of WorkgroupSize elements
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					uint32_t startIdx = wg * numBlocksPerWorkgroup * WorkgroupSize;

					for (uint32_t block = 0; block < numBlocksPerWorkgroup; ++block)
					{
						uint32_t blockStartIdx = startIdx + block * WorkgroupSize;

						for (uint32_t tid = 0; tid < WorkgroupSize; ++tid)
						{
							uint32_t i = blockStartIdx + tid;
							if (i >= totalSplatCount)
								break;

							// For pass 0, use original keys; for later passes, use sorted keys from previous pass
							uint32_t depthKey = (pass == 0) ? cpuDepthKeys[i] : cpuSortedKeys[i];
							uint32_t bin      = (depthKey >> shift) & (RadixSortBins - 1);

							// Use bin-major indexing: bin * numWorkgroups + wg
							allHistogramCounts[bin * numWorkgroups + wg]++;

							// Debug tracking for bins 51 and 52
							if (bin == 51)
							{
								debugAllBin51Count++;
								if (wg == 0)
								{
									debugWg0Bin51Count++;
									if (debugWg0Bin51Count <= 3)
									{
										LOG_INFO("    DEBUG: Element {} (depth key={:#010x}) -> WG[0] Bin[51]", i, depthKey);
									}
								}
								if (i >= 240 && i <= 260)
								{
									LOG_INFO("    DEBUG: Element {} -> WG[{}] Bin[51] (blockStart={}, numBlocks={})",
									         i, wg, startIdx, numBlocksPerWorkgroup);
								}
							}
							if (wg == 0 && bin == 52)
							{
								debugWg0Bin52Count++;
								if (debugWg0Bin52Count <= 3)
								{
									LOG_INFO("    DEBUG: Element {} (depth key={:#010x}) -> WG[0] Bin[52]", i, depthKey);
								}
							}
						}
					}
				}

				if (pass == 0)
				{
					LOG_INFO("    DEBUG: WG[0] Bin[51] CPU count: {}", allHistogramCounts[51 * numWorkgroups + 0]);
					LOG_INFO("    DEBUG: WG[0] Bin[52] CPU count: {}", allHistogramCounts[52 * numWorkgroups + 0]);
					LOG_INFO("    DEBUG: Total Bin[51] count across all WGs: {}", debugAllBin51Count);
					LOG_INFO("    DEBUG: Elements per workgroup: {}", elementsPerWorkgroup);
					LOG_INFO("    DEBUG: Actual num workgroups: {}", numWorkgroups);
				}

				// Now perform exclusive prefix sum
				uint32_t runningSum = 0;
				for (uint32_t i = 0; i < numWorkgroups * RadixSortBins; ++i)
				{
					expectedPrefixSums[i] = runningSum;
					runningSum += allHistogramCounts[i];
				}

				// Debug: Print first few values from GPU histogram
				bool verifyPrefixSumsDebug = (sortMethod == SortMethod::Prescan);
				if (pass == 0 || pass == 1)
				{
					LOG_INFO("  Debug: First 10 GPU histogram values (bin-major order, expecting {}):",
					         verifyPrefixSumsDebug ? "prefix sums" : "raw counts");
					for (uint32_t i = 0; i < std::min<uint32_t>(10, numWorkgroups * RadixSortBins); ++i)
					{
						uint32_t expected = verifyPrefixSumsDebug ? expectedPrefixSums[i] : allHistogramCounts[i];
						LOG_INFO("    histData[{}] = {} (expected={})", i,
						         histogramData[passOffset + i], expected);
					}
					LOG_INFO("  Debug: Values at key positions (bin spacing = {}):", numWorkgroups);
					LOG_INFO("    histData[0] (bin0,wg0) = {}", histogramData[passOffset + 0]);
					LOG_INFO("    histData[{}] (bin1,wg0) = {}", numWorkgroups, histogramData[passOffset + numWorkgroups]);
					LOG_INFO("    histData[{}] (bin2,wg0) = {}", 2 * numWorkgroups, histogramData[passOffset + 2 * numWorkgroups]);

					if (pass == 1)
					{
						// Check WG[1] Bin[0] specifically
						uint32_t idx_wg1_bin0      = passOffset + 0 * numWorkgroups + 1;
						uint32_t expected_wg1_bin0 = expectedPrefixSums[0 * numWorkgroups + 1];
						LOG_INFO("  Debug: WG[1] Bin[0] - idx={}, GPU={}, Expected={}",
						         idx_wg1_bin0, histogramData[idx_wg1_bin0], expected_wg1_bin0);
					}

					if (pass == 2)
					{
						// Extended debug for Pass 2
						LOG_INFO("  Debug Pass 2: Buffer layout info:");
						LOG_INFO("    numWorkgroups = {}", numWorkgroups);
						LOG_INFO("    passOffset = {} (pass * {} * {})", passOffset, numWorkgroups, RadixSortBins);

						// Check raw buffer values at passOffset
						LOG_INFO("  Debug Pass 2: Raw buffer values at passOffset:");
						for (uint32_t i = 0; i < 5; ++i)
						{
							LOG_INFO("    histogramData[{}] = {}", passOffset + i, histogramData[passOffset + i]);
						}

						// Check if there's data at the expected location
						uint32_t nonZeroCount = 0;
						uint32_t totalChecked = std::min<uint32_t>(1000, numWorkgroups * RadixSortBins);
						for (uint32_t i = 0; i < totalChecked; ++i)
						{
							if (histogramData[passOffset + i] != 0)
								nonZeroCount++;
						}
						LOG_INFO("  Debug Pass 2: Non-zero values in first {} entries: {}", totalChecked, nonZeroCount);
					}
				}

				// Compare GPU results with expected values
				// Prescan: compare with prefix sums (histogram is scanned)
				// IntegratedScan: compare with raw counts (histogram is not scanned)
				bool     passCorrect      = true;
				uint32_t mismatchCount    = 0;
				uint32_t nonZeroBins      = 0;
				bool     verifyPrefixSums = (sortMethod == SortMethod::Prescan);

				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
					{
						// Bin-major indexing: bin * numWorkgroups + wg
						uint32_t idx           = passOffset + bin * numWorkgroups + wg;
						uint32_t globalIdx     = bin * numWorkgroups + wg;
						uint32_t gpuValue      = histogramData[idx];
						uint32_t expectedValue = verifyPrefixSums ? expectedPrefixSums[globalIdx] : allHistogramCounts[globalIdx];

						if (gpuValue != expectedValue || allHistogramCounts[globalIdx] > 0)
						{
							bool isCorrect = (gpuValue == expectedValue);
							if (!isCorrect)
							{
								passCorrect      = false;
								histogramCorrect = false;
								mismatchCount++;

								if (mismatchCount <= 10)
								{
									// Previous bin
									if (bin > 0)
									{
										uint32_t prevBin       = bin - 1;
										uint32_t prevIdx       = passOffset + prevBin * numWorkgroups + wg;
										uint32_t prevGlobalIdx = prevBin * numWorkgroups + wg;
										uint32_t prevExpected  = verifyPrefixSums ? expectedPrefixSums[prevGlobalIdx] : allHistogramCounts[prevGlobalIdx];
										LOG_INFO("    Previous: WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count was {})",
										         wg, prevBin, histogramData[prevIdx], prevExpected,
										         allHistogramCounts[prevGlobalIdx]);
									}

									// Next bin
									if (bin < RadixSortBins - 1)
									{
										uint32_t nextBin       = bin + 1;
										uint32_t nextIdx       = passOffset + nextBin * numWorkgroups + wg;
										uint32_t nextGlobalIdx = nextBin * numWorkgroups + wg;
										uint32_t nextExpected  = verifyPrefixSums ? expectedPrefixSums[nextGlobalIdx] : allHistogramCounts[nextGlobalIdx];
										LOG_INFO("    Next: WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count was {})",
										         wg, nextBin, histogramData[nextIdx], nextExpected,
										         allHistogramCounts[nextGlobalIdx]);
									}

									// Current error bin
									LOG_ERROR("  WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count was {}) ✗",
									          wg, bin, gpuValue, expectedValue, allHistogramCounts[globalIdx]);
								}
							}
							else if (nonZeroBins < 5 || allHistogramCounts[globalIdx] > 0)
							{
								if (nonZeroBins < 5)
								{
									LOG_INFO("  WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count={}) ✓",
									         wg, bin, gpuValue, expectedValue, allHistogramCounts[globalIdx]);
								}
								nonZeroBins++;
							}
						}
					}
				}

				if (mismatchCount > 10)
				{
					LOG_ERROR("  ... ({} more mismatches) ...", mismatchCount - 10);
				}

				// Verify the total sum
				uint32_t totalExpected = runningSum;
				LOG_INFO("Total prefix sum: Expected={} (should equal total splats)", totalExpected);

				if (passCorrect)
				{
					LOG_INFO("Pass {} {} histogram verification: PASSED ✓", pass + 1, verifyPrefixSums ? "scanned" : "raw");
				}
				else
				{
					LOG_ERROR("Pass {} {} histogram verification: FAILED ({} mismatches) ✗", pass + 1, verifyPrefixSums ? "scanned" : "raw", mismatchCount);
				}

				if (pass == 3)
				{
					keysBeforePass4 = cpuSortedKeys;
				}

				// Perform CPU radix sort for this pass to prepare for next pass
				container::vector<container::vector<uint32_t>> wgCounts;
				wgCounts.resize(numWorkgroups);
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					wgCounts[wg].resize(RadixSortBins, 0);
				}

				// Count elements per (workgroup, bin)
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					uint32_t startIdx = wg * numBlocksPerWorkgroup * WorkgroupSize;
					for (uint32_t block = 0; block < numBlocksPerWorkgroup; ++block)
					{
						uint32_t blockStartIdx = startIdx + block * WorkgroupSize;
						for (uint32_t tid = 0; tid < WorkgroupSize; ++tid)
						{
							uint32_t i = blockStartIdx + tid;
							if (i >= totalSplatCount)
								break;
							uint32_t bin = (cpuSortedKeys[i] >> shift) & (RadixSortBins - 1);
							wgCounts[wg][bin]++;
						}
					}
				}

				// Compute global offsets in bin-major order: [bin0: WG0..WGn | bin1: WG0..WGn | ...]
				container::vector<uint32_t> globalOffsets;
				globalOffsets.resize(RadixSortBins * numWorkgroups);
				uint32_t runningOffset = 0;
				for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
				{
					for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
					{
						globalOffsets[bin * numWorkgroups + wg] = runningOffset;
						runningOffset += wgCounts[wg][bin];
					}
				}

				// Scatter elements using workgroup-based ordering (matching GPU)
				container::vector<uint32_t> localOffsets(RadixSortBins * numWorkgroups);
				for (uint32_t i = 0; i < RadixSortBins * numWorkgroups; ++i)
				{
					localOffsets[i] = globalOffsets[i];
				}

				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					uint32_t startIdx = wg * numBlocksPerWorkgroup * WorkgroupSize;
					for (uint32_t block = 0; block < numBlocksPerWorkgroup; ++block)
					{
						uint32_t blockStartIdx = startIdx + block * WorkgroupSize;
						for (uint32_t tid = 0; tid < WorkgroupSize; ++tid)
						{
							uint32_t i = blockStartIdx + tid;
							if (i >= totalSplatCount)
								break;
							uint32_t key       = cpuSortedKeys[i];
							uint32_t idx       = cpuSortedIndices[i];
							uint32_t bin       = (key >> shift) & (RadixSortBins - 1);
							uint32_t outputPos = localOffsets[bin * numWorkgroups + wg]++;

							tempKeys[outputPos]    = key;
							tempIndices[outputPos] = idx;
						}
					}
				}

				// Swap buffers for next pass
				cpuSortedKeys.swap(tempKeys);
				cpuSortedIndices.swap(tempIndices);
			}

			verificationHistogram->Unmap();
		}
		else
		{
			LOG_ERROR("Failed to map histogram verification buffer");
			histogramCorrect = false;
		}
	}

	verificationDepths = nullptr;

	// Verify scatter results (sorted keys and indices)
	bool scatterCorrect = true;
	if (verificationSortedKeys && verificationSortedIndices)
	{
		LOG_INFO("");
		LOG_INFO("=== Scatter Verification (Final Sorted Results) ===");

		uint32_t *sortedPairsRaw = static_cast<uint32_t *>(verificationSortedKeys->Map());
		uint32_t *sortedIndices  = static_cast<uint32_t *>(verificationSortedIndices->Map());

		// Extract keys from interleaved uint2 pairs (key, index, key, index, ...)
		container::vector<uint32_t> sortedKeysVec;
		if (sortedPairsRaw)
		{
			sortedKeysVec.resize(totalSplatCount);
			for (uint32_t i = 0; i < totalSplatCount; ++i)
				sortedKeysVec[i] = sortedPairsRaw[i * 2];
		}
		uint32_t *sortedKeys = sortedKeysVec.data();

		if (sortedPairsRaw && sortedIndices)
		{
			// Calculate workgroup distribution
			uint32_t numWorkgroups         = std::min<uint32_t>(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
			uint32_t elementsPerWorkgroup  = (totalSplatCount + numWorkgroups - 1) / numWorkgroups;
			uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

			// Debug: Show which keys are processed by each workgroup
			LOG_INFO("Debug: Keys processed by each workgroup (Pass 4, final sorting):");
			for (uint32_t wg = 0; wg < std::min<uint32_t>(2u, numWorkgroups); ++wg)
			{
				uint32_t startIdx = wg * numBlocksPerWorkgroup * WorkgroupSize;
				uint32_t endIdx   = std::min(startIdx + numBlocksPerWorkgroup * WorkgroupSize, totalSplatCount);
				LOG_INFO("  WG[{}]: Processing elements [{}, {})", wg, startIdx, endIdx);

				if (wg == 0)
				{
					LOG_INFO("    Last 5 elements of WG[0] (input to pass 4):");
					for (uint32_t i = std::max(250u, startIdx); i < std::min(startIdx + 256, totalSplatCount); ++i)
					{
						uint32_t key = keysBeforePass4[i];        // These are from pass 3, input to pass 4
						uint32_t bin = (key >> 24) & 0xFF;
						LOG_INFO("      Element[{}]: key={:#010x}, bin={:#04x}", i, key, bin);
					}
				}
				if (wg == 1)
				{
					LOG_INFO("    First 5 elements of WG[1] (input to pass 4):");
					for (uint32_t i = startIdx; i < std::min(startIdx + 5, totalSplatCount); ++i)
					{
						uint32_t key = keysBeforePass4[i];        // These are from pass 3, input to pass 4
						uint32_t bin = (key >> 24) & 0xFF;
						LOG_INFO("      Element[{}]: key={:#010x}, bin={:#04x}", i, key, bin);
					}
				}
			}

			// Debug: Check the scanned histogram for the last pass
			LOG_INFO("Debug: Checking scanned histogram offsets for Pass 4:");
			uint32_t pass  = 3;
			uint32_t shift = pass * 8;

			if (verificationHistogram)
			{
				uint32_t *histData = static_cast<uint32_t *>(verificationHistogram->Map());
				if (histData)
				{
					// The scanned histogram contains offsets for ALL workgroups
					// Each workgroup has RADIX_SORT_BINS entries
					uint32_t histogramPassOffset = pass * numWorkgroups * RadixSortBins;
					LOG_INFO("  Total histogram entries: {}", numWorkgroups * RadixSortBins);
					LOG_INFO("  Pass 4 starts at offset: {}", histogramPassOffset);
					LOG_INFO("  Critical bins for debugging:");

					for (uint32_t wg = 0; wg < std::min<uint32_t>(3u, numWorkgroups); ++wg)
					{
						uint32_t bin3a = histData[histogramPassOffset + 0x3a * numWorkgroups + wg];
						uint32_t bin3b = histData[histogramPassOffset + 0x3b * numWorkgroups + wg];
						uint32_t binc5 = histData[histogramPassOffset + 0xc5 * numWorkgroups + wg];
						uint32_t binc6 = histData[histogramPassOffset + 0xc6 * numWorkgroups + wg];

						LOG_INFO("    WG[{}]: Bin[0x3a]={}, Bin[0x3b]={}, Bin[0xc5]={}, Bin[0xc6]={}",
						         wg, bin3a, bin3b, binc5, binc6);
					}

					LOG_INFO("  Element counts in critical bins (reconstructed from offsets):");
					for (uint32_t wg = 0; wg < std::min<uint32_t>(3u, numWorkgroups); ++wg)
					{
						if (wg < numWorkgroups - 1)
						{
							// Count for bin 0x3a = next WG's offset for 0x3a minus this WG's offset for 0x3a
							uint32_t count3a = histData[histogramPassOffset + 0x3a * numWorkgroups + (wg + 1)] -
							                   histData[histogramPassOffset + 0x3a * numWorkgroups + wg];
							uint32_t countc5 = histData[histogramPassOffset + 0xc5 * numWorkgroups + (wg + 1)] -
							                   histData[histogramPassOffset + 0xc5 * numWorkgroups + wg];

							LOG_INFO("    WG[{}]: Bin[0x3a] has {} elements, Bin[0xc5] has {} elements",
							         wg, count3a, countc5);
						}
					}

					verificationHistogram->Unmap();
				}
			}

			// Verify sorted keys are in ascending order
			LOG_INFO("Verifying sorted key order:");
			bool     keysOrdered     = true;
			uint32_t outOfOrderCount = 0;

			LOG_INFO("  Debug: numWorkgroups={}, elementsPerWorkgroup={}, numBlocksPerWorkgroup={}",
			         numWorkgroups, elementsPerWorkgroup, numBlocksPerWorkgroup);

			for (uint32_t i = 1; i < totalSplatCount; ++i)
			{
				if (sortedKeys[i - 1] > sortedKeys[i])
				{
					keysOrdered = false;
					outOfOrderCount++;
					if (outOfOrderCount <= 5)
					{
						uint32_t wg_prev    = (i - 1) / (numBlocksPerWorkgroup * WorkgroupSize);
						uint32_t wg_curr    = i / (numBlocksPerWorkgroup * WorkgroupSize);
						uint32_t block_prev = ((i - 1) % (numBlocksPerWorkgroup * WorkgroupSize)) / WorkgroupSize;
						uint32_t block_curr = (i % (numBlocksPerWorkgroup * WorkgroupSize)) / WorkgroupSize;

						LOG_INFO("    Element {} was processed by WG[{}] block[{}]", i - 1, wg_prev, block_prev);
						LOG_INFO("    Element {} was processed by WG[{}] block[{}]", i, wg_curr, block_curr);

						uint32_t start = (i >= 5) ? i - 5 : 0;
						uint32_t end   = (i + 5 < totalSplatCount) ? i + 5 : totalSplatCount - 1;
						LOG_INFO("    Context around failure:");
						for (uint32_t j = start; j <= end; ++j)
						{
							uint32_t wg        = j / (numBlocksPerWorkgroup * WorkgroupSize);
							uint32_t block     = (j % (numBlocksPerWorkgroup * WorkgroupSize)) / WorkgroupSize;
							bool     highlight = (j == i - 1 || j == i);
							LOG_INFO("      {}[{}]={:#010x} (idx={}, WG[{}] block[{}])",
							         highlight ? ">>>" : "   ",
							         j, sortedKeys[j], sortedIndices[j], wg, block);
						}
						LOG_ERROR("  Keys out of order at position {}: GPU[{}]={:#010x} > GPU[{}]={:#010x}",
						          i, i - 1, sortedKeys[i - 1], i, sortedKeys[i]);
					}
				}
			}

			if (keysOrdered)
			{
				LOG_INFO("  All {} keys are correctly sorted in ascending order ✓", totalSplatCount);
			}
			else
			{
				LOG_ERROR("  Found {} out-of-order pairs in sorted keys ✗", outOfOrderCount);
				scatterCorrect = false;
			}

			// Compare GPU results with CPU simulation
			LOG_INFO("Comparing GPU vs CPU radix sort results:");
			uint32_t keyMismatches = 0;
			uint32_t idxMismatches = 0;

			for (uint32_t i = 0; i < totalSplatCount; ++i)
			{
				if (sortedKeys[i] != cpuSortedKeys[i])
				{
					keyMismatches++;
					if (keyMismatches <= 5)
					{
						LOG_ERROR("  Key mismatch at position {}: GPU={:#010x}, CPU={:#010x}",
						          i, sortedKeys[i], cpuSortedKeys[i]);
					}
				}

				if (sortedIndices[i] != cpuSortedIndices[i])
				{
					idxMismatches++;
					if (idxMismatches <= 5)
					{
						LOG_ERROR("  Index mismatch at position {}: GPU={}, CPU={}",
						          i, sortedIndices[i], cpuSortedIndices[i]);
					}
				}
			}

			if (keyMismatches == 0 && idxMismatches == 0)
			{
				LOG_INFO("  GPU radix sort matches CPU simulation exactly ✓");

				LOG_INFO("  First 5 sorted splats (farthest, smallest keys):");
				for (uint32_t i = 0; i < std::min<uint32_t>(5, totalSplatCount); ++i)
				{
					uint32_t   splatIdx = sortedIndices[i];
					math::vec3 worldPos = (*testPositions)[splatIdx];
					math::vec4 viewPos  = lastViewMatrix * math::vec4(worldPos, 1.0f);
					float      depth    = -viewPos.z;
					LOG_INFO("    [{}]: key={:#010x}, splatIdx={}, worldZ={:.1f}, depth={:.1f}",
					         i, sortedKeys[i], splatIdx, worldPos.z, depth);
				}

				LOG_INFO("  Last 5 sorted splats (nearest, largest keys):");
				uint32_t start = totalSplatCount > 5 ? totalSplatCount - 5 : 0;
				for (uint32_t i = start; i < totalSplatCount; ++i)
				{
					uint32_t   splatIdx = sortedIndices[i];
					math::vec3 worldPos = (*testPositions)[splatIdx];
					math::vec4 viewPos  = lastViewMatrix * math::vec4(worldPos, 1.0f);
					float      depth    = -viewPos.z;
					LOG_INFO("    [{}]: key={:#010x}, splatIdx={}, worldZ={:.1f}, depth={:.1f}",
					         i, sortedKeys[i], splatIdx, worldPos.z, depth);
				}
			}
			else
			{
				LOG_ERROR("  Found {} key mismatches and {} index mismatches ✗", keyMismatches, idxMismatches);
				scatterCorrect = false;
			}

			verificationSortedKeys->Unmap();
			verificationSortedIndices->Unmap();
		}
		else
		{
			LOG_ERROR("Failed to map sorted verification buffers");
			scatterCorrect = false;
		}

		verificationSortedKeys    = nullptr;
		verificationSortedIndices = nullptr;
	}

	verificationHistogram = nullptr;

	LOG_INFO("");
	if (allCorrect && histogramCorrect && scatterCorrect)
	{
		LOG_INFO("=== VERIFICATION PASSED: All tests successful ===");
	}
	else
	{
		LOG_ERROR("=== VERIFICATION FAILED: Some tests failed ===");
		if (!allCorrect)
			LOG_ERROR("  - Depth calculation failed");
		if (!histogramCorrect)
			LOG_ERROR("  - Histogram/scan failed");
		if (!scatterCorrect)
			LOG_ERROR("  - Scatter/sort failed");
	}

	return allCorrect && histogramCorrect && scatterCorrect;
}

bool GpuSplatSorter::VerifySortOrder()
{
	if (!verificationSortedKeys)
	{
		LOG_WARNING("No verification data available - call PrepareVerification first");
		return false;
	}

	uint32_t *sortedPairsRaw = static_cast<uint32_t *>(verificationSortedKeys->Map());
	if (!sortedPairsRaw)
	{
		LOG_ERROR("Failed to map verification buffer for sort order check");
		return false;
	}

	// Extract keys in the final sorted order
	container::vector<uint32_t> sortedKeysVec;
	sortedKeysVec.resize(totalSplatCount);

	if (lastSortUsedInlineUnpack)
	{
		// With inline unpack: sortPairsA has pairs sorted by bits 0-23, sortIndicesB has final sorted order
		// Build a lookup map: splatID -> key, read sorted indices and look up keys
		container::vector<uint32_t> splatIDToKey;
		splatIDToKey.resize(totalSplatCount);
		for (uint32_t i = 0; i < totalSplatCount; ++i)
		{
			uint32_t key          = sortedPairsRaw[i * 2];            // .x component
			uint32_t splatID      = sortedPairsRaw[i * 2 + 1];        // .y component
			splatIDToKey[splatID] = key;
		}

		uint32_t *sortedIndices = static_cast<uint32_t *>(verificationSortedIndices->Map());
		if (!sortedIndices)
		{
			LOG_ERROR("Failed to map sorted indices buffer");
			verificationSortedKeys->Unmap();
			return false;
		}

		for (uint32_t i = 0; i < totalSplatCount; ++i)
		{
			uint32_t splatID = sortedIndices[i];
			sortedKeysVec[i] = splatIDToKey[splatID];
		}

		verificationSortedIndices->Unmap();
	}
	else
	{
		// Without inline unpack: sortPairsB has fully sorted pairs
		for (uint32_t i = 0; i < totalSplatCount; ++i)
			sortedKeysVec[i] = sortedPairsRaw[i * 2];
	}
	uint32_t *sortedKeys = sortedKeysVec.data();

	LOG_INFO("=== Simple Sort Order Verification ===");
	LOG_INFO("Checking if {} keys are sorted in ascending order...", totalSplatCount);

	bool     isOrdered       = true;
	uint32_t outOfOrderCount = 0;

	for (uint32_t i = 1; i < totalSplatCount; ++i)
	{
		if (sortedKeys[i - 1] > sortedKeys[i])
		{
			isOrdered = false;
			outOfOrderCount++;

			if (outOfOrderCount <= 5)
			{
				LOG_ERROR("  Out of order at position {}: key[{}]={:#010x} > key[{}]={:#010x}",
				          i, i - 1, sortedKeys[i - 1], i, sortedKeys[i]);
			}
		}
	}

	if (isOrdered)
	{
		LOG_INFO("Sort order verification PASSED: All {} keys are in ascending order ✓", totalSplatCount);

		LOG_INFO("First 3 keys: {:#010x}, {:#010x}, {:#010x}",
		         sortedKeys[0],
		         totalSplatCount > 1 ? sortedKeys[1] : 0,
		         totalSplatCount > 2 ? sortedKeys[2] : 0);

		if (totalSplatCount >= 3)
		{
			LOG_INFO("Last 3 keys:  {:#010x}, {:#010x}, {:#010x}",
			         sortedKeys[totalSplatCount - 3],
			         sortedKeys[totalSplatCount - 2],
			         sortedKeys[totalSplatCount - 1]);
		}
	}
	else
	{
		LOG_ERROR("Sort order verification FAILED: Found {} out-of-order pairs ✗", outOfOrderCount);
	}

	verificationSortedKeys->Unmap();

	return isOrdered;
}
#endif

void GpuSplatSorter::ReadTimingResults()
{
	if (!timestampQueryPool || timingFrameIndex < timingFrameLatency)
	{
		return;
	}

	// Read from the oldest frame slot (N frames ago)
	uint32_t readFrameIndex  = (timingFrameIndex - timingFrameLatency) % timingFrameLatency;
	uint32_t timestampOffset = readFrameIndex * 2;

	uint64_t timestamps[2];
	bool     valid = device->GetQueryPoolResults(
        timestampQueryPool.Get(),
        timestampOffset,
        2,
        timestamps,
        sizeof(timestamps),
        sizeof(uint64_t),
        rhi::QueryResultFlags::WAIT);

	if (valid)
	{
		double ticks   = static_cast<double>(timestamps[1] - timestamps[0]);
		lastSortTimeMs = (ticks * timestampPeriod) / 1000000.0;
	}
}

bool GpuSplatSorter::ReadTimingResultsNonBlocking()
{
	if (!timestampQueryPool || timingFrameIndex < timingFrameLatency)
	{
		return false;
	}

	// Read from the oldest frame slot (N frames ago)
	uint32_t readFrameIndex  = (timingFrameIndex - timingFrameLatency) % timingFrameLatency;
	uint32_t timestampOffset = readFrameIndex * 2;

	uint64_t timestamps[2];
	bool     valid = device->GetQueryPoolResults(
        timestampQueryPool.Get(),
        timestampOffset,
        2,
        timestamps,
        sizeof(timestamps),
        sizeof(uint64_t),
        rhi::QueryResultFlags::NONE);

	if (valid)
	{
		double ticks   = static_cast<double>(timestamps[1] - timestamps[0]);
		lastSortTimeMs = (ticks * timestampPeriod) / 1000000.0;
		return true;
	}

	return false;
}

void GpuSplatSorter::CreateIndirectComputePipelines()
{
	ShaderFactory shaderFactory(device, vfs);

	// Load indirect shader variants
	rhi::ShaderHandle histogramIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_histogram_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle histogramSubgroupIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_histogram_subgroup_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterPairsIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_pairs_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterPairsPrescanIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_pairs_prescan_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterUnpackIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_unpack_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterUnpackPrescanIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_unpack_prescan_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle radixPrefixScanIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_prefix_scan_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle radixPrefixScanSubgroupIndirectShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_prefix_scan_subgroup_indirect_cs",
	    rhi::ShaderStage::COMPUTE);

	// Create sortParams descriptor set layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};
		layoutDesc.bindings.push_back({0, rhi::DescriptorType::STORAGE_BUFFER, 1, rhi::ShaderStageFlags::COMPUTE});
		sortParamsSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Push constant range for indirect shaders, just a single uint32_t for shift or passType
	rhi::PushConstantRange pushConstantRange = {};
	pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
	pushConstantRange.offset                 = 0;
	pushConstantRange.size                   = sizeof(uint32_t);

	// Portable histogram indirect pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = histogramIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {histogramSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		histogramIndirectPipeline             = device->CreateComputePipeline(pipelineDesc);
	}

	// Subgroup-optimized histogram indirect pipeline
	if (histogramSubgroupIndirectShader)
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = histogramSubgroupIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {histogramSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		histogramSubgroupIndirectPipeline     = device->CreateComputePipeline(pipelineDesc);
	}

	// Portable radix prefix scan indirect pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = radixPrefixScanIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scanSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		radixPrefixScanIndirectPipeline       = device->CreateComputePipeline(pipelineDesc);
	}

	// Subgroup-optimized radix prefix scan indirect pipeline
	if (radixPrefixScanSubgroupIndirectShader)
	{
		rhi::ComputePipelineDesc pipelineDesc   = {};
		pipelineDesc.computeShader              = radixPrefixScanSubgroupIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts       = {scanSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges         = {pushConstantRange};
		radixPrefixScanSubgroupIndirectPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter pairs indirect pipeline with integrated scan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterPairsIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsIntegratedSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		scatterPairsIndirectPipeline          = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter pairs indirect pipeline with prescan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterPairsPrescanIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		scatterPairsPrescanIndirectPipeline   = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter unpack indirect pipeline with integrated scan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterUnpackIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsIntegratedSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		scatterUnpackIndirectPipeline         = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter unpack indirect pipeline with prescan
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterUnpackPrescanIndirectShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsSetLayout.Get(), sortParamsSetLayout.Get()};
		pipelineDesc.pushConstantRanges       = {pushConstantRange};
		scatterUnpackPrescanIndirectPipeline  = device->CreateComputePipeline(pipelineDesc);
	}
}

void GpuSplatSorter::CreateIndirectDescriptorSets()
{
	// Create one sortParams descriptor set per indirect args buffer
	sortParamsDescriptorSets.resize(indirectArgsBufferPtrs.size());

	for (size_t i = 0; i < indirectArgsBufferPtrs.size(); ++i)
	{
		auto descriptorSet = device->CreateDescriptorSet(sortParamsSetLayout.Get(), rhi::QueueType::COMPUTE);

		rhi::BufferBinding binding = {};
		binding.buffer             = indirectArgsBufferPtrs[i];
		binding.offset             = 0;
		binding.range              = 32;        // First 32 bytes = SortParams
		binding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		descriptorSet->BindBuffer(0, binding);

		sortParamsDescriptorSets[i] = std::move(descriptorSet);
	}
}

void GpuSplatSorter::SortIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex)
{
	if (!isInitialized || indirectArgsBufferPtrs.empty())
	{
		LOG_WARNING("GpuSplatSorter::SortIndirect called before initialization");
		return;
	}

	// Reset and record begin timestamp
	uint32_t frameSlot       = timingFrameIndex % timingFrameLatency;
	uint32_t timestampOffset = frameSlot * 2;        // 2 timestamps per frame

	if (timestampQueryPool)
	{
		cmdList->ResetQueryPool(timestampQueryPool.Get(), timestampOffset, 2);
		cmdList->WriteTimestamp(timestampQueryPool.Get(), timestampOffset, rhi::StageMask::ComputeShader);
	}

	// Record sorting
	if (sortMethod == SortMethod::IntegratedScan)
	{
		RecordRadixSortIntegratedIndirect(cmdList, indirectBufferIndex);
	}
	else
	{
		RecordRadixSortPrescanIndirect(cmdList, indirectBufferIndex);
	}

	// Record end timestamp for GPU timing
	if (timestampQueryPool)
	{
		cmdList->WriteTimestamp(timestampQueryPool.Get(), timestampOffset + 1, rhi::StageMask::ComputeShader);
	}

	timingFrameIndex++;
}

void GpuSplatSorter::RecordRadixSortIntegratedIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex)
{
	if (!histogramIndirectPipeline || !scatterPairsIndirectPipeline)
	{
		LOG_WARNING("Required indirect pipelines for integrated scan method not created");
		return;
	}

	// Select histogram pipeline based on shader variant
	rhi::IRHIPipeline *activeHistogramPipeline = histogramIndirectPipeline.Get();

	if (shaderVariant == ShaderVariant::SubgroupOptimized && histogramSubgroupIndirectPipeline)
	{
		activeHistogramPipeline = histogramSubgroupIndirectPipeline.Get();
	}

	// DispatchIndirect offsets from the indirectArgs buffer layout:
	//   Offset 32: histogram/scatter dispatches (numWorkgroups)
	//   Offset 44: scan blocks/add offsets dispatches (numScanWorkgroups)
	//   Offset 56: scan block sums dispatch ({1,1,1})
	constexpr uint32_t dispatchOffsetHistogramScatter = 32;

	// 8 bits per radix pass
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8;        // 0, 8, 16, 24

		// --- Pass 1: HISTOGRAM (raw counts, not scanned) ---
		cmdList->SetPipeline(activeHistogramPipeline);
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());
		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&shift), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetHistogramScatter);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer                = histograms.Get();
		histogramWriteTransition.before                = rhi::ResourceState::ShaderWrite;
		histogramWriteTransition.after                 = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&histogramWriteTransition, 1},
		    {},
		    {});

		// --- Pass 2: SCATTER with integrated prefix sum ---
		bool isFinalPass = (pass == RadixPasses - 1);

		if (isFinalPass)
		{
			// Final pass with inline unpack, writing indices directly to output buffer
			cmdList->SetPipeline(scatterUnpackIndirectPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterUnpackIntegratedDescriptorSets[activeOutputBufferIndex].Get());
		}
		else
		{
			cmdList->SetPipeline(scatterPairsIndirectPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterPairsIntegratedDescriptorSets[pass].Get());
		}

		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&shift), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetHistogramScatter);

		// Barrier after scatter to ensure write completion
		{
			bool writeToA = (pass % 2 == 0);

			if (isFinalPass)
			{
				// Final pass wrote to the output indices buffer
				rhi::IRHIBuffer      *outputBuffer = outputBuffers[activeOutputBufferIndex].Get();
				rhi::BufferTransition transition   = {};
				transition.buffer                  = outputBuffer;
				transition.before                  = rhi::ResourceState::ShaderWrite;
				transition.after                   = rhi::ResourceState::GeneralRead;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {&transition, 1},
				    {},
				    {});
			}
			else
			{
				rhi::BufferTransition scatterTransitions[2];

				// Transition the pair buffer that was written to
				scatterTransitions[0].buffer = writeToA ? sortPairsA.Get() : sortPairsB.Get();
				scatterTransitions[0].before = rhi::ResourceState::ShaderWrite;
				scatterTransitions[0].after  = rhi::ResourceState::GeneralRead;

				// Transition histogram back to write-enabled for next pass
				scatterTransitions[1].buffer = histograms.Get();
				scatterTransitions[1].before = rhi::ResourceState::GeneralRead;
				scatterTransitions[1].after  = rhi::ResourceState::ShaderWrite;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {scatterTransitions, 2},
				    {},
				    {});
			}
		}
	}
}

void GpuSplatSorter::RecordRadixSortPrescanIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex)
{
	if (!histogramIndirectPipeline || !radixPrefixScanIndirectPipeline || !scatterPairsPrescanIndirectPipeline)
	{
		LOG_WARNING("Required indirect pipelines for prescan method not created");
		return;
	}

	// Select pipelines based on shader variant
	rhi::IRHIPipeline *activeHistogramPipeline = histogramIndirectPipeline.Get();
	rhi::IRHIPipeline *activeScanPipeline      = radixPrefixScanIndirectPipeline.Get();

	if (shaderVariant == ShaderVariant::SubgroupOptimized)
	{
		if (histogramSubgroupIndirectPipeline && radixPrefixScanSubgroupIndirectPipeline)
		{
			activeHistogramPipeline = histogramSubgroupIndirectPipeline.Get();
			activeScanPipeline      = radixPrefixScanSubgroupIndirectPipeline.Get();
		}
		else
		{
			LOG_WARNING("Subgroup-optimized indirect shaders not available, falling back to portable");
		}
	}

	// DispatchIndirect offsets from the indirectArgs buffer layout:
	//   Offset 32: histogram/scatter dispatches (numWorkgroups)
	//   Offset 44: scan blocks/add offsets dispatches (numScanWorkgroups)
	//   Offset 56: scan block sums dispatch ({1,1,1})
	constexpr uint32_t dispatchOffsetHistogramScatter = 32;
	constexpr uint32_t dispatchOffsetScanBlocks       = 44;
	constexpr uint32_t dispatchOffsetScanBlockSums    = 56;

	// 8 bits per radix pass
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8;        // 0, 8, 16, 24

		// --- Pass 1: HISTOGRAM ---
		cmdList->SetPipeline(activeHistogramPipeline);
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());
		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&shift), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetHistogramScatter);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer                = histograms.Get();
		histogramWriteTransition.before                = rhi::ResourceState::ShaderWrite;
		histogramWriteTransition.after                 = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&histogramWriteTransition, 1},
		    {},
		    {});

		// --- Pass 2: SCAN BLOCKS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());
		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		uint32_t passType = 0;        // Scan blocks
		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&passType), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetScanBlocks);

		// Barrier: Ensure block scan and block sum writes are finished
		{
			rhi::BufferTransition scanBlockTransitions[2];
			scanBlockTransitions[0].buffer = histograms.Get();
			scanBlockTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
			scanBlockTransitions[0].after  = rhi::ResourceState::ShaderReadWrite;

			scanBlockTransitions[1].buffer = blockSums.Get();
			scanBlockTransitions[1].before = rhi::ResourceState::ShaderReadWrite;
			scanBlockTransitions[1].after  = rhi::ResourceState::ShaderReadWrite;

			cmdList->Barrier(
			    rhi::PipelineScope::Compute,
			    rhi::PipelineScope::Compute,
			    {scanBlockTransitions, 2},
			    {},
			    {});
		}

		// --- Pass 3: SCAN BLOCK SUMS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanBlockSumsDescriptorSet.Get());
		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		passType = 1;        // Scan block sums
		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&passType), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetScanBlockSums);

		// Barrier: Ensure scan of block sums is finished
		rhi::BufferTransition blockSumScanTransition = {};
		blockSumScanTransition.buffer                = blockSums.Get();
		blockSumScanTransition.before                = rhi::ResourceState::ShaderReadWrite;
		blockSumScanTransition.after                 = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&blockSumScanTransition, 1},
		    {},
		    {});

		// --- Pass 4: ADD OFFSETS ---
		cmdList->SetPipeline(activeScanPipeline);
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());
		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		passType = 2;
		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&passType), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetScanBlocks);

		// Barrier: Ensure final offsets are written
		{
			rhi::BufferTransition addOffsetTransitions[2];
			addOffsetTransitions[0].buffer = histograms.Get();
			addOffsetTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
			addOffsetTransitions[0].after  = rhi::ResourceState::GeneralRead;

			addOffsetTransitions[1].buffer = blockSums.Get();
			addOffsetTransitions[1].before = rhi::ResourceState::GeneralRead;
			addOffsetTransitions[1].after  = rhi::ResourceState::ShaderReadWrite;

			cmdList->Barrier(
			    rhi::PipelineScope::Compute,
			    rhi::PipelineScope::Compute,
			    {addOffsetTransitions, 2},
			    {},
			    {});
		}

		// --- Pass 5: SCATTER ---
		bool isFinalPass = (pass == RadixPasses - 1);

		if (isFinalPass)
		{
			// Final pass with inline unpack, writing indices directly to output buffer
			cmdList->SetPipeline(scatterUnpackPrescanIndirectPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterUnpackPrescanDescriptorSets[activeOutputBufferIndex].Get());
		}
		else
		{
			cmdList->SetPipeline(scatterPairsPrescanIndirectPipeline.Get());
			cmdList->BindDescriptorSet(0, scatterPairsPrescanDescriptorSets[pass].Get());
		}

		cmdList->BindDescriptorSet(1, sortParamsDescriptorSets[indirectBufferIndex].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&shift), sizeof(uint32_t)});
		cmdList->DispatchIndirect(indirectArgsBufferPtrs[indirectBufferIndex], dispatchOffsetHistogramScatter);

		// Barrier after scatter to ensure write completion
		{
			bool writeToA = (pass % 2 == 0);

			if (isFinalPass)
			{
				// Final pass wrote to the output indices buffer
				rhi::IRHIBuffer      *outputBuffer = outputBuffers[activeOutputBufferIndex].Get();
				rhi::BufferTransition transition   = {};
				transition.buffer                  = outputBuffer;
				transition.before                  = rhi::ResourceState::ShaderWrite;
				transition.after                   = rhi::ResourceState::GeneralRead;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {&transition, 1},
				    {},
				    {});
			}
			else
			{
				rhi::BufferTransition scatterTransitions[2];

				// Transition the pair buffer that was written to
				scatterTransitions[0].buffer = writeToA ? sortPairsA.Get() : sortPairsB.Get();
				scatterTransitions[0].before = rhi::ResourceState::ShaderWrite;
				scatterTransitions[0].after  = rhi::ResourceState::GeneralRead;

				// Transition histogram back to write-enabled for next pass
				scatterTransitions[1].buffer = histograms.Get();
				scatterTransitions[1].before = rhi::ResourceState::GeneralRead;
				scatterTransitions[1].after  = rhi::ResourceState::ShaderWrite;

				cmdList->Barrier(
				    rhi::PipelineScope::Compute,
				    rhi::PipelineScope::Compute,
				    {scatterTransitions, 2},
				    {},
				    {});
			}
		}
	}
}

}        // namespace msplat::engine