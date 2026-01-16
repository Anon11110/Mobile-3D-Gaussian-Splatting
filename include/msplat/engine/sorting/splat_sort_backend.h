#pragma once

#include <cstdint>
#include <msplat/app/camera.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/math/math.h>
#include <msplat/core/timer.h>
#include <msplat/core/vfs.h>
#include <rhi/rhi.h>

namespace msplat::engine
{

// Forward declarations
class Scene;
class GpuSplatSorter;

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

	rhi::IRHISemaphore *GetComputeSemaphore() const override
	{
		// Only return semaphore if Update() was called this frame (semaphore was signaled)
		// to prevent waiting on unsignaled semaphore after backend switch mid-frame
		return m_semaphoreSignaledThisFrame ? m_computeDoneSemaphore.Get() : nullptr;
	}

	rhi::IRHIBuffer *GetSortedIndicesBuffer() const override
	{
		return m_targetBuffer.Get();
	}

  private:
	rhi::IRHIDevice  *m_device = nullptr;
	Scene            *m_scene  = nullptr;
	rhi::BufferHandle m_targetBuffer;
	uint32_t          m_splatCount = 0;

	container::unique_ptr<GpuSplatSorter> m_sorter;
	int                                   m_currentMethod = 1;        // Default: IntegratedScan

	rhi::CommandListHandle m_cmdList;
	rhi::SemaphoreHandle   m_computeDoneSemaphore;
	mutable bool           m_semaphoreSignaledThisFrame = false;

	// Verification state
	bool                                 m_prepareVerification  = false;
	bool                                 m_verificationPrepared = false;
	const container::vector<math::vec3> *m_testPositions        = nullptr;
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
