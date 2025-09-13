#pragma once

#include "msplat/core/containers/string.h"
#include "msplat/core/containers/vector.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace msplat::core
{

/**
 * Platform-agnostic aligned memory allocation
 *
 * @param size Size in bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Pointer to aligned memory, or nullptr on failure
 */
void *AlignedMalloc(size_t size, size_t alignment);

/**
 * Free memory allocated with aligned_malloc
 *
 * @param ptr Pointer to memory allocated with aligned_malloc
 */
void AlignedFree(void *ptr);

/**
 * Get system page size in bytes
 *
 * @return Page size in bytes
 */
size_t GetPageSize();

/**
 * Get CPU cache line size in bytes for optimization
 *
 * @return Cache line size in bytes
 */
size_t GetCacheLineSize();

/**
 * Backtrace item containing detailed stack frame information
 */
struct TraceItem
{
	msplat::container::string module;         // Binary/module name
	uint64_t                  address;        // Memory address
	msplat::container::string symbol;         // Function name (demangled if possible)
	size_t                    offset;         // Offset within function
};

/**
 * Convert TraceItem to a formatted string representation
 *
 * @param item The TraceItem to format
 * @return Formatted string representation
 */
[[nodiscard]] std::string ToString(const TraceItem &item) noexcept;

/**
 * Get current stack backtrace for debugging (debug builds only)
 *
 * @param max_frames Maximum number of frames to capture
 * @return Vector of TraceItem structures with detailed frame information
 */
[[nodiscard]] msplat::container::vector<TraceItem> GetBacktrace(int max_frames = 32);

}        // namespace msplat::core