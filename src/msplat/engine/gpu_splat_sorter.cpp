#include <msplat/engine/gpu_splat_sorter.h>
#include <msplat/engine/shader_factory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <cstring>
#include <algorithm>

namespace msplat::engine
{

GpuSplatSorter::GpuSplatSorter(rhi::IRHIDevice *device)
	: device(device)
	, totalSplatCount(0)
	, isInitialized(false)
{
}

void GpuSplatSorter::Initialize(uint32_t totalSplatCount)
{
	if (isInitialized)
	{
		return;
	}

	this->totalSplatCount = totalSplatCount;

	rhi::BufferDesc bufferDesc = {};
	// TODO: If verification is not needed, remove rhi::BufferUsage::TRANSFER_SRC
	bufferDesc.usage = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
	bufferDesc.resourceUsage = rhi::ResourceUsage::Static;

	// Depth keys buffer
	bufferDesc.size = totalSplatCount * sizeof(uint32_t);
	splatDepths = device->CreateBuffer(bufferDesc);

	// Original indices buffer
	bufferDesc.size = totalSplatCount * sizeof(uint32_t);
	splatIndicesOriginal = device->CreateBuffer(bufferDesc);

	// Sort buffers (ping-pong)
	sortKeysA = device->CreateBuffer(bufferDesc);
	sortKeysB = device->CreateBuffer(bufferDesc);
	sortIndicesA = device->CreateBuffer(bufferDesc);
	sortIndicesB = device->CreateBuffer(bufferDesc);

	// Histogram buffer for radix sort
	// Each workgroup generates a histogram with 256 bins for each of the 4 passes
	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	bufferDesc.size = RadixPasses * numWorkgroups * RadixSortBins * sizeof(uint32_t);
	histograms = device->CreateBuffer(bufferDesc);

	// Block sums buffer for hierarchical scan
	// We need space for the maximum number of workgroups we'll use for scanning
	// For the scan passes, we process WORKGROUP_SIZE * ELEMENTS_PER_THREAD elements per workgroup
	constexpr uint32_t ELEMENTS_PER_THREAD = 4;
	uint32_t elementsPerWorkgroup = WorkgroupSize * ELEMENTS_PER_THREAD;
	uint32_t numWorkgroupsForScan = (numWorkgroups * RadixSortBins + elementsPerWorkgroup - 1) / elementsPerWorkgroup;
	bufferDesc.size = numWorkgroupsForScan * sizeof(uint32_t);
	blockSums = device->CreateBuffer(bufferDesc);

	// Camera UBO (host-visible for frequent updates)
	bufferDesc.usage = rhi::BufferUsage::UNIFORM | rhi::BufferUsage::TRANSFER_DST;
	bufferDesc.resourceUsage = rhi::ResourceUsage::DynamicUpload;
	bufferDesc.hints.persistently_mapped = true;
	bufferDesc.hints.prefer_device_local = false;
	bufferDesc.size = sizeof(math::mat4); // For view matrix
	cameraUBO = device->CreateBuffer(bufferDesc);

	// Reset to device local for subsequent buffers
	bufferDesc.resourceUsage = rhi::ResourceUsage::Static;
	bufferDesc.hints.persistently_mapped = false;
	bufferDesc.hints.prefer_device_local = true;

	CreateInitialIndicesBuffer(totalSplatCount);

	CreateComputePipelines();

	CreateDescriptorSets();

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
	rhi::BufferDesc stagingDesc = {};
	stagingDesc.size = totalSplatCount * sizeof(uint32_t);
	stagingDesc.usage = rhi::BufferUsage::TRANSFER_SRC;
	stagingDesc.resourceUsage = rhi::ResourceUsage::DynamicUpload;
	stagingDesc.hints.persistently_mapped = true;
	stagingDesc.initialData = initialIndices.data();

	rhi::BufferHandle stagingBuffer = device->CreateBuffer(stagingDesc);

	// Copy from staging to device buffer
	rhi::CommandListHandle cmdList = device->CreateCommandList(rhi::QueueType::GRAPHICS);
	cmdList->Begin();

	rhi::BufferCopy copyRegion = {};
	copyRegion.size = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(stagingBuffer.Get(), splatIndicesOriginal.Get(), {&copyRegion, 1});

	cmdList->End();

	rhi::FenceHandle fence = device->CreateFence(false);

	// Submit the command list
	rhi::IRHICommandList* cmdListPtr = cmdList.Get();
	device->SubmitCommandLists({&cmdListPtr, 1}, rhi::QueueType::GRAPHICS, nullptr, nullptr, fence.Get());

	fence->Wait(UINT64_MAX);
}

void GpuSplatSorter::CreateComputePipelines()
{
	ShaderFactory shaderFactory(device);

	rhi::ShaderHandle depthCalcShader = shaderFactory.getOrCreateShader(
		"shaders/compiled/depth_calc.comp.spv",
		rhi::ShaderStage::COMPUTE
	);

	rhi::ShaderHandle histogramShader = shaderFactory.getOrCreateShader(
		"shaders/compiled/radixsort_histograms.comp.spv",
		rhi::ShaderStage::COMPUTE
	);

	rhi::ShaderHandle radixPrefixScanShader = shaderFactory.getOrCreateShader(
		"shaders/compiled/radix_prefix_scan.comp.spv",
		rhi::ShaderStage::COMPUTE
	);

	rhi::ShaderHandle scatterPairsShader = shaderFactory.getOrCreateShader(
		"shaders/compiled/radix_scatter_pairs.comp.spv",
		rhi::ShaderStage::COMPUTE
	);

	// Create descriptor set layouts
	// Depth calculation layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Positions buffer
		layoutDesc.bindings.push_back({
			0,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 1: Output depth keys
		layoutDesc.bindings.push_back({
			1,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 2: Camera UBO
		layoutDesc.bindings.push_back({
			2,
			rhi::DescriptorType::UNIFORM_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		depthCalcSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Histogram layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input elements (depth keys)
		layoutDesc.bindings.push_back({
			0,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 1: Output histograms
		layoutDesc.bindings.push_back({
			1,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		histogramSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Scan layout for global prefix sum
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Data buffer (histograms or block sums)
		layoutDesc.bindings.push_back({
			0,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 1: Block sums buffer
		layoutDesc.bindings.push_back({
			1,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		scanSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Scatter pairs layout (depth keys and splat indices)
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input depth keys
		layoutDesc.bindings.push_back({
			0,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 1: Input splat indices
		layoutDesc.bindings.push_back({
			1,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 2: Output depth keys
		layoutDesc.bindings.push_back({
			2,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 3: Output splat indices
		layoutDesc.bindings.push_back({
			3,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		// Binding 4: Scanned histograms
		layoutDesc.bindings.push_back({
			4,
			rhi::DescriptorType::STORAGE_BUFFER,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		scatterPairsSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create compute pipelines
	// Depth calculation pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader = depthCalcShader.Get();
		pipelineDesc.descriptorSetLayouts = {depthCalcSetLayout.Get()};

		// Push constants for numElements
		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(uint32_t);
		pipelineDesc.pushConstantRanges = {pushConstantRange};

		depthCalcPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Histogram pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader = histogramShader.Get();
		pipelineDesc.descriptorSetLayouts = {histogramSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges = {pushConstantRange};

		histogramPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Radix prefix scan pipeline for global prefix sum
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader = radixPrefixScanShader.Get();
		pipelineDesc.descriptorSetLayouts = {scanSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(ScanPushConstants);
		pipelineDesc.pushConstantRanges = {pushConstantRange};

		radixPrefixScanPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Scatter pairs pipeline (depth keys and splat indices)
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader = scatterPairsShader.Get();
		pipelineDesc.descriptorSetLayouts = {scatterPairsSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges = {pushConstantRange};

		scatterPairsPipeline = device->CreateComputePipeline(pipelineDesc);
	}
}

void GpuSplatSorter::CreateDescriptorSets()
{
	// Create descriptor set for depth calculation
	{
		depthCalcDescriptorSet = device->CreateDescriptorSet(depthCalcSetLayout.Get(), rhi::QueueType::COMPUTE);

		// We'll update the positions buffer binding dynamically in Sort()
		// Set up the other bindings here

		// Binding 1: Output depth keys
		rhi::BufferBinding depthBinding = {};
		depthBinding.buffer = splatDepths.Get();
		depthBinding.offset = 0;
		depthBinding.range = 0; // 0 means whole buffer
		depthBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
		depthCalcDescriptorSet->BindBuffer(1, depthBinding);

		// Binding 2: Camera UBO
		rhi::BufferBinding cameraBinding = {};
		cameraBinding.buffer = cameraUBO.Get();
		cameraBinding.offset = 0;
		cameraBinding.range = sizeof(math::mat4);
		cameraBinding.type = rhi::DescriptorType::UNIFORM_BUFFER;
		depthCalcDescriptorSet->BindBuffer(2, cameraBinding);
	}

	// Create descriptor sets for histogram (one per pass)
	{
		uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			histogramDescriptorSets[pass] = device->CreateDescriptorSet(histogramSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input elements
			rhi::BufferBinding inputBinding = {};
			if (pass == 0)
			{
				inputBinding.buffer = splatDepths.Get(); // First pass uses original depth keys
			}
			else
			{
				// Subsequent passes read from the output of the previous pass
				bool prevPassUsedA = ((pass - 1) % 2 == 0);
				inputBinding.buffer = prevPassUsedA ? sortKeysA.Get() : sortKeysB.Get();
			}
			inputBinding.offset = 0;
			inputBinding.range = 0;
			inputBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(0, inputBinding);

			// Binding 1: Output histograms
			rhi::BufferBinding histogramBinding = {};
			histogramBinding.buffer = histograms.Get();
			histogramBinding.offset = pass * histogramSizePerPass;
			histogramBinding.range = histogramSizePerPass;
			histogramBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(1, histogramBinding);
		}
	}

	// Create descriptor sets for scan operations (one per radix pass)
	{
		uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			scanDescriptorSets[pass] = device->CreateDescriptorSet(scanSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Data buffer (histograms for this pass)
			rhi::BufferBinding dataBinding = {};
			dataBinding.buffer = histograms.Get();
			dataBinding.offset = pass * histogramSizePerPass;
			dataBinding.range = histogramSizePerPass;
			dataBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scanDescriptorSets[pass]->BindBuffer(0, dataBinding);

			// Binding 1: Block sums buffer
			rhi::BufferBinding blockSumsBinding = {};
			blockSumsBinding.buffer = blockSums.Get();
			blockSumsBinding.offset = 0;
			blockSumsBinding.range = 0;
			blockSumsBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scanDescriptorSets[pass]->BindBuffer(1, blockSumsBinding);
		}
	}

	// Create descriptor set for scanning block sums (used in Pass 3 of scan)
	{
		scanBlockSumsDescriptorSet = device->CreateDescriptorSet(scanSetLayout.Get(), rhi::QueueType::COMPUTE);

		// Bind BlockSums as both input and output for this pass
		rhi::BufferBinding blockSumDataBinding = {};
		blockSumDataBinding.buffer = blockSums.Get();
		blockSumDataBinding.offset = 0;
		blockSumDataBinding.range = 0;
		blockSumDataBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
		scanBlockSumsDescriptorSet->BindBuffer(0, blockSumDataBinding);
		scanBlockSumsDescriptorSet->BindBuffer(1, blockSumDataBinding);
	}

	// Create descriptor sets for scatter operations (one per pass)
	{
		uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			bool useA = (pass % 2 == 0);

			scatterPairsDescriptorSets[pass] = device->CreateDescriptorSet(scatterPairsSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input depth keys
			rhi::BufferBinding keysInBinding = {};
			if (pass == 0)
			{
				keysInBinding.buffer = splatDepths.Get();
			}
			else
			{
				// Read from the buffer the previous pass wrote to
				keysInBinding.buffer = useA ? sortKeysB.Get() : sortKeysA.Get();
			}
			keysInBinding.offset = 0;
			keysInBinding.range = 0;
			keysInBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(0, keysInBinding);

			// Binding 1: Input splat indices
			rhi::BufferBinding valuesInBinding = {};
			if (pass == 0)
			{
				valuesInBinding.buffer = splatIndicesOriginal.Get();
			}
			else
			{
				// Read from the buffer the previous pass wrote to
				valuesInBinding.buffer = useA ? sortIndicesB.Get() : sortIndicesA.Get();
			}
			valuesInBinding.offset = 0;
			valuesInBinding.range = 0;
			valuesInBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(1, valuesInBinding);

			// Binding 2: Output depth keys
			rhi::BufferBinding keysOutBinding = {};
			keysOutBinding.buffer = useA ? sortKeysA.Get() : sortKeysB.Get();
			keysOutBinding.offset = 0;
			keysOutBinding.range = 0;
			keysOutBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(2, keysOutBinding);

			// Binding 3: Output splat indices
			rhi::BufferBinding valuesOutBinding = {};
			valuesOutBinding.buffer = useA ? sortIndicesA.Get() : sortIndicesB.Get();
			valuesOutBinding.offset = 0;
			valuesOutBinding.range = 0;
			valuesOutBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(3, valuesOutBinding);

			// Binding 4: Scanned histograms
			rhi::BufferBinding histPairsBinding = {};
			histPairsBinding.buffer = histograms.Get();
			histPairsBinding.offset = pass * histogramSizePerPass;
			histPairsBinding.range = histogramSizePerPass;
			histPairsBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(4, histPairsBinding);
		}
	}
}

void GpuSplatSorter::Sort(rhi::IRHICommandList* cmdList, const Scene& scene, const app::Camera& camera)
{
	if (!isInitialized)
	{
		return;
	}

	// Upload camera data
	math::mat4 viewMatrix = camera.GetViewMatrix();
	device->UpdateBuffer(cameraUBO.Get(), &viewMatrix, sizeof(math::mat4), 0);

	// Update depth calc descriptor set
	const Scene::GpuData& gpuData = scene.GetGpuData();

	// Binding 0: Positions buffer
	rhi::BufferBinding positionsBinding = {};
	positionsBinding.buffer = gpuData.positions.Get();
	positionsBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
	depthCalcDescriptorSet->BindBuffer(0, positionsBinding);

	RecordDepthCalculation(cmdList, scene, camera);

	RecordRadixSort(cmdList);
}

void GpuSplatSorter::RecordDepthCalculation(rhi::IRHICommandList* cmdList, const Scene& scene, const app::Camera& camera)
{
	if (!depthCalcPipeline)
	{
		return;
	}

	cmdList->SetPipeline(depthCalcPipeline.Get());
	cmdList->BindDescriptorSet(0, depthCalcDescriptorSet.Get());

	uint32_t numElements = totalSplatCount;
	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0, {reinterpret_cast<const std::byte*>(&numElements), sizeof(uint32_t)});

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups);

	// Barrier to ensure depth calculation completes before sorting
	rhi::BufferTransition transition = {};
	transition.buffer = splatDepths.Get();
	transition.before = rhi::ResourceState::ShaderReadWrite;
	transition.after = rhi::ResourceState::ShaderReadWrite;

	cmdList->Barrier(
		rhi::PipelineScope::Compute,
		rhi::PipelineScope::Compute,
		{&transition, 1},
		{},
		{}
	);
}

void GpuSplatSorter::RecordRadixSort(rhi::IRHICommandList* cmdList)
{
	if (!histogramPipeline || !radixPrefixScanPipeline || !scatterPairsPipeline)
	{
		LOG_WARNING("Required pipelines not created");
		return;
	}

	const uint32_t maxWorkgroups = 256;
	uint32_t numWorkgroups = std::min(maxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);

	// Calculate how many blocks each workgroup needs to process
	uint32_t elementsPerWorkgroup = (totalSplatCount + numWorkgroups - 1) / numWorkgroups;
	uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

	// Calculate scan parameters
	constexpr uint32_t ELEMENTS_PER_THREAD = 4;
	uint32_t elementsPerScanWorkgroup = WorkgroupSize * ELEMENTS_PER_THREAD;
	uint32_t numScanElements = numWorkgroups * RadixSortBins;
	uint32_t numScanWorkgroups = (numScanElements + elementsPerScanWorkgroup - 1) / elementsPerScanWorkgroup;

	// Size of histogram data for one pass
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

	// 8 bits per radix pass
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8; // 0, 8, 16, 24

		PushConstants pushConstants = {};
		pushConstants.numElements = totalSplatCount;
		pushConstants.shift = shift;
		pushConstants.numWorkgroups = numWorkgroups;
		pushConstants.numBlocksPerWorkgroup = numBlocksPerWorkgroup;

		// --- Pass 1: HISTOGRAM ---
		cmdList->SetPipeline(histogramPipeline.Get());
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&pushConstants), sizeof(PushConstants)}
		);
		cmdList->Dispatch(numWorkgroups);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer = histograms.Get();
		histogramWriteTransition.before = rhi::ResourceState::ShaderReadWrite;
		histogramWriteTransition.after = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
			rhi::PipelineScope::Compute,
			rhi::PipelineScope::Compute,
			{&histogramWriteTransition, 1},
			{},
			{}
		);

		// --- Pass 2: SCAN BLOCKS ---
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());

		ScanPushConstants scanPushConstants = {};
		scanPushConstants.numElements = numScanElements;
		scanPushConstants.passType = 0;

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&scanPushConstants), sizeof(ScanPushConstants)}
		);
		cmdList->Dispatch(numScanWorkgroups);

		// Barrier: Ensure block scan and block sum writes are finished
		rhi::BufferTransition scanBlockTransitions[2];
		scanBlockTransitions[0].buffer = histograms.Get();
		scanBlockTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
		scanBlockTransitions[0].after = rhi::ResourceState::ShaderReadWrite;

		scanBlockTransitions[1].buffer = blockSums.Get();
		scanBlockTransitions[1].before = rhi::ResourceState::ShaderReadWrite;
		scanBlockTransitions[1].after = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
			rhi::PipelineScope::Compute,
			rhi::PipelineScope::Compute,
			{scanBlockTransitions, 2},
			{},
			{}
		);

		// --- Pass 3: SCAN BLOCK SUMS ---
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
		cmdList->BindDescriptorSet(0, scanBlockSumsDescriptorSet.Get());

		scanPushConstants.numElements = numScanWorkgroups;
		scanPushConstants.passType = 1; // Scan block sums

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&scanPushConstants), sizeof(ScanPushConstants)}
		);
		cmdList->Dispatch(1); // Single workgroup

		// Barrier: Ensure scan of block sums is finished
		rhi::BufferTransition blockSumScanTransition = {};
		blockSumScanTransition.buffer = blockSums.Get();
		blockSumScanTransition.before = rhi::ResourceState::ShaderReadWrite;
		blockSumScanTransition.after = rhi::ResourceState::GeneralRead;

		cmdList->Barrier(
			rhi::PipelineScope::Compute,
			rhi::PipelineScope::Compute,
			{&blockSumScanTransition, 1},
			{},
			{}
		);

		// --- Pass 4: ADD OFFSETS ---
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());

		scanPushConstants.numElements = numScanElements;
		scanPushConstants.passType = 2; // Add offsets

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&scanPushConstants), sizeof(ScanPushConstants)}
		);
		cmdList->Dispatch(numScanWorkgroups);

		// Barrier: Ensure final offsets are written
		rhi::BufferTransition addOffsetTransitions[2];
		addOffsetTransitions[0].buffer = histograms.Get();
		addOffsetTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
		addOffsetTransitions[0].after = rhi::ResourceState::GeneralRead;

		addOffsetTransitions[1].buffer = blockSums.Get();
		addOffsetTransitions[1].before = rhi::ResourceState::GeneralRead;
		addOffsetTransitions[1].after = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
			rhi::PipelineScope::Compute,
			rhi::PipelineScope::Compute,
			{addOffsetTransitions, 2},
			{},
			{}
		);

		// --- Pass 5: SCATTER ---
		cmdList->SetPipeline(scatterPairsPipeline.Get());
		cmdList->BindDescriptorSet(0, scatterPairsDescriptorSets[pass].Get());

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&pushConstants), sizeof(PushConstants)}
		);
		cmdList->Dispatch(numWorkgroups);

		// Barrier after scatter to ensure write completion
		bool useA = (pass % 2 == 0);
		rhi::BufferTransition scatterTransitions[3];

		scatterTransitions[0].buffer = useA ? sortKeysA.Get() : sortKeysB.Get();
		scatterTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
		scatterTransitions[0].after = rhi::ResourceState::GeneralRead;

		scatterTransitions[1].buffer = useA ? sortIndicesA.Get() : sortIndicesB.Get();
		scatterTransitions[1].before = rhi::ResourceState::ShaderReadWrite;
		scatterTransitions[1].after = rhi::ResourceState::GeneralRead;

		// Transition histogram back to read-write for next pass
		scatterTransitions[2].buffer = histograms.Get();
		scatterTransitions[2].before = rhi::ResourceState::GeneralRead;
		scatterTransitions[2].after = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
			rhi::PipelineScope::Compute,
			rhi::PipelineScope::Compute,
			{scatterTransitions, 3},
			{},
			{}
		);
	}
}

rhi::BufferHandle GpuSplatSorter::GetSortedIndices() const
{
	// Since we use ping-pong buffers, the final result depends on the number of passes
	bool lastPassUsedA = ((RadixPasses - 1) % 2 == 0);
	return lastPassUsedA ? sortIndicesA : sortIndicesB;
}

void GpuSplatSorter::PrepareVerification(rhi::IRHICommandList* cmdList)
{
	if (!isInitialized)
	{
		LOG_WARNING("Cannot verify sorting - sorter not initialized");
		return;
	}

	// Create readback buffers for verification
	rhi::BufferDesc readbackDesc = {};
	readbackDesc.size = totalSplatCount * sizeof(uint32_t);
	readbackDesc.usage = rhi::BufferUsage::TRANSFER_DST;
	readbackDesc.resourceUsage = rhi::ResourceUsage::Readback;
	readbackDesc.hints.persistently_mapped = true;
	readbackDesc.hints.prefer_device_local = false;

	// Create buffers for depths, sorted keys, and sorted indices
	verificationDepths = device->CreateBuffer(readbackDesc);
	verificationSortedKeys = device->CreateBuffer(readbackDesc);
	verificationSortedIndices = device->CreateBuffer(readbackDesc);

	// Copy depth keys (original unsorted)
	rhi::BufferCopy copyRegion = {};
	copyRegion.size = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(splatDepths.Get(), verificationDepths.Get(), {&copyRegion, 1});

	// Copy sorted keys and indices from final buffers
	// Pass 0 (even) writes to A, Pass 1 (odd) writes to B, Pass 2 (even) writes to A, Pass 3 (odd) writes to B
	// So with 4 passes, the last pass (pass 3, which is odd) writes to B
	bool lastPassUsedA = ((RadixPasses - 1) % 2 == 0);
	rhi::BufferHandle finalKeys = lastPassUsedA ? sortKeysA : sortKeysB;
	rhi::BufferHandle finalIndices = lastPassUsedA ? sortIndicesA : sortIndicesB;

	cmdList->CopyBuffer(finalKeys.Get(), verificationSortedKeys.Get(), {&copyRegion, 1});
	cmdList->CopyBuffer(finalIndices.Get(), verificationSortedIndices.Get(), {&copyRegion, 1});

	// Copy histogram data for verification
	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);
	uint32_t totalHistogramSize = RadixPasses * histogramSizePerPass;

	readbackDesc.size = totalHistogramSize;
	verificationHistogram = device->CreateBuffer(readbackDesc);

	rhi::BufferCopy histogramCopy = {};
	histogramCopy.size = totalHistogramSize;
	cmdList->CopyBuffer(histograms.Get(), verificationHistogram.Get(), {&histogramCopy, 1});

	// Transition all verification buffers
	rhi::BufferTransition transitions[4];
	transitions[0].buffer = verificationDepths.Get();
	transitions[0].before = rhi::ResourceState::CopyDestination;
	transitions[0].after = rhi::ResourceState::GeneralRead;

	transitions[1].buffer = verificationSortedKeys.Get();
	transitions[1].before = rhi::ResourceState::CopyDestination;
	transitions[1].after = rhi::ResourceState::GeneralRead;

	transitions[2].buffer = verificationSortedIndices.Get();
	transitions[2].before = rhi::ResourceState::CopyDestination;
	transitions[2].after = rhi::ResourceState::GeneralRead;

	transitions[3].buffer = verificationHistogram.Get();
	transitions[3].before = rhi::ResourceState::CopyDestination;
	transitions[3].after = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
		rhi::PipelineScope::Copy,
		rhi::PipelineScope::All,
		{transitions, 4},
		{},
		{}
	);

	LOG_INFO("Verification prepared - depths, sorted results, and histogram will be checked after GPU work completes");
}

bool GpuSplatSorter::CheckVerificationResults()
{
	if (!verificationDepths || !verificationSortedKeys || !verificationSortedIndices)
	{
		LOG_WARNING("No verification data available - call PrepareVerification first");
		return false;
	}

	// Map all verification buffers
	uint32_t* depthKeys = static_cast<uint32_t*>(verificationDepths->Map());
	uint32_t* sortedKeys = static_cast<uint32_t*>(verificationSortedKeys->Map());
	uint32_t* sortedIndices = static_cast<uint32_t*>(verificationSortedIndices->Map());

	if (!depthKeys || !sortedKeys || !sortedIndices)
	{
		LOG_ERROR("Failed to map verification buffers");
		if (depthKeys) verificationDepths->Unmap();
		if (sortedKeys) verificationSortedKeys->Unmap();
		if (sortedIndices) verificationSortedIndices->Unmap();
		verificationDepths = nullptr;
		verificationSortedKeys = nullptr;
		verificationSortedIndices = nullptr;
		return false;
	}

	LOG_INFO("=== Radix Sort Verification Results ===");
	LOG_INFO("Total splats: {}", totalSplatCount);

	// CPU-side implementation of FloatToSortableUint (same as shader)
	auto floatToSortableUint = [](float val) -> uint32_t {
		uint32_t u = *reinterpret_cast<uint32_t*>(&val);
		uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
		return 0xFFFFFFFFu - (u ^ mask);
	};

	// First verify depth calculation is correct
	bool depthsCorrect = true;
	LOG_INFO("\n1. Verifying depth calculation:");

	// Calculate starting Z position based on total splat count
	// For 1000 splats: Z from -495 to 504
	// For 10000 splats: Z from -4995 to 5004
	float startZ = -(totalSplatCount / 2.0f - 5.0f);

	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		float worldZ = startZ + i * 1.0f;
		float viewZ = worldZ - 5.0f;
		float expectedDepth = -viewZ;
		uint32_t expectedKey = floatToSortableUint(expectedDepth);
		uint32_t gpuKey = depthKeys[i];

		if (gpuKey != expectedKey)
		{
			depthsCorrect = false;
			if (i < 5 || i >= totalSplatCount - 5)  // Only log first and last few errors
			{
				LOG_ERROR("  Splat[{}]: GPU Key={:#010x}, Expected Key={:#010x}, Depth={:.2f} ✗",
					i, gpuKey, expectedKey, expectedDepth);
			}
		}
	}

	if (depthsCorrect)
	{
		LOG_INFO("  All {} depth keys calculated correctly ✓", totalSplatCount);
	}

	// Verify sorting order
	LOG_INFO("\n2. Verifying radix sort order:");
	bool sortOrderCorrect = true;
	uint32_t outOfOrderCount = 0;

	LOG_INFO("  First 10 sorted keys:");
	for (uint32_t i = 0; i < std::min<uint32_t>(10, totalSplatCount); ++i)
	{
		uint32_t splatIdx = sortedIndices[i];
		float worldZ = startZ + splatIdx * 1.0f;
		float viewZ = worldZ - 5.0f;
		float depth = -viewZ;
		LOG_INFO("    [{}]: key={:#010x}, splatIdx={}, worldZ={:.1f}, depth={:.1f}",
			i, sortedKeys[i], splatIdx, worldZ, depth);
	}

	// Check if sorted keys are in ascending order (near to far)
	for (uint32_t i = 1; i < totalSplatCount; ++i)
	{
		if (sortedKeys[i-1] > sortedKeys[i])
		{
			sortOrderCorrect = false;
			outOfOrderCount++;
			if (outOfOrderCount <= 10)
			{
				LOG_ERROR("  Out of order at position {}: key[{}]={:#010x} > key[{}]={:#010x}",
					i, i-1, sortedKeys[i-1], i, sortedKeys[i]);
			}
		}
	}

	if (sortOrderCorrect)
	{
		LOG_INFO("  All {} keys are correctly sorted in ascending order ✓", totalSplatCount);
	}
	else
	{
		LOG_ERROR("  Found {} out-of-order pairs in sorted keys", outOfOrderCount);
	}

	// Verify indices correspond to correct splats
	LOG_INFO("\n3. Verifying sorted indices:");
	bool indicesCorrect = true;
	uint32_t incorrectIndices = 0;

	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		uint32_t splatIdx = sortedIndices[i];
		if (splatIdx >= totalSplatCount)
		{
			indicesCorrect = false;
			incorrectIndices++;
			LOG_ERROR("  Invalid index at position {}: {} (>= {})", i, splatIdx, totalSplatCount);
			continue;
		}

		// Verify the sorted key matches the depth key of the referenced splat
		if (sortedKeys[i] != depthKeys[splatIdx])
		{
			indicesCorrect = false;
			incorrectIndices++;
			if (incorrectIndices <= 10)
			{
				LOG_ERROR("  Mismatch at position {}: sortedKey={:#010x}, but depthKeys[{}]={:#010x}",
					i, sortedKeys[i], splatIdx, depthKeys[splatIdx]);
			}
		}
	}

	if (indicesCorrect)
	{
		LOG_INFO("  All {} indices correctly map to their corresponding splats ✓", totalSplatCount);
	}
	else
	{
		LOG_ERROR("  Found {} incorrect index mappings", incorrectIndices);
	}

	LOG_INFO("\n4. Sample of sorted results:");
	LOG_INFO("  First 5 splats (nearest - smallest keys):");
	for (uint32_t i = 0; i < std::min<uint32_t>(5, totalSplatCount); ++i)
	{
		uint32_t splatIdx = sortedIndices[i];
		float worldZ = startZ + splatIdx * 1.0f;
		float viewZ = worldZ - 5.0f;
		float depth = -viewZ;
		LOG_INFO("    [{}]: splatIdx={}, worldZ={:.1f}, depth={:.1f}, key={:#010x}",
			i, splatIdx, worldZ, depth, sortedKeys[i]);
	}

	LOG_INFO("  Last 5 splats (farthest - largest keys):");
	uint32_t start = totalSplatCount > 5 ? totalSplatCount - 5 : 0;
	for (uint32_t i = start; i < totalSplatCount; ++i)
	{
		uint32_t splatIdx = sortedIndices[i];
		float worldZ = startZ + splatIdx * 1.0f;
		float viewZ = worldZ - 5.0f;
		float depth = -viewZ;
		LOG_INFO("    [{}]: splatIdx={}, worldZ={:.1f}, depth={:.1f}, key={:#010x}",
			i, splatIdx, worldZ, depth, sortedKeys[i]);
	}

	// Verify expected sorting for test data
	LOG_INFO("\n5. Verifying expected sort order for test data:");
	bool expectedOrderCorrect = true;

	// For our test data, splats are positioned dynamically based on count
	// Camera is at z=5, so:
	// - Splat 0 has largest positive depth (farthest from camera) → small key after float-to-uint
	// - Splat[totalSplatCount-1] has largest negative depth (nearest to camera) → large key after float-to-uint
	// After radix sort (ascending order), we expect:
	// - Splat 0 should be first (smallest key, farthest)
	// - Splat[totalSplatCount-1] should be last (largest key, nearest)
	LOG_INFO("  Checking if all {} splats are sorted correctly...", totalSplatCount);

	// Verify the complete sorting order
	uint32_t incorrectPositions = 0;
	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		// For ascending key order, we expect indices to go from 0 to totalSplatCount-1
		uint32_t expectedIdx = i;
		if (sortedIndices[i] != expectedIdx)
		{
			incorrectPositions++;
			if (incorrectPositions <= 10)
			{
				LOG_ERROR("    Position {}: expected splatIdx={}, got splatIdx={}",
					i, expectedIdx, sortedIndices[i]);
			}
		}
	}

	if (incorrectPositions == 0)
	{
		LOG_INFO("  All {} splats are in the expected order (0→{}) ✓", totalSplatCount, totalSplatCount-1);
		LOG_INFO("  This represents far-to-near ordering (positive to negative depths)");
		expectedOrderCorrect = true;
	}
	else
	{
		LOG_ERROR("  Found {} splats in incorrect positions", incorrectPositions);
		expectedOrderCorrect = false;
	}

	verificationDepths->Unmap();
	verificationSortedKeys->Unmap();
	verificationSortedIndices->Unmap();

	bool histogramCorrect = true;
	if (verificationHistogram)
	{
		LOG_INFO("");
		LOG_INFO("=== Histogram Verification ===");
		LOG_INFO("Skipping histogram verification for 5-pass version (histogram buffer is modified by scan operations)");

		// Skip detailed histogram verification since the 5-pass implementation modifies
		// the histogram buffer in-place during the scan operations, converting it from
		// counts to prefix sums. The sorting itself has been verified to work correctly.
		histogramCorrect = true;

		if (false)
		{
		// Use the same calculation as in RecordRadixSort
		const uint32_t maxWorkgroups = 20;
		uint32_t numWorkgroups = std::min(maxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
		uint32_t* histogramData = static_cast<uint32_t*>(verificationHistogram->Map());

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

				// Count elements in each bin based on depth keys
				for (uint32_t i = 0; i < totalSplatCount; ++i)
				{
					float worldZ = startZ + i * 1.0f;
					float viewZ = worldZ - 5.0f;
					float depth = -viewZ;
					uint32_t depthKey = floatToSortableUint(depth);

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

				// Histogram data layout: [pass0_wg0 | pass0_wg1 | ... | pass1_wg0 | pass1_wg1 | ...]
				uint32_t passOffset = pass * numWorkgroups * RadixSortBins;
				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
					{
						uint32_t idx = passOffset + wg * RadixSortBins + bin;
						combinedGpuHistogram[bin] += histogramData[idx];
					}
				}

				LOG_INFO("Non-zero histogram bins:");
				uint32_t nonZeroBins = 0;
				uint32_t totalNonZeroBins = 0;
				bool passCorrect = true;

				// Count total non-zero bins first
				for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
				{
					if (combinedGpuHistogram[bin] > 0 || expectedHistogram[bin] > 0)
						totalNonZeroBins++;
				}

				LOG_INFO("  Total non-zero bins: {}", totalNonZeroBins);

				for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
				{
					if (combinedGpuHistogram[bin] > 0 || expectedHistogram[bin] > 0)
					{
						bool binCorrect = (combinedGpuHistogram[bin] == expectedHistogram[bin]);
						if (!binCorrect)
						{
							passCorrect = false;
							histogramCorrect = false;
						}

						// Show all non-zero bins for pass 0 to debug
						if (pass == 0 || nonZeroBins < 5 || (combinedGpuHistogram[bin] > 0 && bin >= RadixSortBins - 5))
						{
							LOG_INFO("  Bin[{:3}]: GPU={:4}, Expected={:4} {}",
								bin, combinedGpuHistogram[bin], expectedHistogram[bin],
								binCorrect ? "✓" : "✗ INCORRECT");
						}
						else if (nonZeroBins == 5 && pass != 0)
						{
							LOG_INFO("  ... (omitting middle bins) ...");
						}
						nonZeroBins++;
					}
				}

				// Verify total count for this pass
				uint32_t totalGpuCount = 0;
				for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
				{
					totalGpuCount += combinedGpuHistogram[bin];
				}

				LOG_INFO("Total elements counted in pass {}: GPU={}, Expected={} {}",
					pass + 1, totalGpuCount, totalSplatCount,
					(totalGpuCount == totalSplatCount) ? "✓" : "✗ INCORRECT");

				if (totalGpuCount != totalSplatCount)
				{
					passCorrect = false;
					histogramCorrect = false;
				}

				if (passCorrect)
				{
					LOG_INFO("Pass {} verification: PASSED ✓", pass + 1);
				}
				else
				{
					LOG_ERROR("Pass {} verification: FAILED ✗", pass + 1);
				}
			}

			verificationHistogram->Unmap();
		}
		else
		{
			LOG_ERROR("Failed to map histogram verification buffer");
			histogramCorrect = false;
		}
		}

		verificationHistogram = nullptr;
	}

	verificationDepths = nullptr;
	verificationSortedKeys = nullptr;
	verificationSortedIndices = nullptr;

	LOG_INFO("");
	bool allTestsPassed = depthsCorrect && sortOrderCorrect && indicesCorrect &&
	                      expectedOrderCorrect && histogramCorrect;

	if (allTestsPassed)
	{
		LOG_INFO("=== ✓ VERIFICATION PASSED: All tests successful ===");
	}
	else
	{
		LOG_ERROR("=== ✗ VERIFICATION FAILED: Some tests failed ===");
		if (!depthsCorrect) LOG_ERROR("  - Depth calculation failed");
		if (!sortOrderCorrect) LOG_ERROR("  - Sort order failed");
		if (!indicesCorrect) LOG_ERROR("  - Index mapping failed");
		if (!expectedOrderCorrect) LOG_ERROR("  - Expected order failed");
		if (!histogramCorrect) LOG_ERROR("  - Histogram verification failed");
	}

	return allTestsPassed;
}

}        // namespace msplat::engine