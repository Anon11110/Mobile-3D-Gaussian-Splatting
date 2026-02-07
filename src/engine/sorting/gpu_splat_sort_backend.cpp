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

	m_computeCmdLists[0] = device->CreateCommandList(rhi::QueueType::COMPUTE);
	m_computeCmdLists[1] = device->CreateCommandList(rhi::QueueType::COMPUTE);
	m_graphicsCmdList    = device->CreateCommandList(rhi::QueueType::GRAPHICS);

	// Create semaphores for pipelined async compute
	m_pipelineSemaphores[0] = device->CreateSemaphore();
	m_pipelineSemaphores[1] = device->CreateSemaphore();

	m_sorter = container::make_unique<GpuSplatSorter>(device, vfs);
	m_sorter->Initialize(totalSplatCount, sortedIndicesBuffer);

	// Get the sorter's internal buffers for pipelined async compute
	// to avoids creating duplicate buffers and ensures descriptor sets match
	m_pipelineBuffers[0] = m_sorter->GetPrimaryOutputBuffer();
	m_pipelineBuffers[1] = m_sorter->GetAlternateOutputBuffer();

	m_sorter->SetSortMethod(
	    static_cast<GpuSplatSorter::SortMethod>(m_currentMethod));

	LOG_INFO("GpuSplatSortBackend initialized for {} splats (pipelined async compute ready)", totalSplatCount);
	return true;
}

void GpuSplatSortBackend::Update(const app::Camera &camera)
{
	if (!m_sorter || m_splatCount == 0)
	{
		return;
	}

	m_semaphoreSignaledThisFrame = false;

	if (m_asyncComputeEnabled)
	{
		// Pipelined async compute: Sort frame N+1 while graphics renders frame N
		// writeIndex alternates: 0, 1, 0, 1, ...
		uint32_t writeIndex = m_pipelineFrameIndex % 2;

		// Set output buffer for this frame's sort
		m_sorter->SetOutputBuffer(m_pipelineBuffers[writeIndex]);

		rhi::IRHICommandList *cmdList = m_computeCmdLists[writeIndex].Get();
		cmdList->Begin();

		m_sorter->Sort(cmdList, *m_scene, camera);

		// Barrier to transition buffer from compute write to graphics read
		rhi::BufferTransition transition{};
		transition.buffer = m_pipelineBuffers[writeIndex].Get();
		transition.before = rhi::ResourceState::ShaderReadWrite;
		transition.after  = rhi::ResourceState::GeneralRead;

		// Graphics acquires when m_pipelineFrameIndex >= 4 (after Update).
		// That means graphics first acquires on frame 3 (idx=3 at start, idx=4 after Update).
		// On frame 3, graphics reads buffer[0] (readIndex = 4 % 2 = 0).
		// Buffer[0] was written on frame 2 (idx=2, writeIndex = 0).
		// So we need to QFOT release starting frame 2 (idx >= 2).
		// Timeline:
		// - Frame 0-1: regular barriers (no QFOT)
		// - Frame 2: QFOT release buffer[0]
		// - Frame 3: QFOT release buffer[1], QFOT acquire buffer[0]
		// - Frame 4+: full QFOT pairs
		if (m_pipelineFrameIndex >= 2)
		{
			// Frame 2+: QFOT release the buffer we just wrote
			cmdList->ReleaseToQueue(rhi::QueueType::GRAPHICS, {&transition, 1}, {});
		}
		else
		{
			// Frame 0-1: memory barrier only, no QFOT
			cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {&transition, 1}, {}, {});
		}

		if (m_pipelineFrameIndex >= 2)
		{
			m_warmupComplete = true;
		}

		cmdList->End();

		std::array<rhi::IRHICommandList *, 1> cmdLists = {cmdList};

		// During warmup phase (frames 0-1), use WaitIdle for synchronization
		// Don't signal semaphores during warmup
		// - Frame 0-1: warmup, no semaphore (use WaitIdle for sync)
		// - Frame 2+: pipelined, semaphore signal/wait pairs
		bool isWarmupPhase = !m_warmupComplete && (m_pipelineFrameIndex < 2);

		if (isWarmupPhase)
		{
			// Warmup: submit without semaphore signal, use WaitIdle for sync
			m_device->SubmitCommandLists(cmdLists, rhi::QueueType::COMPUTE);
			m_device->WaitIdle();
			m_semaphoreSignaledThisFrame = false;

			m_sorter->ReadTimingResults();
		}
		else
		{
			// Pipelined mode: signal semaphore for graphics queue to wait on
			rhi::SubmitInfo     submitInfo{};
			rhi::IRHISemaphore *signalSem = m_pipelineSemaphores[writeIndex].Get();
			submitInfo.signalSemaphores   = std::span<rhi::IRHISemaphore *const>(&signalSem, 1);
			m_device->SubmitCommandLists(cmdLists, rhi::QueueType::COMPUTE, submitInfo);
			m_semaphoreSignaledThisFrame = true;

			// In pipelined mode, use non-blocking read here.
			// The compute work from 3 frames ago may not have completed yet since
			// we only sync via semaphore which graphics hasn't waited on yet.
			m_sorter->ReadTimingResultsNonBlocking();
		}

		m_pipelineFrameIndex++;
	}
}

void GpuSplatSortBackend::Update(const app::Camera &camera, rhi::IRHICommandList *cmdList)
{
	if (!m_sorter || m_splatCount == 0)
	{
		return;
	}

	m_semaphoreSignaledThisFrame = false;

	// Ensure sorter outputs to the primary buffer (m_targetBuffer = sortIndicesB)
	// This is needed after switching from async compute where activeOutputBufferIndex may be 1
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

void GpuSplatSortBackend::SetAsyncCompute(bool enabled)
{
	if (m_asyncComputeEnabled == enabled || !m_device)
	{
		m_asyncComputeEnabled = enabled;
		return;
	}

	m_device->WaitIdle();

	// Track QFOT state before resetting. QFOT release starts on frame 2:
	// - Frame 0-1: regular barriers (no QFOT)
	// - Frame 2: QFOT release buffer[0]
	// - Frame 3: QFOT release buffer[1], graphics acquires buffer[0]
	// - Frame 4+: full QFOT pairs
	uint32_t savedFrameIndex = m_pipelineFrameIndex;

	// Reset pipeline state when toggling async compute
	m_pipelineFrameIndex         = 0;
	m_warmupComplete             = false;
	m_semaphoreSignaledThisFrame = false;

	// Recreate semaphores to ensure they start in unsignaled state
	m_pipelineSemaphores[0] = m_device->CreateSemaphore();
	m_pipelineSemaphores[1] = m_device->CreateSemaphore();

	if (!enabled && savedFrameIndex >= 3)
	{
		// Switching from async compute to single-queue mode
		// QFOT release starts on frame 2 (idx >= 2), so savedFrameIndex >= 3 means at least one QFOT release happened.
		// In pipelined mode:
		// - Compute last wrote to buffer[(savedFrameIndex-1) % 2], which was QFOT released
		// - Graphics last read buffer[savedFrameIndex % 2], which was already QFOT acquired

		rhi::IRHICommandList *cmdList = m_graphicsCmdList.Get();
		cmdList->Begin();

		uint32_t              lastWriteIndex = (savedFrameIndex - 1) % 2;
		rhi::BufferTransition t{};
		t.buffer = m_pipelineBuffers[lastWriteIndex].Get();
		t.before = rhi::ResourceState::ShaderReadWrite;
		t.after  = rhi::ResourceState::GeneralRead;
		cmdList->AcquireFromQueue(rhi::QueueType::COMPUTE, {&t, 1}, {});

		cmdList->End();

		std::array<rhi::IRHICommandList *, 1> cmdLists = {cmdList};
		m_device->SubmitCommandLists(cmdLists, rhi::QueueType::GRAPHICS);
		m_device->WaitIdle();
	}

	m_asyncComputeEnabled = enabled;
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
