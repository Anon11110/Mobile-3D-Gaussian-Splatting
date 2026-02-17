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

	for (uint32_t i = 0; i < k_asyncPipelineDepth; ++i)
	{
		m_computeCmdLists[i]    = device->CreateCommandList(rhi::QueueType::COMPUTE);
		m_computeFences[i]      = device->CreateFence(true);
		m_pipelineSemaphores[i] = device->CreateSemaphore();
	}
	m_graphicsCmdList = device->CreateCommandList(rhi::QueueType::GRAPHICS);

	m_sorter = container::make_unique<GpuSplatSorter>(device, vfs);
	m_sorter->Initialize(totalSplatCount, sortedIndicesBuffer, k_asyncPipelineDepth);

	// Get the sorter's internal buffers for pipelined async compute
	for (uint32_t i = 0; i < k_asyncPipelineDepth; ++i)
	{
		m_pipelineBuffers[i] = m_sorter->GetOutputBuffer(i);
	}

	m_sorter->SetSortMethod(
	    static_cast<GpuSplatSorter::SortMethod>(m_currentMethod));

	LOG_INFO("GpuSplatSortBackend initialized for {} splats (pipeline depth {})", totalSplatCount, k_asyncPipelineDepth);
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
		// Pipelined async compute: Sort frame N+(K-1) while graphics renders frame N
		// writeIndex cycles: 0, 1, ..., K-1, 0, 1, ...
		uint32_t writeIndex = m_pipelineFrameIndex % k_asyncPipelineDepth;

		// Wait for this pipeline slot's previous compute submission to complete
		m_computeFences[writeIndex]->Wait(UINT64_MAX);
		m_computeFences[writeIndex]->Reset();

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

		// QFOT release starts at frame K (after K warmup frames).
		// Timeline:
		// - Frame 0..K-1: regular barriers (warmup, no QFOT)
		// - Frame K+: QFOT release the buffer we just wrote
		if (m_pipelineFrameIndex >= k_asyncPipelineDepth)
		{
			cmdList->ReleaseToQueue(rhi::QueueType::GRAPHICS, {&transition, 1}, {});
		}
		else
		{
			cmdList->Barrier(rhi::PipelineScope::Compute, rhi::PipelineScope::Compute, {&transition, 1}, {}, {});
		}

		if (m_pipelineFrameIndex >= k_asyncPipelineDepth)
		{
			m_warmupComplete = true;
		}

		cmdList->End();

		std::array<rhi::IRHICommandList *, 1> cmdLists = {cmdList};

		// During warmup phase (frames 0..K-1), use WaitIdle for synchronization
		bool isWarmupPhase = !m_warmupComplete && (m_pipelineFrameIndex < k_asyncPipelineDepth);

		if (isWarmupPhase)
		{
			// Warmup: submit with fence, then WaitIdle for full sync
			rhi::SubmitInfo submitInfo{};
			submitInfo.signalFence = m_computeFences[writeIndex].Get();
			m_device->SubmitCommandLists(cmdLists, rhi::QueueType::COMPUTE, submitInfo);
			m_device->WaitIdle();
			m_semaphoreSignaledThisFrame = false;

			m_sorter->ReadTimingResults();
		}
		else
		{
			// Pipelined mode: signal fence + semaphore for graphics queue to wait on
			rhi::SubmitInfo submitInfo{};
			submitInfo.signalFence        = m_computeFences[writeIndex].Get();
			rhi::IRHISemaphore *signalSem = m_pipelineSemaphores[writeIndex].Get();
			submitInfo.signalSemaphores   = std::span<rhi::IRHISemaphore *const>(&signalSem, 1);
			m_device->SubmitCommandLists(cmdLists, rhi::QueueType::COMPUTE, submitInfo);
			m_semaphoreSignaledThisFrame = true;

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

	// Ensure sorter outputs to the primary buffer (m_targetBuffer)
	// This is needed after switching from async compute where activeOutputBufferIndex may be >0
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

	// Track QFOT state before resetting. QFOT release starts on frame K:
	// - Frame 0..K-1: regular barriers (warmup, no QFOT)
	// - Frame K+: QFOT release
	uint32_t savedFrameIndex = m_pipelineFrameIndex;

	// Reset pipeline state when toggling async compute
	m_pipelineFrameIndex         = 0;
	m_warmupComplete             = false;
	m_semaphoreSignaledThisFrame = false;

	// Recreate fences (signaled) and semaphores (unsignaled) for clean pipeline start
	for (uint32_t i = 0; i < k_asyncPipelineDepth; ++i)
	{
		m_computeFences[i]      = m_device->CreateFence(true);
		m_pipelineSemaphores[i] = m_device->CreateSemaphore();
	}

	if (!enabled && savedFrameIndex >= k_asyncPipelineDepth + 1)
	{
		// Switching from async compute to single-queue mode.
		// Acquire the last (K-1) QFOT-released buffers that graphics hasn't consumed yet.
		rhi::IRHICommandList *cmdList = m_graphicsCmdList.Get();
		cmdList->Begin();

		uint32_t buffersToAcquire = std::min(savedFrameIndex - k_asyncPipelineDepth, k_asyncPipelineDepth - 1);
		for (uint32_t i = 1; i <= buffersToAcquire; ++i)
		{
			uint32_t              bufIdx = (savedFrameIndex - i) % k_asyncPipelineDepth;
			rhi::BufferTransition t{};
			t.buffer = m_pipelineBuffers[bufIdx].Get();
			t.before = rhi::ResourceState::ShaderReadWrite;
			t.after  = rhi::ResourceState::GeneralRead;
			cmdList->AcquireFromQueue(rhi::QueueType::COMPUTE, {&t, 1}, {});
		}

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
