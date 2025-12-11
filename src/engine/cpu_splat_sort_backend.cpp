#include <msplat/core/log.h>
#include <msplat/engine/scene.h>
#include <msplat/engine/splat_sort_backend.h>

namespace msplat::engine
{

CpuSplatSortBackend::CpuSplatSortBackend()  = default;
CpuSplatSortBackend::~CpuSplatSortBackend() = default;

bool CpuSplatSortBackend::Initialize(
    rhi::IRHIDevice  *device,
    Scene            *scene,
    rhi::BufferHandle sortedIndicesBuffer,
    uint32_t          totalSplatCount)
{
	m_device       = device;
	m_scene        = scene;
	m_targetBuffer = sortedIndicesBuffer;
	m_splatCount   = totalSplatCount;

	LOG_INFO("CpuSplatSortBackend initialized for {} splats", totalSplatCount);
	return true;
}

void CpuSplatSortBackend::Update(const app::Camera &camera)
{
	if (!m_scene || m_splatCount == 0)
	{
		return;
	}

	// Start timing if new sort
	if (!m_sortInProgress)
	{
		m_sortTimer.reset();
		m_sortTimer.start();
		m_sortInProgress = true;
	}

	// Trigger async sort via Scene
	m_scene->UpdateView(camera.GetViewMatrix());

	// Check if sort completed and upload
	if (m_scene->IsCpuSortComplete())
	{
		auto sortedIndices = m_scene->GetCpuSortedIndices();
		if (!sortedIndices.empty())
		{
			m_sortTimer.stop();
			m_lastSortDurationMs = static_cast<float>(m_sortTimer.elapsedMilliseconds());

			timer::Timer uploadTimer;
			uploadTimer.start();

			// Upload to app-owned buffer
			auto fence = m_device->UploadBufferAsync(
			    m_targetBuffer.Get(),
			    sortedIndices.data(),
			    sortedIndices.size() * sizeof(uint32_t));

			if (fence)
			{
				fence->Wait();
			}

			uploadTimer.stop();
			m_lastUploadDurationMs = static_cast<float>(uploadTimer.elapsedMilliseconds());
			m_sortInProgress       = false;
		}
	}
}

bool CpuSplatSortBackend::IsSortComplete() const
{
	if (!m_scene)
	{
		return true;
	}
	return m_scene->IsCpuSortComplete();
}

SortMetrics CpuSplatSortBackend::GetMetrics() const
{
	return SortMetrics{
	    .sortDurationMs   = m_lastSortDurationMs,
	    .uploadDurationMs = m_lastUploadDurationMs,
	    .sortComplete     = IsSortComplete()};
}

const char *CpuSplatSortBackend::GetName() const
{
	return "CPU";
}

const char *CpuSplatSortBackend::GetMethodName() const
{
	return "Async Radix";
}

}        // namespace msplat::engine
