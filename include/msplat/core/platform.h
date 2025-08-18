#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace msplat::core
{

/**
 * Platform-agnostic aligned memory allocation
 *
 * @param size Size in bytes to allocate
 * @param alignment Alignment requirement (must be power of 2)
 * @return Pointer to aligned memory, or nullptr on failure
 */
void *aligned_malloc(size_t size, size_t alignment);

/**
 * Free memory allocated with aligned_malloc
 *
 * @param ptr Pointer to memory allocated with aligned_malloc
 */
void aligned_free(void *ptr);

/**
 * Get system page size in bytes
 *
 * @return Page size in bytes
 */
size_t get_page_size();

/**
 * Get CPU cache line size in bytes for optimization
 *
 * @return Cache line size in bytes
 */
size_t get_cache_line_size();

/**
 * Get current stack backtrace for debugging (debug builds only)
 *
 * @param max_frames Maximum number of frames to capture
 * @return Vector of symbol names/addresses
 */
std::vector<std::string> get_backtrace(int max_frames = 32);

}        // namespace msplat::core