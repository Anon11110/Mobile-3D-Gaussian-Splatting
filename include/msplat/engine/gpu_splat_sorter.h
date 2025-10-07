#pragma once

#include <msplat/app/camera.h>
#include <msplat/core/containers/vector.h>
#include <msplat/engine/scene.h>
#include <rhi/rhi.h>

namespace msplat::engine
{

class GpuSplatSorter
{
  public:
	GpuSplatSorter(rhi::IRHIDevice *device);
	~GpuSplatSorter() = default;

	void Initialize(uint32_t totalSplatCount);
	void Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);

	rhi::BufferHandle GetSortedIndices() const;

	// Verification methods
	// Phase 1: Prepare verification (copies data to readback buffers)
	void PrepareVerification(rhi::IRHICommandList *cmdList);
	// Phase 2: Check results after GPU work completes
	bool CheckVerificationResults();

  private:
	void CreateInitialIndicesBuffer(uint32_t totalSplatCount);
	void CreateComputePipelines();
	void CreateDescriptorSets();
	void RecordDepthCalculation(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);
	void RecordRadixSort(rhi::IRHICommandList *cmdList);

	static constexpr uint32_t WorkgroupSize = 256;
	static constexpr uint32_t RadixSortBins = 256;
	static constexpr uint32_t RadixPasses   = 4;
	static constexpr uint32_t SubgroupSize  = 32;

	struct PushConstants
	{
		uint32_t numElements;
		uint32_t shift;
		uint32_t numWorkgroups;
		uint32_t numBlocksPerWorkgroup;
	};

	rhi::IRHIDevice *device;
	uint32_t         totalSplatCount;
	bool             isInitialized;

	rhi::BufferHandle splatDepths;
	rhi::BufferHandle splatIndicesOriginal;

	rhi::BufferHandle verificationDepths;
	rhi::BufferHandle sortKeysA;
	rhi::BufferHandle sortKeysB;
	rhi::BufferHandle sortIndicesA;
	rhi::BufferHandle sortIndicesB;
	rhi::BufferHandle histograms;
	rhi::BufferHandle verificationHistogram;
	rhi::BufferHandle cameraUBO;

	rhi::PipelineHandle depthCalcPipeline;
	rhi::PipelineHandle histogramPipeline;

	rhi::DescriptorSetLayoutHandle depthCalcSetLayout;
	rhi::DescriptorSetLayoutHandle histogramSetLayout;

	rhi::DescriptorSetHandle depthCalcDescriptorSet;
	rhi::DescriptorSetHandle histogramDescriptorSets[4];
};

}        // namespace msplat::engine