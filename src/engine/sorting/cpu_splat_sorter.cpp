#include "engine/sorting/cpu_splat_sorter.h"
#include "core/log.h"
#include "core/parallel.h"
#include "engine/splat/splat_math.h"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace msplat::engine
{

class CpuSplatSorter::Impl
{
  public:
	explicit Impl(uint32_t max_splats);
	~Impl();

	void                            RequestSort(const container::vector<math::vec3> &splat_positions, const math::mat4 &view_matrix);
	bool                            IsSortComplete() const;
	container::span<const uint32_t> GetSortedIndices();

  private:
	void SortWorker();

	// Threading and synchronization
	std::thread             m_workerThread;
	std::mutex              m_mutex;
	std::condition_variable m_cv;
	std::atomic<bool>       m_stopWorker    = false;
	bool                    m_sortRequested = false;

	// Double buffered sort data
	container::vector<uint32_t> m_indices_A;
	container::vector<uint32_t> m_indices_B;
	container::vector<float>    m_depths;
	uint32_t                   *m_producerBuffer = nullptr;
	std::atomic<uint32_t *>     m_consumerBuffer = nullptr;

	// Data for the worker
	container::vector<math::vec3> m_worker_positions;
	math::mat4                    m_worker_view_matrix;
};

CpuSplatSorter::Impl::Impl(uint32_t max_splats)
{
	m_indices_A.resize(max_splats);
	m_indices_B.resize(max_splats);
	m_depths.resize(max_splats);

	m_producerBuffer = m_indices_A.data();
	m_consumerBuffer.store(nullptr);        // No data ready initially

	m_workerThread = std::thread(&CpuSplatSorter::Impl::SortWorker, this);
}

CpuSplatSorter::Impl::~Impl()
{
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_stopWorker = true;
	}
	m_cv.notify_one();
	if (m_workerThread.joinable())
	{
		m_workerThread.join();
	}
}

void CpuSplatSorter::Impl::RequestSort(const container::vector<math::vec3> &splat_positions, const math::mat4 &view_matrix)
{
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_sortRequested)
		{
			// A sort is already pending, don't queue another one
			return;
		}
		m_worker_positions   = splat_positions;
		m_worker_view_matrix = view_matrix;
		m_sortRequested      = true;
	}
	m_cv.notify_one();
}

bool CpuSplatSorter::Impl::IsSortComplete() const
{
	return m_consumerBuffer.load() != nullptr;
}

container::span<const uint32_t> CpuSplatSorter::Impl::GetSortedIndices()
{
	uint32_t *readyBuffer = m_consumerBuffer.exchange(nullptr);
	if (readyBuffer)
	{
		return {readyBuffer, m_indices_A.size()};
	}
	return {};
}

void CpuSplatSorter::Impl::SortWorker()
{
	while (true)
	{
		// Wait for a sort request or shutdown signal
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this] { return m_sortRequested || m_stopWorker; });

			if (m_stopWorker)
			{
				break;
			}
		}
		// Lock released here - safe to do parallel work since:
		// - m_worker_positions and m_worker_view_matrix are only written by RequestSort
		//   which returns early if m_sortRequested is true
		// - m_depths and m_producerBuffer are only accessed by this worker thread

		const size_t count = m_worker_positions.size();

		// 1. Calculate depths in parallel
		// Uses ParallelFor to distribute depth computation across multiple threads.
		// Each chunk writes to disjoint ranges of m_depths and m_producerBuffer.
		core::ParallelFor(
		    static_cast<size_t>(0), count, static_cast<size_t>(0),        // 0 = use default grain size
		    [this](size_t begin, size_t end) {
			    for (size_t i = begin; i < end; ++i)
			    {
				    m_depths[i]         = ComputeViewSpaceDepth(m_worker_positions[i], m_worker_view_matrix);
				    m_producerBuffer[i] = static_cast<uint32_t>(i);
			    }
		    });

		// 2. Sort indices based on depths (back to front)
		// Uses ParallelSort which leverages std::execution::par_unseq when available.
		auto *depthsPtr = m_depths.data();
		core::ParallelSort(m_producerBuffer, m_producerBuffer + count,
		                   [depthsPtr](const uint32_t a, const uint32_t b) { return depthsPtr[a] < depthsPtr[b]; });

		// 3. Mark as complete and swap to the other buffer for next sort
		uint32_t *completedBuffer = m_producerBuffer;
		m_producerBuffer          = (completedBuffer == m_indices_A.data()) ? m_indices_B.data() : m_indices_A.data();
		m_consumerBuffer.store(completedBuffer);

		// Reset request flag (needs lock for synchronization with RequestSort)
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_sortRequested = false;
		}
	}
}

// --- CpuSplatSorter PIMPL Forwarders ---

CpuSplatSorter::CpuSplatSorter(uint32_t max_splats) :
    p_impl(container::make_unique<Impl>(max_splats))
{}
CpuSplatSorter::~CpuSplatSorter() = default;
void CpuSplatSorter::RequestSort(const container::vector<math::vec3> &splat_positions, const math::mat4 &view_matrix)
{
	p_impl->RequestSort(splat_positions, view_matrix);
}
bool CpuSplatSorter::IsSortComplete() const
{
	return p_impl->IsSortComplete();
}
container::span<const uint32_t> CpuSplatSorter::GetSortedIndices()
{
	return p_impl->GetSortedIndices();
}

}        // namespace msplat::engine