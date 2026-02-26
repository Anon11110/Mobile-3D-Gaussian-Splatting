#pragma once

#include <functional>
#include <msplat/app/camera.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/vfs.h>
#include <msplat/engine/scene/scene.h>
#include <msplat/engine/sorting/gpu_splat_sorter.h>
#include <rhi/rhi.h>

struct FrameUBO;

namespace msplat::engine
{

// Configuration for tile-based rendering
struct TileConfig
{
	uint32_t tileSize     = 16;        // Tile size in pixels (typically 16)
	uint32_t screenWidth  = 0;         // Screen width in pixels
	uint32_t screenHeight = 0;         // Screen height in pixels
	uint32_t tilesX       = 0;         // Number of tiles in X dimension
	uint32_t tilesY       = 0;         // Number of tiles in Y dimension
	uint32_t totalTiles   = 0;         // Total number of tiles

	void Update(uint32_t width, uint32_t height)
	{
		screenWidth  = width;
		screenHeight = height;
		tilesX       = (width + tileSize - 1) / tileSize;
		tilesY       = (height + tileSize - 1) / tileSize;
		totalTiles   = tilesX * tilesY;
	}
};

// Tile-based compute rasterization pipeline for 3D Gaussian Splatting:
//
// 1. Preprocess: Project 3D Gaussians to 2D, evaluate SH, compute tile coverage
// 2. Sort: Sort by (TileID << 16 | Depth16) using existing radix sort
// 3. Identify Ranges: Find start/end indices for each tile
// 4. Rasterize: Per-tile workgroups blend splats and write to storage image
class ComputeSplatRasterizer
{
  public:
	ComputeSplatRasterizer(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs = nullptr);
	~ComputeSplatRasterizer() = default;

	ComputeSplatRasterizer(const ComputeSplatRasterizer &)                = delete;
	ComputeSplatRasterizer &operator=(const ComputeSplatRasterizer &)     = delete;
	ComputeSplatRasterizer(ComputeSplatRasterizer &&) noexcept            = default;
	ComputeSplatRasterizer &operator=(ComputeSplatRasterizer &&) noexcept = default;

	void Initialize(uint32_t screenWidth, uint32_t screenHeight, uint32_t maxSplatCount);
	void Resize(uint32_t screenWidth, uint32_t screenHeight);

	void Render(rhi::IRHICommandList *cmdList, const Scene &scene, const FrameUBO &frameUBO);

	rhi::IRHITexture *GetOutputImage() const
	{
		return m_outputImage.Get();
	}

	// Get the global counter value (number of tile instances emitted)
	uint32_t ReadTileInstanceCount();

	const TileConfig &GetTileConfig() const
	{
		return m_tileConfig;
	}

	struct TransmittanceStats
	{
		uint64_t totalEvaluations;         // Total possible splat-pixel evaluations
		uint64_t actualEvaluations;        // Actual evaluations performed before early exit
		uint32_t earlyExitPixels;          // Pixels that triggered transmittance early exit
		float    savingsPercent;           // Percentage of evaluations saved
	};

	struct Statistics
	{
		uint32_t activeSplats;              // Number of splats after culling
		uint32_t totalTileInstances;        // Total tile-splat pairs emitted
		float    avgTilesPerSplat;          // Average tiles touched per visible splat
		double   preprocessMs;              // Preprocess pass duration
		double   sortMs;                    // Sort pass duration
		double   rangesMs;                  // Identify ranges pass duration
		double   rasterMs;                  // Rasterization pass duration

		TransmittanceStats transmittance;        // Transmittance culling savings
	};

	Statistics GetStatistics() const
	{
		return m_stats;
	}

	struct BufferInfo
	{
		rhi::BufferHandle geometryBuffer;          // Gaussian2D per splat
		rhi::BufferHandle tileKeys;                // Packed tile keys (unsorted, sorter's input)
		rhi::BufferHandle tileValues;              // Splat indices (unsorted, sorter's input)
		rhi::BufferHandle sortedTileKeys;          // Sorted tile pairs (uint2: key + index, sorter's sortPairsB)
		rhi::BufferHandle sortedTileValues;        // Sorted splat indices (sorter's output)
		rhi::BufferHandle tileRanges;              // Per-tile start/end
		rhi::BufferHandle globalCounter;           // Atomic counter
		rhi::BufferHandle counterReadback;         // CPU-readable counter copy
	};

	BufferInfo GetBufferInfo() const;

	bool IsInitialized() const
	{
		return m_isInitialized;
	}

#ifdef ENABLE_SORT_VERIFICATION
	bool VerifySortOrder();
#endif

	void SetCPUSortDebug(bool enabled)
	{
		m_cpuSortDebugEnabled = enabled;
	}
	bool IsCPUSortDebugEnabled() const
	{
		return m_cpuSortDebugEnabled;
	}

	// Sort method and shader variant switching
	void SetSortMethod(int method);
	int  GetSortMethod() const;
	void SetShaderVariant(int variant);
	int  GetShaderVariant() const;

	// Transmittance culling analysis mode
	// mode: 0=off, 1=stats only, 2=stats+heatmap
	void SetTransmittanceStatsMode(uint32_t mode)
	{
		m_transmittanceStatsMode = mode;
	}
	uint32_t GetTransmittanceStatsMode() const
	{
		return m_transmittanceStatsMode;
	}
	TransmittanceStats ReadTransmittanceStats();

	// Profiling callbacks for GPU timestamp queries
	using TimestampCallback = std::function<void(rhi::IRHICommandList *, bool begin)>;
	void SetProfilingCallbacks(
	    TimestampCallback preprocessCallback,
	    TimestampCallback sortCallback,
	    TimestampCallback rangesCallback,
	    TimestampCallback rasterCallback)
	{
		m_onPreprocessTimestamp = preprocessCallback;
		m_onSortTimestamp       = sortCallback;
		m_onRangesTimestamp     = rangesCallback;
		m_onRasterTimestamp     = rasterCallback;
	}

  private:
	void CreateBuffers(uint32_t maxSplatCount);
	void CreateComputePipelines();
	void RecreatePreprocessPipeline(uint32_t shDegree);
	void CreateDescriptorSets();
	void RebindSceneDescriptors(const Scene &scene);
	void RecordPreprocess(rhi::IRHICommandList *cmdList, const Scene &scene, const FrameUBO &frameUBO);
	void RecordSort(rhi::IRHICommandList *cmdList);
	void RecordIdentifyRanges(rhi::IRHICommandList *cmdList);
	void RecordRasterize(rhi::IRHICommandList *cmdList);
	void CreateOutputImage();
	void RebindRasterDescriptors();
	void PerformCPUSort();
	void ResizeTileBuffers(uint32_t newMaxTileInstances);

	struct PreprocessPC
	{
		uint32_t numSplats;
		uint32_t tilesX;
		uint32_t tilesY;
		uint32_t tileSize;
		float    nearPlane;
		float    farPlane;
		uint32_t maxTileInstances;
		uint32_t _pad0;
	};

	struct RangesPC
	{
		uint32_t numTileInstances;
		uint32_t numTiles;
	};

	struct RasterPC
	{
		uint32_t tilesX;
		uint32_t tilesY;
		uint32_t screenWidth;
		uint32_t screenHeight;
		uint32_t transmittanceStatsMode;
		uint32_t _rasterPad0;
	};

	static constexpr uint32_t WorkgroupSize        = 256;
	static constexpr uint32_t InitialTilesPerSplat = 16;        // 4x4 tile coverage max
	static constexpr float    DefaultNearPlane     = 0.1f;
	static constexpr float    DefaultFarPlane      = 100.0f;

	rhi::IRHIDevice                        *m_device;
	container::shared_ptr<vfs::IFileSystem> m_vfs;
	TileConfig                              m_tileConfig;
	bool                                    m_isInitialized = false;

	uint32_t m_maxSplatCount    = 0;
	uint32_t m_maxTileInstances = 0;

	// Buffers
	rhi::BufferHandle m_geometryBuffer;          // Gaussian2D[] - preprocessed 2D data
	rhi::BufferHandle m_tileRanges;              // int2[] - per-tile start/end
	rhi::BufferHandle m_globalCounter;           // uint32_t - atomic counter
	rhi::BufferHandle m_counterReadback;         // CPU-readable copy of counter
	rhi::BufferHandle m_frameUBO;                // FrameUBO for preprocess shader
	rhi::BufferHandle m_sortedTileValues;        // Output buffer for sorted tile values

	// Tile key sorter
	// Preprocess writes directly to sorter's splatDepths (keys) and splatIndicesOriginal (values)
	container::unique_ptr<GpuSplatSorter> m_tileSorter;

	// Verification readback buffer
	rhi::BufferHandle m_sortVerifyReadback;

	// Output storage image
	rhi::TextureHandle m_outputImage;

	// Shaders retained for pipeline recreation
	rhi::ShaderHandle m_preprocessShader;

	// SH degree for specialization constants
	uint32_t m_currentShDegree = 3;

	// Pipelines
	rhi::PipelineHandle m_preprocessPipeline;
	rhi::PipelineHandle m_rangesPipeline;
	rhi::PipelineHandle m_rasterPipeline;

	// Descriptor set layouts
	rhi::DescriptorSetLayoutHandle m_preprocessLayout;
	rhi::DescriptorSetLayoutHandle m_rangesLayout;
	rhi::DescriptorSetLayoutHandle m_rasterLayout;

	// Descriptor sets
	rhi::DescriptorSetHandle m_preprocessDescriptorSet;
	rhi::DescriptorSetHandle m_rangesDescriptorSet;
	rhi::DescriptorSetHandle m_rasterDescriptorSet;

	// Track bound scene for descriptor rebinding
	const Scene *m_lastBoundScene = nullptr;

	Statistics m_stats = {};

	// Near/far planes for depth encoding (can be configured)
	float m_nearPlane = DefaultNearPlane;
	float m_farPlane  = DefaultFarPlane;

	// Dynamic tile buffer resize tracking
	bool m_hasRenderedOneFrame = false;

	// Transmittance stats analysis
	uint32_t          m_transmittanceStatsMode = 0;        // 0=off, 1=stats only, 2=stats+heatmap
	rhi::BufferHandle m_transmittanceStatsBuffer;          // 3 x uint32: totalEvals, actualEvals, earlyExitPixels
	rhi::BufferHandle m_transmittanceStatsReadback;        // CPU-readable copy

	// Debug CPU sort
	bool              m_cpuSortDebugEnabled = false;
	rhi::BufferHandle m_dbgKeysReadback;
	rhi::BufferHandle m_dbgValsReadback;
	rhi::BufferHandle m_dbgKeysStaging;
	rhi::BufferHandle m_dbgValsStaging;

	// Profiling callbacks
	TimestampCallback m_onPreprocessTimestamp;
	TimestampCallback m_onSortTimestamp;
	TimestampCallback m_onRangesTimestamp;
	TimestampCallback m_onRasterTimestamp;
};

}        // namespace msplat::engine
