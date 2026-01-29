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

	/// Initialize the sorter
	/// @param totalSplatCount Number of splats to sort
	/// @param outputBuffer Buffer where final sorted indices are written.
	///        Must be large enough for totalSplatCount * sizeof(uint32_t).
	void Initialize(uint32_t totalSplatCount, rhi::BufferHandle outputBuffer);
	void Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);

	rhi::BufferHandle GetSortedIndices() const;

	/// Get the primary output buffer (sortIndicesB)
	rhi::BufferHandle GetPrimaryOutputBuffer() const;

	/// Get the alternate output buffer for pipelined rendering (sortIndicesB_Alt)
	rhi::BufferHandle GetAlternateOutputBuffer() const;

	/// Change the output buffer for pipelined rendering
	/// @param outputBuffer New buffer to write sorted indices to (must be primary or alternate buffer)
	void SetOutputBuffer(rhi::BufferHandle outputBuffer);

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

	// Buffer information for memory tracking
	struct BufferInfo
	{
		rhi::BufferHandle splatDepths;
		rhi::BufferHandle splatIndicesOriginal;
		rhi::BufferHandle sortKeysA;
		rhi::BufferHandle sortKeysB;
		rhi::BufferHandle sortIndicesA;
		rhi::BufferHandle sortIndicesB;
		rhi::BufferHandle sortIndicesB_Alt;
		rhi::BufferHandle histograms;
		rhi::BufferHandle blockSums;
		rhi::BufferHandle cameraUBO;
	};

	BufferInfo GetBufferInfo() const;

	// GPU Timing
	double GetLastSortTimeMs() const
	{
		return lastSortTimeMs;
	}

	// Blocking version that call after GPU work completes to read timing results
	void ReadTimingResults();

	// Non-blocking version that doesn't wait for GPU completion
	// Returns true if timing data was successfully read, false if not yet available
	bool ReadTimingResultsNonBlocking();

	// Synchronize timing frame index with external profiling system
	// Call this before Sort() to ensure timing data corresponds to correct frame
	void SetTimingFrameIndex(uint32_t frameIndex)
	{
		timingFrameIndex = frameIndex;
	}

	// Set the frame latency for timing queries (default: 3)
	// Should match the profiling frame latency used by the app
	void SetTimingLatency(uint32_t latency)
	{
		timingFrameLatency = latency;
	}

	bool IsTimingEnabled() const
	{
		return timestampQueryPool != nullptr;
	}

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
	rhi::BufferHandle sortIndicesB;            // Primary output buffer (index 0)
	rhi::BufferHandle sortIndicesB_Alt;        // Secondary output buffer (index 1) for pipelined async compute
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
	// Double-buffered scatter descriptor sets for pipelined async compute
	// Index 0: for primary output buffer, Index 1: for secondary output buffer
	rhi::DescriptorSetHandle scatterPairsPrescanDescriptorSets[2][4];           // [bufferIdx][pass]
	rhi::DescriptorSetHandle scatterPairsIntegratedDescriptorSets[2][4];        // [bufferIdx][pass]
	uint32_t                 activeOutputBufferIndex = 0;

	SortMethod    sortMethod    = SortMethod::IntegratedScan;
	ShaderVariant shaderVariant = ShaderVariant::Portable;

	// Store last view matrix for verification
	math::mat4 lastViewMatrix = math::mat4(1.0f);

	// Cache for descriptor set binding, skip updates if unchanged to avoid in-use errors
	rhi::IRHIBuffer *lastBoundPositionsBuffer = nullptr;
	rhi::IRHIBuffer *lastBoundOutputBuffer    = nullptr;        // Track output buffer for SetOutputBuffer

	// GPU Timing infrastructure
	uint32_t             timingFrameLatency = 3;        // N-frame latency for reading results
	rhi::QueryPoolHandle timestampQueryPool;
	double               timestampPeriod  = 1.0;        // nanoseconds per tick
	uint32_t             timingFrameIndex = 0;          // Rolling frame index
	double               lastSortTimeMs   = 0.0;        // Last measured sort time in ms
};

}        // namespace msplat::engine