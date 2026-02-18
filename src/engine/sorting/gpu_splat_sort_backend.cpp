#include <msplat/core/log.h>
#include <msplat/engine/sorting/gpu_splat_sorter.h>
#include <msplat/engine/sorting/splat_sort_backend.h>

namespace msplat::engine
{

GpuSplatSortBackend::GpuSplatSortBackend()  = default;
GpuSplatSortBackend::~GpuSplatSortBackend() = default;

bool GpuSplatSortBackend::Initialize(
    rhi::IRHIDevice                        *device,
    Scene                                  *scene,
    rhi::BufferHandle                       sortedIndicesBuffer,
    uint32_t                                totalSplatCount,
    container::shared_ptr<vfs::IFileSystem> vfs)
{
	m_device       = device;
	m_scene        = scene;
	m_targetBuffer = sortedIndicesBuffer;
	m_splatCount   = totalSplatCount;

	m_sorter = container::make_unique<GpuSplatSorter>(device, vfs);
	m_sorter->Initialize(totalSplatCount, sortedIndicesBuffer, k_maxFramesInFlight);

	m_sorter->SetSortMethod(
	    static_cast<GpuSplatSorter::SortMethod>(m_currentMethod));

	LOG_INFO("GpuSplatSortBackend initialized for {} splats", totalSplatCount);
	return true;
}

void GpuSplatSortBackend::Update(const app::Camera &camera)
{
	(void) camera;
}

void GpuSplatSortBackend::Update(const app::Camera &camera, rhi::IRHICommandList *cmdList)
{
	if (!m_sorter || m_splatCount == 0)
	{
		return;
	}

	// Ensure sorter outputs to the primary buffer (m_targetBuffer)
	m_sorter->SetOutputBuffer(m_targetBuffer);

	m_sorter->Sort(cmdList, *m_scene, camera);

	// Barrier to transition buffer from compute write to graphics read
	rhi::BufferTransition transition{};
	transition.buffer = m_targetBuffer.Get();
	transition.before = rhi::ResourceState::ShaderReadWrite;
	transition.after  = rhi::ResourceState::GeneralRead;
	cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Graphics, {&transition, 1}, {}, {});

	// Read GPU timing results from previous frames
	m_sorter->ReadTimingResults();
}

bool GpuSplatSortBackend::IsSortComplete() const
{
	// GPU sort is synchronous within Update()
	return true;
}

SortMetrics GpuSplatSortBackend::GetMetrics() const
{
	return SortMetrics{
	    .sortDurationMs   = m_sorter ? static_cast<float>(m_sorter->GetLastSortTimeMs()) : 0.0f,
	    .uploadDurationMs = 0.0f,
	    .sortComplete     = true};
}

const char *GpuSplatSortBackend::GetName() const
{
	return "GPU";
}

bool GpuSplatSortBackend::VerifySort()
{
#ifdef ENABLE_SORT_VERIFICATION
	if (!m_sorter)
	{
		return false;
	}

	if (!m_verificationPrepared)
	{
		LOG_WARNING("Verification not prepared - call RequestVerification() first, then Update()");
		return false;
	}

	m_verificationPrepared = false;
	return m_sorter->VerifySortOrder();
#else
	return true;
#endif
}

void GpuSplatSortBackend::RequestVerification()
{
#ifdef ENABLE_SORT_VERIFICATION
	m_prepareVerification = true;
#endif
}

void GpuSplatSortBackend::PrepareVerification(rhi::IRHICommandList *cmdList)
{
#ifdef ENABLE_SORT_VERIFICATION
	if (m_sorter)
	{
		m_sorter->PrepareVerification(cmdList);
		m_verificationPrepared = true;
	}
#else
	(void) cmdList;
#endif
}

void GpuSplatSortBackend::SetSortMethod(int method)
{
	m_currentMethod = method;
	if (m_sorter)
	{
		m_sorter->SetSortMethod(
		    static_cast<GpuSplatSorter::SortMethod>(method));
	}
}

int GpuSplatSortBackend::GetSortMethod() const
{
	return m_currentMethod;
}

const char *GpuSplatSortBackend::GetMethodName() const
{
	return m_currentMethod == 0 ? "Prescan" : "Integrated Scan";
}

void GpuSplatSortBackend::SetShaderVariant(int variant)
{
	if (!m_sorter)
	{
		return;
	}

	m_sorter->SetShaderVariant(variant == 0 ? GpuSplatSorter::ShaderVariant::Portable : GpuSplatSorter::ShaderVariant::SubgroupOptimized);
}

int GpuSplatSortBackend::GetShaderVariant() const
{
	if (!m_sorter)
	{
		return 0;
	}

	return m_sorter->GetShaderVariant() == GpuSplatSorter::ShaderVariant::Portable ? 0 : 1;
}

const char *GpuSplatSortBackend::GetShaderVariantName() const
{
	if (!m_sorter)
	{
		return "Unknown";
	}

	return m_sorter->GetShaderVariantName();
}

bool GpuSplatSortBackend::HasComprehensiveVerification() const
{
#ifdef ENABLE_SORT_VERIFICATION
	return true;
#else
	return false;
#endif
}

bool GpuSplatSortBackend::RunComprehensiveVerification()
{
#ifdef ENABLE_SORT_VERIFICATION
	if (!m_sorter)
	{
		return false;
	}

	return m_sorter->CheckVerificationResults(m_testPositions);
#else
	return true;
#endif
}

void GpuSplatSortBackend::SetTestPositions(const container::vector<math::vec3> *positions)
{
	m_testPositions = positions;
}

void GpuSplatSortBackend::SetTimingFrameIndex(uint32_t frameIndex)
{
	if (m_sorter)
	{
		m_sorter->SetTimingFrameIndex(frameIndex);
	}
}

void GpuSplatSortBackend::SetTimingLatency(uint32_t latency)
{
	if (m_sorter)
	{
		m_sorter->SetTimingLatency(latency);
	}
}

}        // namespace msplat::engine
