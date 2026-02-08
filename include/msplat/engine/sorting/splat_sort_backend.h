#pragma once

#include <cstdint>
#include <msplat/app/camera.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/math/math.h>
#include <msplat/core/timer.h>
#include <msplat/core/vfs.h>
#include <msplat/engine/sorting/gpu_splat_sorter.h>
#include <rhi/rhi.h>

namespace msplat::engine
{

// Forward declarations
class Scene;

/// Performance metrics returned by backends
struct SortMetrics
{
	float sortDurationMs   = 0.0f;        // Time spent sorting
	float uploadDurationMs = 0.0f;        // Time spent uploading (CPU backend only)
	bool  sortComplete     = true;        // False if CPU sort still in progress
};

/// Abstract interface for splat sorting backends
class ISplatSortBackend
{
  public:
	virtual ~ISplatSortBackend() = default;

	/// Initialize the backend
	/// @param device RHI device for GPU operations
	/// @param scene Scene containing splat data (positions, etc.)
	/// @param sortedIndicesBuffer App-owned buffer where sorted indices are written
	/// @param totalSplatCount Number of splats to sort
	/// @param vfs Optional VFS for shader loading
	/// @return true if initialization succeeded
	virtual bool Initialize(
	    rhi::IRHIDevice                        *device,
	    Scene                                  *scene,
	    rhi::BufferHandle                       sortedIndicesBuffer,
	    uint32_t                                totalSplatCount,
	    container::shared_ptr<vfs::IFileSystem> vfs = nullptr) = 0;

	/// Update sorting based on current camera view
	/// CPU backend: Triggers async sort, uploads when complete
	/// GPU backend: Records and submits compute dispatches
	/// @param camera Camera providing view matrix and position
	virtual void Update(const app::Camera &camera) = 0;

	/// Update with external command list (single queue mode)
	/// Records sort commands directly into provided command list
	/// Caller manages Begin/End/Submit
	/// @param camera Camera providing view matrix and position
	/// @param cmdList Command list to record into (must be in recording state)
	virtual void Update(const app::Camera &camera, rhi::IRHICommandList *cmdList)
	{
		(void) camera;
		(void) cmdList;
	}

	/// Check if the most recent sort operation has completed
	/// CPU backend: May return false while async sort is in progress
	/// GPU backend: Always returns true (synchronous within frame)
	virtual bool IsSortComplete() const = 0;

	/// Get performance metrics from the last sort operation
	virtual SortMetrics GetMetrics() const = 0;

	/// Get human-readable backend name (e.g., "CPU", "GPU")
	virtual const char *GetName() const = 0;

	/// Trigger verification of sort correctness (optional)
	/// @return true if sort order is correct, false otherwise
	virtual bool VerifySort()
	{
		return true;
	}

	/// Set sorting method (GPU backend only: 0=Prescan, 1=IntegratedScan)
	virtual void SetSortMethod(int method)
	{
		(void) method;
	}

	/// Get current sorting method index
	virtual int GetSortMethod() const
	{
		return 0;
	}

	/// Get human-readable method name (e.g., "Prescan", "Integrated Scan")
	virtual const char *GetMethodName() const
	{
		return "Default";
	}

	/// Set shader variant (GPU backend only: 0=Portable, 1=SubgroupOptimized)
	virtual void SetShaderVariant(int variant)
	{
		(void) variant;
	}

	/// Get current shader variant index
	virtual int GetShaderVariant() const
	{
		return 0;
	}

	/// Get human-readable shader variant name
	virtual const char *GetShaderVariantName() const
	{
		return "Default";
	}

	/// Check if comprehensive verification is available
	virtual bool HasComprehensiveVerification() const
	{
		return false;
	}

	/// Run comprehensive verification (GPU backend only)
	virtual bool RunComprehensiveVerification()
	{
		return true;
	}

	/// Set test positions for comprehensive verification (GPU backend only)
	virtual void SetTestPositions(const container::vector<math::vec3> *positions)
	{
		(void) positions;
	}

	/// Get semaphore that signals when compute sort is complete (GPU backend only)
	/// Used for cross-queue synchronization between compute and graphics
	/// @return Semaphore to wait on before using sorted indices, or nullptr if not applicable
	virtual rhi::IRHISemaphore *GetComputeSemaphore() const
	{
		return nullptr;
	}

	/// Get the sorted indices buffer for queue ownership transfer
	virtual rhi::IRHIBuffer *GetSortedIndicesBuffer() const
	{
		return nullptr;
	}

	/// Enable or disable async compute (GPU backend only)
	/// When disabled, sorting uses graphics queue to avoid queue transfer overhead
	virtual void SetAsyncCompute(bool enabled)
	{
		(void) enabled;
	}

	virtual bool IsAsyncComputeEnabled() const
	{
		return false;
	}

	/// Synchronize timing frame index with app's profiling system (GPU backend only)
	/// Call this before Update() to ensure sort timing corresponds to the same frame as render timing
	virtual void SetTimingFrameIndex(uint32_t frameIndex)
	{
		(void) frameIndex;
	}

	/// Set the frame latency for timing queries (GPU backend only)
	/// Should match the profiling frame latency used by the app
	virtual void SetTimingLatency(uint32_t latency)
	{
		(void) latency;
	}

	/// Set sort direction
	/// @param ascending true for near-to-far, false for far-to-near
	virtual void SetSortAscending(bool ascending)
	{
		(void) ascending;
	}

	virtual bool IsSortAscending() const
	{
		return false;
	}
};

/// GPU backend implementation using GpuSplatSorter
class GpuSplatSortBackend : public ISplatSortBackend
{
  public:
	GpuSplatSortBackend();
	~GpuSplatSortBackend() override;

	bool Initialize(
	    rhi::IRHIDevice                        *device,
	    Scene                                  *scene,
	    rhi::BufferHandle                       sortedIndicesBuffer,
	    uint32_t                                totalSplatCount,
	    container::shared_ptr<vfs::IFileSystem> vfs = nullptr) override;

	void Update(const app::Camera &camera) override;
	void Update(const app::Camera &camera, rhi::IRHICommandList *cmdList) override;

	bool        IsSortComplete() const override;
	SortMetrics GetMetrics() const override;
	const char *GetName() const override;

	bool        VerifySort() override;
	void        SetSortMethod(int method) override;
	int         GetSortMethod() const override;
	const char *GetMethodName() const override;
	void        SetShaderVariant(int variant) override;
	int         GetShaderVariant() const override;
	const char *GetShaderVariantName() const override;
	bool        HasComprehensiveVerification() const override;
	bool        RunComprehensiveVerification() override;
	void        SetTestPositions(const container::vector<math::vec3> *positions) override;

	void RequestVerification();

	// Prepare verification within an external command list
	void PrepareVerification(rhi::IRHICommandList *cmdList);

	GpuSplatSorter *GetSorter() const
	{
		return m_sorter.get();
	}

	GpuSplatSorter::BufferInfo GetSorterBufferInfo() const
	{
		if (m_sorter)
			return m_sorter->GetBufferInfo();
		return {};
	}

	rhi::IRHISemaphore *GetComputeSemaphore() const override
	{
		// Only return a semaphore if we actually signaled one this frame
		if (!m_asyncComputeEnabled || !m_semaphoreSignaledThisFrame)
		{
			return nullptr;
		}

		// In pipelined mode, graphics waits on the semaphore from the PREVIOUS pipelined frame.
		// Warmup frames 0-1 don't signal semaphores.
		// Frame 2 is the first pipelined frame (signals sem[0]), m_pipelineFrameIndex becomes 3 after Update.
		// Frame 3 is the second pipelined frame (signals sem[1]), m_pipelineFrameIndex becomes 4 after Update.
		// We can only wait on a valid previous semaphore starting from frame 3.
		if (m_pipelineFrameIndex < 4)
		{
			return nullptr;
		}

		// readIndex matches the writeIndex from the previous frame
		// Frame 3 (after Update, idx=4): readIndex=0, wait on sem[0] signaled on frame 2
		// Frame 4 (after Update, idx=5): readIndex=1, wait on sem[1] signaled on frame 3
		uint32_t readIndex = m_pipelineFrameIndex % 2;
		return m_pipelineSemaphores[readIndex].Get();
	}

	rhi::IRHIBuffer *GetSortedIndicesBuffer() const override
	{
		if (!m_asyncComputeEnabled)
		{
			// Single queue mode: use app-provided buffer
			return m_targetBuffer.Get();
		}

		if (m_warmupComplete)
		{
			// Pipelined mode: return the READ buffer (previous frame's result)
			uint32_t readIndex = m_pipelineFrameIndex % 2;
			return m_pipelineBuffers[readIndex].Get();
		}
		else
		{
			// Frame 0 warmup: return current frame's buffer (serial rendering)
			uint32_t currentIndex = (m_pipelineFrameIndex - 1) % 2;
			return m_pipelineBuffers[currentIndex].Get();
		}
	}

	/// Check if pipelined async compute is warmed up, which means it has valid previous frame result with semaphore
	bool IsPipelineWarmedUp() const
	{
		// Need m_pipelineFrameIndex >= 4 for QFOT acquire
		return m_asyncComputeEnabled && m_pipelineFrameIndex >= 4;
	}

	void SetAsyncCompute(bool enabled) override;

	bool IsAsyncComputeEnabled() const override
	{
		return m_asyncComputeEnabled;
	}

	void SetTimingFrameIndex(uint32_t frameIndex) override;
	void SetTimingLatency(uint32_t latency) override;

	void SetSortAscending(bool ascending) override
	{
		m_sortAscending = ascending;
		if (m_sorter)
		{
			m_sorter->SetSortAscending(ascending);
		}
	}

	bool IsSortAscending() const override
	{
		return m_sortAscending;
	}

  private:
	rhi::IRHIDevice  *m_device = nullptr;
	Scene            *m_scene  = nullptr;
	rhi::BufferHandle m_targetBuffer;
	uint32_t          m_splatCount = 0;

	container::unique_ptr<GpuSplatSorter> m_sorter;
	int                                   m_currentMethod = 1;        // Default: IntegratedScan

	rhi::CommandListHandle m_computeCmdLists[2];        // Double-buffered for async compute
	rhi::CommandListHandle m_graphicsCmdList;

	// Pipelined async compute: double-buffered output for true N+1 parallelism
	// Frame N: Sort into buffer[N%2], render using buffer[(N-1)%2]
	rhi::BufferHandle    m_pipelineBuffers[2];                        // Double-buffered sorted indices
	rhi::SemaphoreHandle m_pipelineSemaphores[2];                     // Semaphore per buffer
	uint32_t             m_pipelineFrameIndex         = 0;            // Current write buffer index
	bool                 m_warmupComplete             = false;        // First frame has no previous result
	bool                 m_asyncComputeEnabled        = false;
	mutable bool         m_semaphoreSignaledThisFrame = false;        // Track if semaphore was signaled in Update

	// Verification state
	bool                                 m_prepareVerification  = false;
	bool                                 m_verificationPrepared = false;
	const container::vector<math::vec3> *m_testPositions        = nullptr;

	// Sort direction
	bool m_sortAscending = false;        // false = far-to-near, true = near-to-far
};

/// CPU backend implementation using Scene's CpuSplatSorter
class CpuSplatSortBackend : public ISplatSortBackend
{
  public:
	CpuSplatSortBackend();
	~CpuSplatSortBackend() override;

	bool Initialize(
	    rhi::IRHIDevice                        *device,
	    Scene                                  *scene,
	    rhi::BufferHandle                       sortedIndicesBuffer,
	    uint32_t                                totalSplatCount,
	    container::shared_ptr<vfs::IFileSystem> vfs = nullptr) override;

	void Update(const app::Camera &camera) override;

	bool        IsSortComplete() const override;
	SortMetrics GetMetrics() const override;
	const char *GetName() const override;
	const char *GetMethodName() const override;

	bool VerifySort() override;

  private:
	rhi::IRHIDevice  *m_device = nullptr;
	Scene            *m_scene  = nullptr;
	rhi::BufferHandle m_targetBuffer;
	uint32_t          m_splatCount = 0;

	// Timing
	timer::Timer m_sortTimer;
	float        m_lastSortDurationMs   = 0.0f;
	float        m_lastUploadDurationMs = 0.0f;
	bool         m_sortInProgress       = false;

	// Store last view matrix for verification
	math::mat4 m_lastViewMatrix{math::Identity()};
};

}        // namespace msplat::engine
