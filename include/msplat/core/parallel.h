#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

#if defined(MSPLAT_ENABLE_STD_PAR_SORT) && __has_include(<execution>)
#	include <execution>
#	define MSPLAT_HAS_PARALLEL_EXECUTION 1
#else
#	define MSPLAT_HAS_PARALLEL_EXECUTION 0
#endif

namespace msplat::core
{

/**
 * Configuration for parallel execution primitives.
 *
 * Controls threading behavior for parallelFor and parallelSort operations.
 * Platform-specific defaults are applied at initialization.
 */
struct ParallelConfig
{
	/**
	 * Maximum number of worker threads to use.
	 * 0 = auto-detect from std::thread::hardware_concurrency()
	 * 1 = disable parallelism (serial execution)
	 */
	uint32_t maxThreads = 0;

	/**
	 * Minimum number of items before parallelism is considered.
	 * Below this threshold, work is executed serially to avoid overhead.
	 * Desktop default: 32768, Mobile default: 65536
	 */
	size_t minParallelSize = 32768;

	/**
	 * Preferred chunk size for parallel work distribution.
	 * Larger values reduce scheduling overhead but may cause load imbalance.
	 * Matches vk_gaussian_splatting BATCHSIZE convention.
	 */
	size_t defaultGrainSize = 8192;
};

/**
 * Get the global parallel configuration.
 *
 * The configuration is initialized with platform-appropriate defaults on first access.
 * Thread-safe for reading; modifications should be done before any parallel operations.
 *
 * @return Reference to the global ParallelConfig
 */
ParallelConfig &GetParallelConfig();

/**
 * Set the maximum number of worker threads.
 *
 * @param threads Number of threads (0 = auto-detect, 1 = disable parallelism)
 */
void SetParallelThreads(uint32_t threads);

/**
 * Set the minimum size threshold for parallelism.
 *
 * @param minSize Minimum number of items before parallel execution is considered
 */
void SetParallelMinSize(size_t minSize);

/**
 * Set the default grain size for work distribution.
 *
 * @param grainSize Preferred chunk size for parallel work
 */
void SetParallelGrainSize(size_t grainSize);

/**
 * Check if currently executing inside a parallel region.
 *
 * Used to detect nested parallelism and prevent deadlocks.
 *
 * @return true if inside a parallelFor callback
 */
bool IsInsideParallelRegion();

/**
 * Execute a function over a range in parallel.
 *
 * Splits the range [begin, end) into chunks of approximately grainSize elements
 * and executes func(chunkBegin, chunkEnd) on worker threads. Falls back to
 * serial execution for small ranges or when maxThreads is 1.
 *
 * The function must be thread-safe as it will be called concurrently from
 * multiple threads. Each chunk range is guaranteed to be non-overlapping.
 *
 * @tparam Func Callable with signature void(size_t begin, size_t end)
 * @param begin Start of the range (inclusive)
 * @param end End of the range (exclusive)
 * @param grainSize Preferred chunk size (0 = use defaultGrainSize from config)
 * @param func Function to execute for each chunk
 */
template <typename Func>
void ParallelFor(size_t begin, size_t end, size_t grainSize, Func &&func);

/**
 * Execute a function over a range in parallel (convenience overload).
 *
 * Equivalent to ParallelFor(0, count, 0, func).
 *
 * @tparam Func Callable with signature void(size_t begin, size_t end)
 * @param count Number of elements to process
 * @param func Function to execute for each chunk
 */
template <typename Func>
void ParallelFor(size_t count, Func &&func);

/**
 * Sort a range using parallel algorithms when available.
 *
 * Uses std::sort with std::execution::par_unseq when MSPLAT_ENABLE_STD_PAR_SORT
 * is defined and the platform supports it. Otherwise falls back to std::sort.
 *
 * @tparam RandomIt Random access iterator type
 * @tparam Compare Comparison function type
 * @param first Iterator to the beginning of the range
 * @param last Iterator to the end of the range
 * @param comp Comparison function
 */
template <typename RandomIt, typename Compare>
void ParallelSort(RandomIt first, RandomIt last, Compare comp);

/**
 * Sort a range using parallel algorithms when available (default comparison).
 *
 * @tparam RandomIt Random access iterator type
 * @param first Iterator to the beginning of the range
 * @param last Iterator to the end of the range
 */
template <typename RandomIt>
void ParallelSort(RandomIt first, RandomIt last);

// ============================================================================
// Implementation details (declared in parallel.cpp)
// ============================================================================

namespace detail
{

/**
 * Internal implementation of parallel for-loop execution.
 * Called by template wrapper after configuration checks.
 *
 * @param begin Start of range
 * @param end End of range
 * @param grainSize Chunk size for distribution
 * @param taskFunc Type-erased task function
 */
void ParallelForImpl(size_t begin, size_t end, size_t grainSize,
                     void (*taskFunc)(size_t, size_t, void *), void *userData);

}        // namespace detail

// ============================================================================
// Template implementations
// ============================================================================

template <typename Func>
void ParallelFor(size_t begin, size_t end, size_t grainSize, Func &&func)
{
	if (begin >= end)
	{
		return;
	}

	const auto  &config = GetParallelConfig();
	const size_t count  = end - begin;

	// Use default grain size if not specified
	const size_t effectiveGrainSize = (grainSize == 0) ? config.defaultGrainSize : grainSize;

	// Serial fallback conditions
	if (count <= config.minParallelSize || config.maxThreads <= 1 || IsInsideParallelRegion())
	{
		func(begin, end);
		return;
	}

	// Type-erased wrapper for the function
	auto wrapper = [](size_t chunkBegin, size_t chunkEnd, void *data) {
		auto *f = static_cast<std::remove_reference_t<Func> *>(data);
		(*f)(chunkBegin, chunkEnd);
	};

	detail::ParallelForImpl(begin, end, effectiveGrainSize, wrapper,
	                        const_cast<void *>(static_cast<const void *>(&func)));
}

template <typename Func>
void ParallelFor(size_t count, Func &&func)
{
	ParallelFor(static_cast<size_t>(0), count, static_cast<size_t>(0), std::forward<Func>(func));
}

template <typename RandomIt, typename Compare>
void ParallelSort(RandomIt first, RandomIt last, Compare comp)
{
#if MSPLAT_HAS_PARALLEL_EXECUTION
	std::sort(std::execution::par_unseq, first, last, comp);
#else
	std::sort(first, last, comp);
#endif
}

template <typename RandomIt>
void ParallelSort(RandomIt first, RandomIt last)
{
#if MSPLAT_HAS_PARALLEL_EXECUTION
	std::sort(std::execution::par_unseq, first, last);
#else
	std::sort(first, last);
#endif
}

}        // namespace msplat::core
