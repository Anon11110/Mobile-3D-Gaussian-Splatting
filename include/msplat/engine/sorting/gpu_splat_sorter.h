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

	void Initialize(uint32_t totalSplatCount, rhi::BufferHandle outputBuffer, uint32_t pipelineDepth = 2,
	                container::span<rhi::IRHIBuffer *const> indirectArgsBuffers = {});

	void Sort(rhi::IRHICommandList *cmdList, const Scene &scene, const app::Camera &camera);

	// Sort pre-written keys without depth calculation.
	void SortOnly(rhi::IRHICommandList *cmdList);

	// Sort using DispatchIndirect, indirectBufferIndex selects which K-buffered indirect args buffer to use
	void SortIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex);

	// Sort pre-written keys without depth calculation
	void SortOnlyIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex);

	// Composable sort building blocks for multi-pass sorting
	// Pack keys+values from splatDepths/splatIndicesOriginal
	void PackPairsIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex);
	// Radix sort pairs only (no pack/unpack). Make numPasses even for now so result ends in sortPairsB
	void SortPairsIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex, uint32_t numPasses = RadixPasses);
	// Unpack sorted indices from sortPairsB into output buffer
	void UnpackPairsIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex);

	rhi::BufferHandle GetSortPairsB() const
	{
		return sortPairsB;
	}

	rhi::BufferHandle GetSortedIndices() const;

	// Get an output buffer by index (0 = primary, 1..K-1 = alternates for pipelined async compute)
	rhi::BufferHandle GetOutputBuffer(uint32_t index) const;

	// Get total number of output buffers (pipeline depth)
	uint32_t GetOutputBufferCount() const;

	// Change the output buffer for pipelined rendering
	void SetOutputBuffer(rhi::BufferHandle outputBuffer);

	// Sort direction
	void SetSortAscending(bool ascending)
	{
		sortAscending = ascending;
	}
	bool IsSortAscending() const
	{
		return sortAscending;
	}

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

#ifdef ENABLE_SORT_VERIFICATION
	// Verification methods
	// Phase 1: Prepare verification (copies data to readback buffers)
	void PrepareVerification(rhi::IRHICommandList *cmdList);
	// Phase 2: Check results after GPU work completes
	bool CheckVerificationResults(const container::vector<math::vec3> *testPositions = nullptr);
	// Simple verification: Only checks if final sorted keys are in ascending order
	bool VerifySortOrder();
#endif

	// Buffer information for memory tracking
	struct BufferInfo
	{
		rhi::BufferHandle                    splatDepths;
		rhi::BufferHandle                    splatIndicesOriginal;
		rhi::BufferHandle                    sortPairsA;
		rhi::BufferHandle                    sortPairsB;
		container::vector<rhi::BufferHandle> outputBuffers;        // [0]=primary, [1..K-1]=alternates
		rhi::BufferHandle                    histograms;
		rhi::BufferHandle                    blockSums;
		rhi::BufferHandle                    cameraUBO;
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
	void RecordPackPairs(rhi::IRHICommandList *cmdList);
	void RecordUnpackIndices(rhi::IRHICommandList *cmdList);
	void RecordRadixSortPrescan(rhi::IRHICommandList *cmdList, bool inlineUnpack = false);
	void RecordRadixSortIntegrated(rhi::IRHICommandList *cmdList, bool inlineUnpack = false);
	void CreateIndirectComputePipelines();
	void CreateIndirectDescriptorSets();
	void RecordRadixSortPrescanIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex, bool inlineUnpack = true, uint32_t numPasses = RadixPasses);
	void RecordRadixSortIntegratedIndirect(rhi::IRHICommandList *cmdList, uint32_t indirectBufferIndex, bool inlineUnpack = true, uint32_t numPasses = RadixPasses);

	static constexpr uint32_t WorkgroupSize    = 256;
	static constexpr uint32_t MaxWorkgroups    = 256;
	static constexpr uint32_t RadixSortBins    = 256;
	static constexpr uint32_t ElementPerThread = 4;
	static constexpr uint32_t RadixPasses      = 4;
	static constexpr uint32_t SubgroupSize     = 32;

	struct DepthCalcPushConstants
	{
		uint32_t numElements;
		uint32_t sortAscending;        // 0 = far-to-near, 1 = near-to-far
	};

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

#ifdef ENABLE_SORT_VERIFICATION
	rhi::BufferHandle verificationDepths;
	rhi::BufferHandle verificationSortedKeys;
	rhi::BufferHandle verificationSortedIndices;
	rhi::BufferHandle verificationHistogram;
#endif

	rhi::BufferHandle                    sortPairsA;           // Ping-pong uint2 pair buffer A (key, index)
	rhi::BufferHandle                    sortPairsB;           // Ping-pong uint2 pair buffer B (key, index)
	container::vector<rhi::BufferHandle> outputBuffers;        // [0]=primary, [1..K-1]=alternates for pipelined async compute
	rhi::BufferHandle                    histograms;
	rhi::BufferHandle                    blockSums;
	rhi::BufferHandle                    cameraUBO;

	rhi::PipelineHandle depthCalcPipeline;
	rhi::PipelineHandle packPairsPipeline;
	rhi::PipelineHandle unpackIndicesPipeline;

	// Portable shader pipelines
	rhi::PipelineHandle histogramPipeline;
	rhi::PipelineHandle radixPrefixScanPipeline;

	// Subgroup-optimized shader pipelines
	rhi::PipelineHandle histogramSubgroupPipeline;
	rhi::PipelineHandle radixPrefixScanSubgroupPipeline;

	rhi::PipelineHandle scatterPairsPipeline;                // Default scatter with integrated scan
	rhi::PipelineHandle scatterPairsPrescanPipeline;         // Scatter with prescan method
	rhi::PipelineHandle scatterUnpackPipeline;               // Final-pass scatter: writes indices directly (integrated)
	rhi::PipelineHandle scatterUnpackPrescanPipeline;        // Final-pass scatter: writes indices directly (prescan)

	// Indirect dispatch pipeline variants
	rhi::PipelineHandle histogramIndirectPipeline;
	rhi::PipelineHandle histogramSubgroupIndirectPipeline;
	rhi::PipelineHandle radixPrefixScanIndirectPipeline;
	rhi::PipelineHandle radixPrefixScanSubgroupIndirectPipeline;
	rhi::PipelineHandle scatterPairsIndirectPipeline;
	rhi::PipelineHandle scatterPairsPrescanIndirectPipeline;
	rhi::PipelineHandle scatterUnpackIndirectPipeline;
	rhi::PipelineHandle scatterUnpackPrescanIndirectPipeline;

	rhi::DescriptorSetLayoutHandle depthCalcSetLayout;
	rhi::DescriptorSetLayoutHandle packPairsSetLayout;
	rhi::DescriptorSetLayoutHandle unpackIndicesSetLayout;
	rhi::DescriptorSetLayoutHandle histogramSetLayout;
	rhi::DescriptorSetLayoutHandle scanSetLayout;
	rhi::DescriptorSetLayoutHandle scatterPairsSetLayout;                  // For prescan method
	rhi::DescriptorSetLayoutHandle scatterPairsIntegratedSetLayout;        // For integrated scan method
	rhi::DescriptorSetLayoutHandle sortParamsSetLayout;                    // Set 1: sortParams for indirect dispatch

	rhi::DescriptorSetHandle                    depthCalcDescriptorSet;
	rhi::DescriptorSetHandle                    packPairsDescriptorSet;
	container::vector<rhi::DescriptorSetHandle> unpackIndicesDescriptorSets;        // [bufferIdx] for output buffers
	rhi::DescriptorSetHandle                    histogramDescriptorSets[4];
	rhi::DescriptorSetHandle                    scanDescriptorSets[4];
	rhi::DescriptorSetHandle                    scanBlockSumsDescriptorSet;
	rhi::DescriptorSetHandle                    scatterPairsPrescanDescriptorSets[4];           // [pass]
	rhi::DescriptorSetHandle                    scatterPairsIntegratedDescriptorSets[4];        // [pass]
	container::vector<rhi::DescriptorSetHandle> scatterUnpackPrescanDescriptorSets;             // [bufferIdx] final pass writes indices
	container::vector<rhi::DescriptorSetHandle> scatterUnpackIntegratedDescriptorSets;          // [bufferIdx] final pass writes indices
	uint32_t                                    activeOutputBufferIndex = 0;
	uint32_t                                    outputBufferCount       = 2;

	// Indirect dispatch descriptor sets
	container::vector<rhi::DescriptorSetHandle> sortParamsDescriptorSets;        // [indirectBufferIndex]
	container::vector<rhi::IRHIBuffer *>        indirectArgsBufferPtrs;

	SortMethod    sortMethod    = SortMethod::IntegratedScan;
	ShaderVariant shaderVariant = ShaderVariant::Portable;
	bool          sortAscending = false;        // false = far-to-near, true = near-to-far

#ifdef ENABLE_SORT_VERIFICATION
	bool       lastSortUsedInlineUnpack = false;                   // Track if last sort used inline unpack for verification
	math::mat4 lastViewMatrix           = math::mat4(1.0f);        // Store last view matrix for verification
#endif

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