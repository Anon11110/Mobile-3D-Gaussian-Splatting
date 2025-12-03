#include "msplat/core/parallel.h"

#include "msplat/core/log.h"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Platform detection for default configuration
#if defined(_WIN32) || defined(_WIN64)
#	define MSPLAT_PLATFORM_DESKTOP 1
#elif defined(__APPLE__)
#	include <TargetConditionals.h>
#	if TARGET_OS_IOS
#		define MSPLAT_PLATFORM_MOBILE 1
#	else
#		define MSPLAT_PLATFORM_DESKTOP 1
#	endif
#elif defined(__ANDROID__)
#	define MSPLAT_PLATFORM_MOBILE 1
#elif defined(__linux__)
#	define MSPLAT_PLATFORM_DESKTOP 1
#else
#	define MSPLAT_PLATFORM_DESKTOP 1
#endif

namespace msplat::core
{

// ============================================================================
// Thread-local state for nested parallelism detection
// ============================================================================

namespace
{
thread_local bool g_insideParallelRegion = false;

/**
 * RAII guard for setting/clearing the parallel region flag.
 */
class ParallelRegionGuard
{
  public:
	ParallelRegionGuard()
	{
		g_insideParallelRegion = true;
	}
	~ParallelRegionGuard()
	{
		g_insideParallelRegion = false;
	}

	ParallelRegionGuard(const ParallelRegionGuard &)            = delete;
	ParallelRegionGuard &operator=(const ParallelRegionGuard &) = delete;
};

}        // anonymous namespace

bool IsInsideParallelRegion()
{
	return g_insideParallelRegion;
}

// ============================================================================
// ThreadPool implementation
// ============================================================================

namespace
{

class ThreadPool
{
  public:
	explicit ThreadPool(uint32_t numThreads);
	~ThreadPool();

	// Non-copyable, non-movable
	ThreadPool(const ThreadPool &)            = delete;
	ThreadPool &operator=(const ThreadPool &) = delete;
	ThreadPool(ThreadPool &&)                 = delete;
	ThreadPool &operator=(ThreadPool &&)      = delete;

	/**
	 * Enqueue a task to be executed by a worker thread.
	 */
	void Enqueue(std::function<void()> task);

	/**
	 * Wait for all currently enqueued tasks to complete.
	 */
	void WaitAll();

	/**
	 * Get the number of worker threads.
	 */
	uint32_t GetThreadCount() const
	{
		return static_cast<uint32_t>(m_workers.size());
	}

  private:
	std::vector<std::thread>          m_workers;
	std::queue<std::function<void()>> m_tasks;

	std::mutex              m_mutex;
	std::condition_variable m_taskAvailable;
	std::condition_variable m_taskComplete;

	std::atomic<bool>     m_stop{false};
	std::atomic<uint32_t> m_activeTasks{0};
};

ThreadPool::ThreadPool(uint32_t numThreads)
{
	if (numThreads == 0)
	{
		numThreads = 1;        // Minimum 1 thread
	}

	m_workers.reserve(numThreads);

	for (uint32_t i = 0; i < numThreads; ++i)
	{
		m_workers.emplace_back([this] {
			while (true)
			{
				std::function<void()> task;

				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_taskAvailable.wait(lock, [this] { return m_stop.load() || !m_tasks.empty(); });

					if (m_stop.load() && m_tasks.empty())
					{
						return;
					}

					task = std::move(m_tasks.front());
					m_tasks.pop();

					// Mark thread as working on a task BEFORE releasing the lock
					// This prevents WaitAll() from returning early when the queue
					// is empty but a task is about to be executed.
					++m_activeTasks;
				}

				// Execute the task with parallel region guard
				{
					ParallelRegionGuard guard;
					task();
				}

				// Mark task complete and notify waiters
				if (--m_activeTasks == 0)
				{
					m_taskComplete.notify_all();
				}
			}
		});
	}

	LOG_DEBUG("ThreadPool created with {} worker threads", numThreads);
}

ThreadPool::~ThreadPool()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop.store(true);
	}

	m_taskAvailable.notify_all();

	for (auto &worker : m_workers)
	{
		if (worker.joinable())
		{
			worker.join();
		}
	}

	// Note: Don't log here - this destructor runs during static destruction
	// when the logger might already be destroyed
}

void ThreadPool::Enqueue(std::function<void()> task)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_tasks.push(std::move(task));
	}
	m_taskAvailable.notify_one();
}

void ThreadPool::WaitAll()
{
	std::unique_lock<std::mutex> lock(m_mutex);
	m_taskComplete.wait(lock, [this] { return m_tasks.empty() && m_activeTasks.load() == 0; });
}

// ============================================================================
// Global state
// ============================================================================

ParallelConfig g_parallelConfig;
bool           g_configInitialized = false;
std::mutex     g_configMutex;

ThreadPool *g_threadPool = nullptr;
std::mutex  g_poolMutex;

/**
 * Initialize parallel configuration with platform-appropriate defaults.
 */
void InitializeConfig()
{
	if (g_configInitialized)
	{
		return;
	}

	uint32_t hwConcurrency = std::thread::hardware_concurrency();
	if (hwConcurrency == 0)
	{
		hwConcurrency = 4;        // Fallback if hardware_concurrency returns 0
	}

#if defined(MSPLAT_PLATFORM_MOBILE)
	// Mobile: Conservative defaults
	g_parallelConfig.maxThreads       = std::min(hwConcurrency, 4u);
	g_parallelConfig.minParallelSize  = 65536;        // 64K threshold
	g_parallelConfig.defaultGrainSize = 8192;
#else
	// Desktop: More aggressive parallelism
	g_parallelConfig.maxThreads       = std::min(hwConcurrency > 1 ? hwConcurrency - 1 : 1u, 8u);
	g_parallelConfig.minParallelSize  = 32768;        // 32K threshold
	g_parallelConfig.defaultGrainSize = 8192;
#endif

	g_configInitialized = true;

	LOG_DEBUG("Parallel config initialized: maxThreads={}, minParallelSize={}, grainSize={}",
	          g_parallelConfig.maxThreads, g_parallelConfig.minParallelSize,
	          g_parallelConfig.defaultGrainSize);
}

/**
 * Get or create the global thread pool.
 */
ThreadPool &GetThreadPool()
{
	std::lock_guard<std::mutex> lock(g_poolMutex);

	if (g_threadPool == nullptr)
	{
		// Ensure config is initialized
		{
			std::lock_guard<std::mutex> configLock(g_configMutex);
			InitializeConfig();
		}

		uint32_t threadCount = g_parallelConfig.maxThreads;
		if (threadCount == 0)
		{
			threadCount = std::thread::hardware_concurrency();
			if (threadCount == 0)
			{
				threadCount = 4;
			}
#if defined(MSPLAT_PLATFORM_MOBILE)
			threadCount = std::min(threadCount, 4u);
#else
			threadCount = std::min(threadCount > 1 ? threadCount - 1 : 1u, 8u);
#endif
		}

		// Create pool with at least 1 thread
		g_threadPool = new ThreadPool(std::max(threadCount, 1u));
	}

	return *g_threadPool;
}

}        // anonymous namespace

// ============================================================================
// Public API implementation
// ============================================================================

ParallelConfig &GetParallelConfig()
{
	std::lock_guard<std::mutex> lock(g_configMutex);
	InitializeConfig();
	return g_parallelConfig;
}

void SetParallelThreads(uint32_t threads)
{
	std::lock_guard<std::mutex> lock(g_configMutex);
	InitializeConfig();
	g_parallelConfig.maxThreads = threads;

	// Note: This doesn't resize an existing pool. The pool uses the config
	// at creation time. To change thread count, restart the application.
	LOG_DEBUG("Parallel maxThreads set to {}", threads);
}

void SetParallelMinSize(size_t minSize)
{
	std::lock_guard<std::mutex> lock(g_configMutex);
	InitializeConfig();
	g_parallelConfig.minParallelSize = minSize;
	LOG_DEBUG("Parallel minParallelSize set to {}", minSize);
}

void SetParallelGrainSize(size_t grainSize)
{
	std::lock_guard<std::mutex> lock(g_configMutex);
	InitializeConfig();
	g_parallelConfig.defaultGrainSize = grainSize;
	LOG_DEBUG("Parallel defaultGrainSize set to {}", grainSize);
}

namespace detail
{

void ParallelForImpl(size_t begin, size_t end, size_t grainSize, void (*taskFunc)(size_t, size_t, void *),
                     void *userData)
{
	if (begin >= end)
	{
		return;
	}

	ThreadPool &pool = GetThreadPool();

	// Calculate number of chunks
	const size_t count     = end - begin;
	const size_t numChunks = (count + grainSize - 1) / grainSize;

	// For very few chunks, it may not be worth the overhead
	if (numChunks <= 1)
	{
		taskFunc(begin, end, userData);
		return;
	}

	// Enqueue chunks
	for (size_t chunkIdx = 0; chunkIdx < numChunks; ++chunkIdx)
	{
		const size_t chunkBegin = begin + chunkIdx * grainSize;
		const size_t chunkEnd   = std::min(chunkBegin + grainSize, end);

		pool.Enqueue([taskFunc, chunkBegin, chunkEnd, userData]() { taskFunc(chunkBegin, chunkEnd, userData); });
	}

	// Wait for all chunks to complete
	pool.WaitAll();
}

}        // namespace detail

// ============================================================================
// Cleanup at program exit
// ============================================================================

namespace
{

struct ThreadPoolCleanup
{
	~ThreadPoolCleanup()
	{
		// Note: This runs during static destruction - logging may not work
		std::lock_guard<std::mutex> lock(g_poolMutex);
		if (g_threadPool != nullptr)
		{
			delete g_threadPool;
			g_threadPool = nullptr;
		}
	}
};

// Static instance ensures cleanup at program exit
static ThreadPoolCleanup g_poolCleanup;

}        // anonymous namespace

}        // namespace msplat::core
