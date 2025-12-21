#pragma once

#include <msplat/app/camera.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/vfs.h>
#include <msplat/engine/scene/scene.h>
#include <rhi/rhi.h>

namespace msplat::engine
{

class GpuSplatSorter
{
  public:
	enum class SortMethod
	{
		Prescan,              // Uses pre-scanned histograms (5 dispatches per pass)
		IntegratedScan        // Integrated prefix sum in scatter (2 dispatches per pass)
	};

	enum class ShaderVariant
	{
		Portable,                // Works on all GPUs (Blelloch scan, direct atomics)
		SubgroupOptimized        // Faster but requires reliable subgroup support (Fail on Qualcomm Adreno)
	};

	GpuSplatSorter(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs = nullptr);
	~GpuSplatSorter() = default;

	GpuSplatSorter(const GpuSplatSorter &)                = delete;
	GpuSplatSorter &operator=(const GpuSplatSorter &)     = delete;
	GpuSplatSorter(GpuSplatSorter &&) noexcept            = default;
	GpuSplatSorter &operator=(GpuSplatSorter &&) noexcept = default;

	void Initialize(uint32_t totalSplatCount);
	void Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);

	rhi::BufferHandle GetSortedIndices() const;

	// Method switching
	void SetSortMethod(SortMethod method)
	{
		sortMethod = method;
	}
	SortMethod GetSortMethod() const
	{
		return sortMethod;
	}
	const char *GetSortMethodName() const
	{
		return sortMethod == SortMethod::Prescan ? "Prescan" : "Integrated";
	}

	// Shader variant switching
	void SetShaderVariant(ShaderVariant variant)
	{
		shaderVariant = variant;
	}
	ShaderVariant GetShaderVariant() const
	{
		return shaderVariant;
	}
	const char *GetShaderVariantName() const
	{
		return shaderVariant == ShaderVariant::Portable ? "Portable" : "SubgroupOptimized";
	}

	// Verification methods
	// Phase 1: Prepare verification (copies data to readback buffers)
	void PrepareVerification(rhi::IRHICommandList *cmdList);
	// Phase 2: Check results after GPU work completes
	bool CheckVerificationResults(const container::vector<math::vec3> *testPositions = nullptr);
	// Simple verification: Only checks if final sorted keys are in ascending order
	bool VerifySortOrder();

  private:
	void CreateInitialIndicesBuffer(uint32_t totalSplatCount);
	void CreateComputePipelines();
	void CreateDescriptorSets();
	void RecordDepthCalculation(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);
	void RecordRadixSortPrescan(rhi::IRHICommandList *cmdList);
	void RecordRadixSortIntegrated(rhi::IRHICommandList *cmdList);

	static constexpr uint32_t WorkgroupSize    = 256;
	static constexpr uint32_t MaxWorkgroups    = 256;
	static constexpr uint32_t RadixSortBins    = 256;
	static constexpr uint32_t ElementPerThread = 4;
	static constexpr uint32_t RadixPasses      = 4;
	static constexpr uint32_t SubgroupSize     = 32;

	struct PushConstants
	{
		uint32_t numElements;
		uint32_t shift;
		uint32_t numWorkgroups;
		uint32_t numBlocksPerWorkgroup;
	};

	struct ScanPushConstants
	{
		uint32_t numElements;
		uint32_t passType;        // 0 = scan blocks, 1 = scan block sums, 2 = add offsets
	};

	rhi::IRHIDevice                        *device;
	container::shared_ptr<vfs::IFileSystem> vfs;
	uint32_t                                totalSplatCount;
	bool                                    isInitialized;

	rhi::BufferHandle splatDepths;
	rhi::BufferHandle splatIndicesOriginal;

	rhi::BufferHandle verificationDepths;
	rhi::BufferHandle verificationSortedKeys;
	rhi::BufferHandle verificationSortedIndices;
	rhi::BufferHandle sortKeysA;
	rhi::BufferHandle sortKeysB;
	rhi::BufferHandle sortIndicesA;
	rhi::BufferHandle sortIndicesB;
	rhi::BufferHandle histograms;
	rhi::BufferHandle blockSums;
	rhi::BufferHandle verificationHistogram;
	rhi::BufferHandle cameraUBO;

	rhi::PipelineHandle depthCalcPipeline;

	// Portable shader pipelines
	rhi::PipelineHandle histogramPipeline;
	rhi::PipelineHandle radixPrefixScanPipeline;

	// Subgroup-optimized shader pipelines
	rhi::PipelineHandle histogramSubgroupPipeline;
	rhi::PipelineHandle radixPrefixScanSubgroupPipeline;

	rhi::PipelineHandle scatterPairsPipeline;               // Default scatter with integrated scan
	rhi::PipelineHandle scatterPairsPrescanPipeline;        // Scatter with prescan method

	rhi::DescriptorSetLayoutHandle depthCalcSetLayout;
	rhi::DescriptorSetLayoutHandle histogramSetLayout;
	rhi::DescriptorSetLayoutHandle scanSetLayout;
	rhi::DescriptorSetLayoutHandle scatterPairsSetLayout;                  // For prescan method
	rhi::DescriptorSetLayoutHandle scatterPairsIntegratedSetLayout;        // For integrated scan method

	rhi::DescriptorSetHandle depthCalcDescriptorSet;
	rhi::DescriptorSetHandle histogramDescriptorSets[4];
	rhi::DescriptorSetHandle scanDescriptorSets[4];
	rhi::DescriptorSetHandle scanBlockSumsDescriptorSet;
	rhi::DescriptorSetHandle scatterPairsPrescanDescriptorSets[4];           // Prescan method descriptor sets
	rhi::DescriptorSetHandle scatterPairsIntegratedDescriptorSets[4];        // Integrated scan descriptor sets

	SortMethod    sortMethod    = SortMethod::IntegratedScan;
	ShaderVariant shaderVariant = ShaderVariant::Portable;

	// Store last view matrix for verification
	math::mat4 lastViewMatrix = math::mat4(1.0f);
};

}        // namespace msplat::engine