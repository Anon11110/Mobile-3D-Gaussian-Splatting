#include <algorithm>
#include <cstring>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <msplat/engine/gpu_splat_sorter.h>
#include <msplat/engine/shader_factory.h>

namespace msplat::engine
{

GpuSplatSorter::GpuSplatSorter(rhi::IRHIDevice *device) :
    device(device), totalSplatCount(0), isInitialized(false)
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
	bufferDesc.usage         = rhi::BufferUsage::STORAGE | rhi::BufferUsage::TRANSFER_DST | rhi::BufferUsage::TRANSFER_SRC;
	bufferDesc.resourceUsage = rhi::ResourceUsage::Static;

	// Depth keys buffer
	bufferDesc.size = totalSplatCount * sizeof(uint32_t);
	splatDepths     = device->CreateBuffer(bufferDesc);

	// Original indices buffer
	bufferDesc.size      = totalSplatCount * sizeof(uint32_t);
	splatIndicesOriginal = device->CreateBuffer(bufferDesc);

	// Sort buffers (ping-pong)
	sortKeysA    = device->CreateBuffer(bufferDesc);
	sortKeysB    = device->CreateBuffer(bufferDesc);
	sortIndicesA = device->CreateBuffer(bufferDesc);
	sortIndicesB = device->CreateBuffer(bufferDesc);

	// Histogram buffer for radix sort
	// Each workgroup generates a histogram with 256 bins for each of the 4 passes
	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
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
	ShaderFactory shaderFactory(device);

	rhi::ShaderHandle depthCalcShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/depth_calc.comp.spv",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle histogramShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_histogram.comp.spv",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle radixPrefixScanShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_prefix_scan.comp.spv",
	    rhi::ShaderStage::COMPUTE);

	rhi::ShaderHandle scatterPairsShader = shaderFactory.getOrCreateShader(
	    "shaders/compiled/radix_scatter_pairs.comp.spv",
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

		depthCalcSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Histogram layout
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input elements (depth keys)
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

	// Scatter pairs layout (depth keys and splat indices)
	{
		rhi::DescriptorSetLayoutDesc layoutDesc = {};

		// Binding 0: Input depth keys
		layoutDesc.bindings.push_back({0,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 1: Input splat indices
		layoutDesc.bindings.push_back({1,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 2: Output depth keys
		layoutDesc.bindings.push_back({2,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 3: Output splat indices
		layoutDesc.bindings.push_back({3,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		// Binding 4: Scanned histograms
		layoutDesc.bindings.push_back({4,
		                               rhi::DescriptorType::STORAGE_BUFFER,
		                               1,
		                               rhi::ShaderStageFlags::COMPUTE});

		scatterPairsSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
	}

	// Create compute pipelines
	// Depth calculation pipeline
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = depthCalcShader.Get();
		pipelineDesc.descriptorSetLayouts     = {depthCalcSetLayout.Get()};

		// Push constants for numElements
		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(uint32_t);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

		depthCalcPipeline = device->CreateComputePipeline(pipelineDesc);
	}

	// Histogram pipeline
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

	// Radix prefix scan pipeline for global prefix sum
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

	// Scatter pairs pipeline (depth keys and splat indices)
	{
		rhi::ComputePipelineDesc pipelineDesc = {};
		pipelineDesc.computeShader            = scatterPairsShader.Get();
		pipelineDesc.descriptorSetLayouts     = {scatterPairsSetLayout.Get()};

		rhi::PushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags             = rhi::ShaderStageFlags::COMPUTE;
		pushConstantRange.offset                 = 0;
		pushConstantRange.size                   = sizeof(PushConstants);
		pipelineDesc.pushConstantRanges          = {pushConstantRange};

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
		depthBinding.buffer             = splatDepths.Get();
		depthBinding.offset             = 0;
		depthBinding.range              = 0;        // 0 means whole buffer
		depthBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
		depthCalcDescriptorSet->BindBuffer(1, depthBinding);

		// Binding 2: Camera UBO
		rhi::BufferBinding cameraBinding = {};
		cameraBinding.buffer             = cameraUBO.Get();
		cameraBinding.offset             = 0;
		cameraBinding.range              = sizeof(math::mat4);
		cameraBinding.type               = rhi::DescriptorType::UNIFORM_BUFFER;
		depthCalcDescriptorSet->BindBuffer(2, cameraBinding);
	}

	// Create descriptor sets for histogram (one per pass)
	{
		uint32_t numWorkgroups        = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		for (uint32_t pass = 0; pass < RadixPasses; ++pass)
		{
			histogramDescriptorSets[pass] = device->CreateDescriptorSet(histogramSetLayout.Get(), rhi::QueueType::COMPUTE);

			// Binding 0: Input elements
			rhi::BufferBinding inputBinding = {};
			if (pass == 0)
			{
				inputBinding.buffer = splatDepths.Get();        // First pass uses original depth keys
			}
			else
			{
				// Subsequent passes read from the output of the previous pass
				bool prevPassUsedA  = ((pass - 1) % 2 == 0);
				inputBinding.buffer = prevPassUsedA ? sortKeysA.Get() : sortKeysB.Get();
			}
			inputBinding.offset = 0;
			inputBinding.range  = 0;
			inputBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(0, inputBinding);

			// Binding 1: Output histograms
			rhi::BufferBinding histogramBinding = {};
			histogramBinding.buffer             = histograms.Get();
			histogramBinding.offset             = pass * histogramSizePerPass;
			histogramBinding.range              = histogramSizePerPass;
			histogramBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			histogramDescriptorSets[pass]->BindBuffer(1, histogramBinding);

			//-----------------------------------------------------------------------

			// // Binding 0: Input elements (depth keys)
			// rhi::BufferBinding inputBinding = {};
			// inputBinding.buffer             = splatDepths.Get();
			// inputBinding.offset             = 0;
			// inputBinding.range              = 0;
			// inputBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			// histogramDescriptorSets[pass]->BindBuffer(0, inputBinding);

			// // Binding 1: Output histograms (with offset for this pass)
			// rhi::BufferBinding histogramBinding = {};
			// histogramBinding.buffer             = histograms.Get();
			// histogramBinding.offset             = pass * histogramSizePerPass;
			// histogramBinding.range              = histogramSizePerPass;
			// histogramBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			// histogramDescriptorSets[pass]->BindBuffer(1, histogramBinding);
		}
	}

	// Create descriptor sets for scan operations (one per radix pass)
	{
		uint32_t numWorkgroups        = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
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

	// Create descriptor sets for scatter operations (one per pass)
	{
		uint32_t numWorkgroups        = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
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
			keysInBinding.range  = 0;
			keysInBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
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
			valuesInBinding.range  = 0;
			valuesInBinding.type   = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(1, valuesInBinding);

			// Binding 2: Output depth keys
			rhi::BufferBinding keysOutBinding = {};
			keysOutBinding.buffer             = useA ? sortKeysA.Get() : sortKeysB.Get();
			keysOutBinding.offset             = 0;
			keysOutBinding.range              = 0;
			keysOutBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(2, keysOutBinding);

			// Binding 3: Output splat indices
			rhi::BufferBinding valuesOutBinding = {};
			valuesOutBinding.buffer             = useA ? sortIndicesA.Get() : sortIndicesB.Get();
			valuesOutBinding.offset             = 0;
			valuesOutBinding.range              = 0;
			valuesOutBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(3, valuesOutBinding);

			// Binding 4: Scanned histograms
			rhi::BufferBinding histPairsBinding = {};
			histPairsBinding.buffer             = histograms.Get();
			histPairsBinding.offset             = pass * histogramSizePerPass;
			histPairsBinding.range              = histogramSizePerPass;
			histPairsBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
			scatterPairsDescriptorSets[pass]->BindBuffer(4, histPairsBinding);
		}
	}
}

void GpuSplatSorter::Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera)
{
	if (!isInitialized)
	{
		return;
	}

	// Upload camera data
	math::mat4 viewMatrix = camera.GetViewMatrix();
	device->UpdateBuffer(cameraUBO.Get(), &viewMatrix, sizeof(math::mat4), 0);

	// Update depth calc descriptor set
	const Scene::GpuData &gpuData = scene.GetGpuData();

	// Binding 0: Splat Attributes buffer (interleaved AoS layout)
	rhi::BufferBinding attributesBinding = {};
	attributesBinding.buffer             = gpuData.splat_attributes.Get();
	attributesBinding.type               = rhi::DescriptorType::STORAGE_BUFFER;
	depthCalcDescriptorSet->BindBuffer(0, attributesBinding);

	RecordDepthCalculation(cmdList, scene, camera);

	RecordRadixSort(cmdList);
}

void GpuSplatSorter::RecordDepthCalculation(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera)
{
	if (!depthCalcPipeline)
	{
		return;
	}

	cmdList->SetPipeline(depthCalcPipeline.Get());
	cmdList->BindDescriptorSet(0, depthCalcDescriptorSet.Get());

	uint32_t numElements = totalSplatCount;
	cmdList->PushConstants(rhi::ShaderStageFlags::COMPUTE, 0, {reinterpret_cast<const std::byte *>(&numElements), sizeof(uint32_t)});

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	cmdList->Dispatch(numWorkgroups);

	// Barrier to ensure depth calculation completes before sorting
	rhi::BufferTransition transition = {};
	transition.buffer                = splatDepths.Get();
	transition.before                = rhi::ResourceState::ShaderReadWrite;
	transition.after                 = rhi::ResourceState::ShaderReadWrite;

	cmdList->Barrier(
	    rhi::PipelineScope::Compute,
	    rhi::PipelineScope::Compute,
	    {&transition, 1},
	    {},
	    {});
}

void GpuSplatSorter::RecordRadixSort(rhi::IRHICommandList *cmdList)
{
	if (!histogramPipeline || !radixPrefixScanPipeline || !scatterPairsPipeline)
	{
		LOG_WARNING("Required pipelines not created");
		return;
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
		cmdList->SetPipeline(histogramPipeline.Get());
		cmdList->BindDescriptorSet(0, histogramDescriptorSets[pass].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier: Ensure histogram writes are finished
		rhi::BufferTransition histogramWriteTransition = {};
		histogramWriteTransition.buffer                = histograms.Get();
		histogramWriteTransition.before                = rhi::ResourceState::ShaderReadWrite;
		histogramWriteTransition.after                 = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {&histogramWriteTransition, 1},
		    {},
		    {});

		// --- Pass 2: SCAN BLOCKS ---
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
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

		// --- Pass 3: SCAN BLOCK SUMS ---
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
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
		cmdList->SetPipeline(radixPrefixScanPipeline.Get());
		cmdList->BindDescriptorSet(0, scanDescriptorSets[pass].Get());

		scanPushConstants.numElements = numScanElements;
		scanPushConstants.passType    = 2;        // Add offsets

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&scanPushConstants), sizeof(ScanPushConstants)});
		cmdList->Dispatch(numScanWorkgroups);

		// Barrier: Ensure final offsets are written
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

		// --- Pass 5: SCATTER ---
		cmdList->SetPipeline(scatterPairsPipeline.Get());
		cmdList->BindDescriptorSet(0, scatterPairsDescriptorSets[pass].Get());

		cmdList->PushConstants(
		    rhi::ShaderStageFlags::COMPUTE,
		    0,
		    {reinterpret_cast<const std::byte *>(&pushConstants), sizeof(PushConstants)});
		cmdList->Dispatch(numWorkgroups);

		// Barrier after scatter to ensure write completion
		bool                  useA = (pass % 2 == 0);
		rhi::BufferTransition scatterTransitions[3];

		scatterTransitions[0].buffer = useA ? sortKeysA.Get() : sortKeysB.Get();
		scatterTransitions[0].before = rhi::ResourceState::ShaderReadWrite;
		scatterTransitions[0].after  = rhi::ResourceState::GeneralRead;

		scatterTransitions[1].buffer = useA ? sortIndicesA.Get() : sortIndicesB.Get();
		scatterTransitions[1].before = rhi::ResourceState::ShaderReadWrite;
		scatterTransitions[1].after  = rhi::ResourceState::GeneralRead;

		// Transition histogram back to read-write for next pass
		scatterTransitions[2].buffer = histograms.Get();
		scatterTransitions[2].before = rhi::ResourceState::GeneralRead;
		scatterTransitions[2].after  = rhi::ResourceState::ShaderReadWrite;

		cmdList->Barrier(
		    rhi::PipelineScope::Compute,
		    rhi::PipelineScope::Compute,
		    {scatterTransitions, 3},
		    {},
		    {});
	}
}

rhi::BufferHandle GpuSplatSorter::GetSortedIndices() const
{
	// Since we use ping-pong buffers, the final result depends on the number of passes
	bool lastPassUsedA = ((RadixPasses - 1) % 2 == 0);
	return lastPassUsedA ? sortIndicesA : sortIndicesB;
}

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

	// Copy depth keys (original unsorted)
	rhi::BufferCopy copyRegion = {};
	copyRegion.size            = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(splatDepths.Get(), verificationDepths.Get(), {&copyRegion, 1});

	// Copy sorted keys and indices from final buffers
	// Pass 0 (even) writes to A, Pass 1 (odd) writes to B, Pass 2 (even) writes to A, Pass 3 (odd) writes to B
	// So with 4 passes, the last pass (pass 3, which is odd) writes to B
	bool              lastPassUsedA = ((RadixPasses - 1) % 2 == 0);
	rhi::BufferHandle finalKeys     = lastPassUsedA ? sortKeysA : sortKeysB;
	rhi::BufferHandle finalIndices  = lastPassUsedA ? sortIndicesA : sortIndicesB;

	cmdList->CopyBuffer(finalKeys.Get(), verificationSortedKeys.Get(), {&copyRegion, 1});
	cmdList->CopyBuffer(finalIndices.Get(), verificationSortedIndices.Get(), {&copyRegion, 1});

	// Copy histogram data for verification
	uint32_t numWorkgroups        = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);
	uint32_t totalHistogramSize   = RadixPasses * histogramSizePerPass;

	readbackDesc.size     = totalHistogramSize;
	verificationHistogram = device->CreateBuffer(readbackDesc);

	rhi::BufferCopy histogramCopy = {};
	histogramCopy.size            = totalHistogramSize;
	cmdList->CopyBuffer(histograms.Get(), verificationHistogram.Get(), {&histogramCopy, 1});

	// Transition all verification buffers
	rhi::BufferTransition transitions[4];
	transitions[0].buffer = verificationDepths.Get();
	transitions[0].before = rhi::ResourceState::CopyDestination;
	transitions[0].after  = rhi::ResourceState::GeneralRead;

	transitions[1].buffer = verificationSortedKeys.Get();
	transitions[1].before = rhi::ResourceState::CopyDestination;
	transitions[1].after  = rhi::ResourceState::GeneralRead;

	transitions[2].buffer = verificationSortedIndices.Get();
	transitions[2].before = rhi::ResourceState::CopyDestination;
	transitions[2].after  = rhi::ResourceState::GeneralRead;

	transitions[3].buffer = verificationHistogram.Get();
	transitions[3].before = rhi::ResourceState::CopyDestination;
	transitions[3].after  = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
	    rhi::PipelineScope::Copy,
	    rhi::PipelineScope::All,
	    {transitions, 4},
	    {},
	    {});

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

	container::vector<uint32_t> cpuDepthKeys;
	cpuDepthKeys.resize(totalSplatCount);

	bool     allCorrect     = true;
	uint32_t incorrectCount = 0;

	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		// Expected depth for test data using actual test position
		float worldZ = (*testPositions)[i].z;

		float viewZ         = worldZ - 5.0f;        // Camera at Z=5
		float expectedDepth = -viewZ;               // depth = -viewPos.z

		uint32_t expectedKey = floatToSortableUint(expectedDepth);
		uint32_t gpuKey      = depthKeys[i];

		// Store CPU-calculated key for histogram verification
		cpuDepthKeys[i] = expectedKey;

		bool isCorrect = (gpuKey == expectedKey);
		if (!isCorrect)
		{
			allCorrect = false;
			incorrectCount++;

			if (incorrectCount <= 10)
			{
				LOG_INFO("  Splat[{}]: GPU Key={:#010x}, Expected Key={:#010x}, Depth={:.2f} ✗ INCORRECT",
				         i, gpuKey, expectedKey, expectedDepth);
			}
		}
	}

	if (allCorrect)
	{
		LOG_INFO("All {} depth keys are correct ✓", totalSplatCount);
	}
	else
	{
		LOG_ERROR("Found {} incorrect depth keys out of {}", incorrectCount, totalSplatCount);

		LOG_INFO("Sample of correct values for reference:");
		for (uint32_t i = 0; i < std::min<uint32_t>(5, totalSplatCount); ++i)
		{
			float    worldZ        = (*testPositions)[i].z;
			float    viewZ         = worldZ - 5.0f;
			float    expectedDepth = -viewZ;
			uint32_t expectedKey   = floatToSortableUint(expectedDepth);
			uint32_t gpuKey        = depthKeys[i];

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
		LOG_INFO("=== Scanned Histogram Verification (Prefix Sums) ===");
		LOG_INFO("Note: After scan passes, histograms contain exclusive prefix sums, not counts");

		uint32_t  numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
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
				uint32_t actualNumWorkgroups   = std::min(MaxWorkgroups, (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize);
				uint32_t elementsPerWorkgroup  = (totalSplatCount + actualNumWorkgroups - 1) / actualNumWorkgroups;
				uint32_t numBlocksPerWorkgroup = (elementsPerWorkgroup + WorkgroupSize - 1) / WorkgroupSize;

				// Debug: Track elements in specific bins for WG[0]
				uint32_t debugWg0Bin51Count = 0;
				uint32_t debugWg0Bin52Count = 0;
				uint32_t debugAllBin51Count = 0;

				// Each workgroup processes numBlocksPerWorkgroup blocks of WorkgroupSize elements
				for (uint32_t wg = 0; wg < actualNumWorkgroups; ++wg)
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
							allHistogramCounts[bin * actualNumWorkgroups + wg]++;

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
					LOG_INFO("    DEBUG: WG[0] Bin[51] CPU count: {}", allHistogramCounts[51 * actualNumWorkgroups + 0]);
					LOG_INFO("    DEBUG: WG[0] Bin[52] CPU count: {}", allHistogramCounts[52 * actualNumWorkgroups + 0]);
					LOG_INFO("    DEBUG: Total Bin[51] count across all WGs: {}", debugAllBin51Count);
					LOG_INFO("    DEBUG: Elements per workgroup: {}", elementsPerWorkgroup);
					LOG_INFO("    DEBUG: Actual num workgroups: {}", actualNumWorkgroups);
				}

				// Now perform exclusive prefix sum
				uint32_t runningSum = 0;
				for (uint32_t i = 0; i < numWorkgroups * RadixSortBins; ++i)
				{
					expectedPrefixSums[i] = runningSum;
					runningSum += allHistogramCounts[i];
				}

				// Debug: Print first few raw values from GPU histogram
				if (pass == 0 || pass == 1)
				{
					LOG_INFO("  Debug: First 10 raw GPU histogram values (bin-major order):");
					for (uint32_t i = 0; i < std::min<uint32_t>(10, numWorkgroups * RadixSortBins); ++i)
					{
						LOG_INFO("    histData[{}] = {} (expected={})", i,
						         histogramData[passOffset + i], expectedPrefixSums[i]);
					}
					LOG_INFO("  Debug: Values at key positions:");
					LOG_INFO("    histData[0] (bin0,wg0) = {}", histogramData[passOffset + 0]);
					LOG_INFO("    histData[40] (bin1,wg0) = {}", histogramData[passOffset + 40]);
					LOG_INFO("    histData[80] (bin2,wg0) = {}", histogramData[passOffset + 80]);

					if (pass == 1)
					{
						// Check WG[1] Bin[0] specifically
						uint32_t idx_wg1_bin0      = passOffset + 0 * numWorkgroups + 1;
						uint32_t expected_wg1_bin0 = expectedPrefixSums[0 * numWorkgroups + 1];
						LOG_INFO("  Debug: WG[1] Bin[0] - idx={}, GPU={}, Expected={}",
						         idx_wg1_bin0, histogramData[idx_wg1_bin0], expected_wg1_bin0);
					}
				}

				// Compare GPU scanned results with expected prefix sums
				bool     passCorrect   = true;
				uint32_t mismatchCount = 0;
				uint32_t nonZeroBins   = 0;

				for (uint32_t wg = 0; wg < numWorkgroups; ++wg)
				{
					for (uint32_t bin = 0; bin < RadixSortBins; ++bin)
					{
						// Bin-major indexing: bin * numWorkgroups + wg
						uint32_t idx           = passOffset + bin * numWorkgroups + wg;
						uint32_t globalIdx     = bin * numWorkgroups + wg;
						uint32_t gpuValue      = histogramData[idx];
						uint32_t expectedValue = expectedPrefixSums[globalIdx];

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
										LOG_INFO("    Previous: WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count was {})",
										         wg, prevBin, histogramData[prevIdx], expectedPrefixSums[prevGlobalIdx],
										         allHistogramCounts[prevGlobalIdx]);
									}

									// Next bin
									if (bin < RadixSortBins - 1)
									{
										uint32_t nextBin       = bin + 1;
										uint32_t nextIdx       = passOffset + nextBin * numWorkgroups + wg;
										uint32_t nextGlobalIdx = nextBin * numWorkgroups + wg;
										LOG_INFO("    Next: WG[{}] Bin[{:3}]: GPU={:6}, Expected={:6} (count was {})",
										         wg, nextBin, histogramData[nextIdx], expectedPrefixSums[nextGlobalIdx],
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
					LOG_INFO("Pass {} scanned histogram verification: PASSED ✓", pass + 1);
				}
				else
				{
					LOG_ERROR("Pass {} scanned histogram verification: FAILED ({} mismatches) ✗", pass + 1, mismatchCount);
				}

				if (pass == 3)
				{
					keysBeforePass4 = cpuSortedKeys;
				}

				// Perform CPU radix sort for this pass to prepare for next pass
				container::vector<uint32_t> counts;
				counts.resize(RadixSortBins);
				for (uint32_t i = 0; i < RadixSortBins; ++i)
				{
					counts[i] = 0;
				}

				for (uint32_t i = 0; i < totalSplatCount; ++i)
				{
					uint32_t bin = (cpuSortedKeys[i] >> shift) & (RadixSortBins - 1);
					counts[bin]++;
				}

				// Calculate exclusive prefix sums (starting positions)
				container::vector<uint32_t> offsets;
				offsets.resize(RadixSortBins);
				uint32_t sum = 0;
				for (uint32_t i = 0; i < RadixSortBins; ++i)
				{
					offsets[i] = sum;
					sum += counts[i];
				}

				// Scatter elements to their sorted positions
				for (uint32_t i = 0; i < totalSplatCount; ++i)
				{
					uint32_t key       = cpuSortedKeys[i];
					uint32_t idx       = cpuSortedIndices[i];
					uint32_t bin       = (key >> shift) & (RadixSortBins - 1);
					uint32_t outputPos = offsets[bin]++;

					tempKeys[outputPos]    = key;
					tempIndices[outputPos] = idx;
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

		uint32_t *sortedKeys    = static_cast<uint32_t *>(verificationSortedKeys->Map());
		uint32_t *sortedIndices = static_cast<uint32_t *>(verificationSortedIndices->Map());

		if (sortedKeys && sortedIndices)
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
					uint32_t splatIdx = sortedIndices[i];
					float    worldZ   = (*testPositions)[splatIdx].z;
					float    depth    = -(worldZ - 5.0f);
					LOG_INFO("    [{}]: key={:#010x}, splatIdx={}, worldZ={:.1f}, depth={:.1f}",
					         i, sortedKeys[i], splatIdx, worldZ, depth);
				}

				LOG_INFO("  Last 5 sorted splats (nearest, largest keys):");
				uint32_t start = totalSplatCount > 5 ? totalSplatCount - 5 : 0;
				for (uint32_t i = start; i < totalSplatCount; ++i)
				{
					uint32_t splatIdx = sortedIndices[i];
					float    worldZ   = (*testPositions)[splatIdx].z;
					float    depth    = -(worldZ - 5.0f);
					LOG_INFO("    [{}]: key={:#010x}, splatIdx={}, worldZ={:.1f}, depth={:.1f}",
					         i, sortedKeys[i], splatIdx, worldZ, depth);
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

}        // namespace msplat::engine