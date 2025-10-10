#include <msplat/engine/gpu_splat_sorter.h>
#include <msplat/engine/shader_factory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <cstring>

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

	// Create buffers for sorting
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
			rhi::DescriptorType::STORAGE_BUFFER_DYNAMIC,
			1,
			rhi::ShaderStageFlags::COMPUTE
		});

		histogramSetLayout = device->CreateDescriptorSetLayout(layoutDesc);
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

	// Create descriptor set for histogram
	{
		histogramDescriptorSet = device->CreateDescriptorSet(histogramSetLayout.Get(), rhi::QueueType::COMPUTE);

		// Binding 0: Input elements (depth keys)
		rhi::BufferBinding inputBinding = {};
		inputBinding.buffer = splatDepths.Get();
		inputBinding.offset = 0;
		inputBinding.range = 0;
		inputBinding.type = rhi::DescriptorType::STORAGE_BUFFER;
		histogramDescriptorSet->BindBuffer(0, inputBinding);

		// Binding 1: Output histograms
		// Range per binding
		uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
		uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

		rhi::BufferBinding histogramBinding = {};
		histogramBinding.buffer = histograms.Get();
		histogramBinding.offset = 0;
		histogramBinding.range = histogramSizePerPass;
		histogramBinding.type = rhi::DescriptorType::STORAGE_BUFFER_DYNAMIC;
		histogramDescriptorSet->BindBuffer(1, histogramBinding);
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

	// Dispatch compute shader
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
	if (!histogramPipeline)
	{
		LOG_WARNING("Histogram pipeline not created");
		return;
	}

	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	uint32_t numBlocksPerWorkgroup = 1; // For simplicity, each workgroup processes one block

	// If we have more elements than can fit in one workgroup
	if (totalSplatCount > WorkgroupSize * numWorkgroups)
	{
		numBlocksPerWorkgroup = (totalSplatCount + (WorkgroupSize * numWorkgroups) - 1) / (WorkgroupSize * numWorkgroups);
	}

	cmdList->SetPipeline(histogramPipeline.Get());

	// Size of histogram data for one pass
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);

	// Compute histograms for all 4 passes (8 bits each)
	for (uint32_t pass = 0; pass < RadixPasses; ++pass)
	{
		uint32_t shift = pass * 8; // 0, 8, 16, 24

		PushConstants pushConstants = {};
		pushConstants.numElements = totalSplatCount;
		pushConstants.shift = shift;
		pushConstants.numWorkgroups = numWorkgroups;
		pushConstants.numBlocksPerWorkgroup = numBlocksPerWorkgroup;

		// Dynamic offset for this pass
		uint32_t dynamicOffset = pass * histogramSizePerPass;
		cmdList->BindDescriptorSet(0, histogramDescriptorSet.Get(), {&dynamicOffset, 1});

		cmdList->PushConstants(
			rhi::ShaderStageFlags::COMPUTE,
			0,
			{reinterpret_cast<const std::byte*>(&pushConstants), sizeof(PushConstants)}
		);
		cmdList->Dispatch(numWorkgroups);

		rhi::BufferTransition transition = {};
		transition.buffer = histograms.Get();
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

	// TODO: Implement prefix sum and scatter for actual sorting
}

rhi::BufferHandle GpuSplatSorter::GetSortedIndices() const
{
	// For now, return the original unsorted indices
	// Will be updated when radix sort is implemented
	return splatIndicesOriginal;
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

	verificationDepths = device->CreateBuffer(readbackDesc);

	// Copy depth and histogram data to readback buffer for verification
	rhi::BufferCopy copyRegion = {};
	copyRegion.size = totalSplatCount * sizeof(uint32_t);
	cmdList->CopyBuffer(splatDepths.Get(), verificationDepths.Get(), {&copyRegion, 1});

	// Copy histogram data for verification
	uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
	uint32_t histogramSizePerPass = numWorkgroups * RadixSortBins * sizeof(uint32_t);
	uint32_t totalHistogramSize = RadixPasses * histogramSizePerPass;

	readbackDesc.size = totalHistogramSize;
	verificationHistogram = device->CreateBuffer(readbackDesc);

	rhi::BufferCopy histogramCopy = {};
	histogramCopy.size = totalHistogramSize;
	cmdList->CopyBuffer(histograms.Get(), verificationHistogram.Get(), {&histogramCopy, 1});

	rhi::BufferTransition transitions[2];
	transitions[0].buffer = verificationDepths.Get();
	transitions[0].before = rhi::ResourceState::CopyDestination;
	transitions[0].after = rhi::ResourceState::GeneralRead;

	transitions[1].buffer = verificationHistogram.Get();
	transitions[1].before = rhi::ResourceState::CopyDestination;
	transitions[1].after = rhi::ResourceState::GeneralRead;

	cmdList->Barrier(
		rhi::PipelineScope::Copy,
		rhi::PipelineScope::All,
		{transitions, 2},
		{},
		{}
	);

	LOG_INFO("Verification prepared - depths and histogram will be checked after GPU work completes");
}

bool GpuSplatSorter::CheckVerificationResults()
{
	if (!verificationDepths)
	{
		LOG_WARNING("No verification data available - call PrepareVerification first");
		return false;
	}

	// return true;

	// Map the verification buffer to read depth values
	uint32_t* depthKeys = static_cast<uint32_t*>(verificationDepths->Map());
	if (!depthKeys)
	{
		LOG_ERROR("Failed to map verification buffer");
		verificationDepths = nullptr;
		return false;
	}

	LOG_INFO("=== Depth Calculation Verification Results ===");
	LOG_INFO("Total splats: {}", totalSplatCount);

	// CPU-side implementation of FloatToSortableUint (same as shader)
	auto floatToSortableUint = [](float val) -> uint32_t {
		uint32_t u = *reinterpret_cast<uint32_t*>(&val);
		uint32_t mask = (u & 0x80000000u) ? 0xFFFFFFFFu : 0x80000000u;
		return 0xFFFFFFFFu - (u ^ mask);
	};

	// For our test data with 1000 splats:
	// Camera is at (0, 0, 5)
	// Splats are at Z = -495 to 504 (1000 splats with spacing of 1.0)
	// View space Z = world_Z - camera_Z = world_Z - 5
	// So view space Z = -500 to 499
	// Depth = -viewZ = 500 to -499

	bool allCorrect = true;
	uint32_t incorrectCount = 0;

	LOG_INFO("Verifying all {} depth keys:", totalSplatCount);

	for (uint32_t i = 0; i < totalSplatCount; ++i)
	{
		// Expected depth for test data
		float worldZ = -495.0f + i * 1.0f;  // Test splats from Z=-495 to Z=504
		float viewZ = worldZ - 5.0f;        // Camera at Z=5
		float expectedDepth = -viewZ;        // depth = -viewPos.z

		uint32_t expectedKey = floatToSortableUint(expectedDepth);
		uint32_t gpuKey = depthKeys[i];

		bool isCorrect = (gpuKey == expectedKey);
		if (!isCorrect)
		{
			allCorrect = false;
			incorrectCount++;

			LOG_INFO("  Splat[{}]: GPU Key={:#010x}, Expected Key={:#010x}, Depth={:.2f} ✗ INCORRECT",
				i, gpuKey, expectedKey, expectedDepth);
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
			float worldZ = -495.0f + i * 1.0f;
			float viewZ = worldZ - 5.0f;
			float expectedDepth = -viewZ;
			uint32_t expectedKey = floatToSortableUint(expectedDepth);
			uint32_t gpuKey = depthKeys[i];

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

	bool histogramCorrect = true;
	if (verificationHistogram)
	{
		LOG_INFO("");
		LOG_INFO("=== Histogram Verification ===");

		uint32_t numWorkgroups = (totalSplatCount + WorkgroupSize - 1) / WorkgroupSize;
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
					float worldZ = -495.0f + i * 1.0f;
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
				bool passCorrect = true;
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

						// Show first 5 and last 5 non-zero bins
						if (nonZeroBins < 5 || (combinedGpuHistogram[bin] > 0 && bin >= RadixSortBins - 5))
						{
							LOG_INFO("  Bin[{:3}]: GPU={:4}, Expected={:4} {}",
								bin, combinedGpuHistogram[bin], expectedHistogram[bin],
								binCorrect ? "✓" : "✗ INCORRECT");
						}
						else if (nonZeroBins == 5)
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

		verificationHistogram = nullptr;
	}

	verificationDepths = nullptr;

	LOG_INFO("");
	if (allCorrect && histogramCorrect)
	{
		LOG_INFO("=== VERIFICATION PASSED: Depth calculation and histogram are correct ===");
	}
	else
	{
		if (!allCorrect)
			LOG_ERROR("=== VERIFICATION FAILED: Depth calculation has errors ===");
		if (!histogramCorrect)
			LOG_ERROR("=== VERIFICATION FAILED: Histogram has errors ===");
	}

	return allCorrect && histogramCorrect;
}

}        // namespace msplat::engine