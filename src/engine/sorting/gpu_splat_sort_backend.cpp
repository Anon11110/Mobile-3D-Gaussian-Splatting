#include <array>
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

	// Create GPU sorter
	m_sorter = container::make_unique<GpuSplatSorter>(device, vfs);
	m_sorter->Initialize(totalSplatCount);

	// Set default sort method
	m_sorter->SetSortMethod(
	    static_cast<GpuSplatSorter::SortMethod>(m_currentMethod));

	LOG_INFO("GpuSplatSortBackend initialized for {} splats", totalSplatCount);
	return true;
}

void GpuSplatSortBackend::Update(const app::Camera &camera)
{
	if (!m_sorter || m_splatCount == 0)
	{
		return;
	}

	// Create command list for compute work
	auto cmdList = m_device->CreateCommandList(rhi::QueueType::COMPUTE);
	cmdList->Begin();

	// Execute GPU sort - pass camera directly
	m_sorter->Sort(cmdList.Get(), *m_scene, camera);

	// Copy result from sorter's internal buffer to app-owned target buffer
	rhi::BufferHandle sorterOutput = m_sorter->GetSortedIndices();
	rhi::BufferCopy   copyRegion{};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size      = static_cast<uint64_t>(m_splatCount) * sizeof(uint32_t);

	std::array<rhi::BufferCopy, 1> regions = {copyRegion};
	cmdList->CopyBuffer(sorterOutput.Get(), m_targetBuffer.Get(), regions);

	cmdList->End();

	// Submit and wait for completion
	std::array<rhi::IRHICommandList *, 1> cmdLists = {cmdList.Get()};
	m_device->SubmitCommandLists(cmdLists, rhi::QueueType::COMPUTE);
	m_device->WaitIdle();
}

bool GpuSplatSortBackend::IsSortComplete() const
{
	// GPU sort is synchronous within Update()
	return true;
}

SortMetrics GpuSplatSortBackend::GetMetrics() const
{
	// TODO: Add GPU timing queries for accurate metrics
	return SortMetrics{
	    .sortDurationMs   = 0.0f,
	    .uploadDurationMs = 0.0f,
	    .sortComplete     = true};
}

const char *GpuSplatSortBackend::GetName() const
{
	return "GPU";
}

bool GpuSplatSortBackend::VerifySort()
{
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
}

void GpuSplatSortBackend::RequestVerification()
{
	m_prepareVerification = true;
}

void GpuSplatSortBackend::PrepareVerification(rhi::IRHICommandList *cmdList)
{
	if (m_sorter)
	{
		m_sorter->PrepareVerification(cmdList);
		m_verificationPrepared = true;
	}
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

bool GpuSplatSortBackend::HasComprehensiveVerification() const
{
	return true;
}

bool GpuSplatSortBackend::RunComprehensiveVerification()
{
	if (!m_sorter)
	{
		return false;
	}

	return m_sorter->VerifySortOrder();
}

}        // namespace msplat::engine
