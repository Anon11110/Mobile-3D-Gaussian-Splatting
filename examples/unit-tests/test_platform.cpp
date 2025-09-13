#include <cstdint>
#include <cstring>

#include "msplat/core/platform.h"
#include "test_framework.h"
#include <msplat/core/log.h>

TEST(aligned_malloc_basic)
{
	void *ptr = msplat::core::AlignedMalloc(1024, 16);
	if (!ptr)
		return false;

	// Check alignment
	uintptr_t addr    = reinterpret_cast<uintptr_t>(ptr);
	bool      aligned = (addr % 16) == 0;

	msplat::core::AlignedFree(ptr);
	return aligned;
}

TEST(aligned_malloc_large_alignment)
{
	void *ptr = msplat::core::AlignedMalloc(1024, 256);
	if (!ptr)
		return false;

	// Check alignment
	uintptr_t addr    = reinterpret_cast<uintptr_t>(ptr);
	bool      aligned = (addr % 256) == 0;

	msplat::core::AlignedFree(ptr);
	return aligned;
}

TEST(aligned_malloc_zero_size)
{
	void *ptr = msplat::core::AlignedMalloc(0, 16);
	return ptr == nullptr;        // Should return null for zero size
}

TEST(aligned_malloc_invalid_alignment)
{
	void *ptr = msplat::core::AlignedMalloc(1024, 15);        // Not power of 2
	return ptr == nullptr;                                    // Should return null for invalid alignment
}

TEST(aligned_malloc_write_test)
{
	void *ptr = msplat::core::AlignedMalloc(1024, 64);
	if (!ptr)
		return false;

	// Write test pattern
	memset(ptr, 0xAA, 1024);

	// Verify pattern
	uint8_t *bytes   = static_cast<uint8_t *>(ptr);
	bool     success = true;
	for (int i = 0; i < 1024; ++i)
	{
		if (bytes[i] != 0xAA)
		{
			success = false;
			break;
		}
	}

	msplat::core::AlignedFree(ptr);
	return success;
}

TEST(aligned_free_null)
{
	// Should not crash
	msplat::core::AlignedFree(nullptr);
	return true;
}

TEST(get_page_size)
{
	size_t page_size = msplat::core::GetPageSize();

	// Page size should be a power of 2 and at least 1KB
	if (page_size < 1024)
		return false;
	if ((page_size & (page_size - 1)) != 0)
		return false;        // Check power of 2

	LOG_INFO("(page size: {} bytes) ", page_size);
	return true;
}

TEST(get_cache_line_size)
{
	size_t cache_line_size = msplat::core::GetCacheLineSize();

	// Cache line size should be reasonable (16-256 bytes typically)
	if (cache_line_size < 16 || cache_line_size > 256)
		return false;
	if ((cache_line_size & (cache_line_size - 1)) != 0)
		return false;        // Check power of 2

	LOG_INFO("(cache line size: {} bytes) ", cache_line_size);
	return true;
}

TEST(get_backtrace_basic)
{
	auto backtrace = msplat::core::GetBacktrace(10);

// In debug builds should have some frames, in release might be empty
#ifndef NDEBUG
	if (!backtrace.empty())
	{
		// Verify that TraceItem fields are populated
		for (const auto &item : backtrace)
		{
			// At least address should be non-zero
			if (item.address == 0)
				return false;
			// Module and symbol might be "unknown" but should not be empty
			if (item.module.empty() || item.symbol.empty())
				return false;
		}

		// Optionally print first few frames for debugging
		LOG_INFO("Backtrace (first 3 frames):");
		for (size_t i = 0; i < std::min(size_t(3), backtrace.size()); ++i)
		{
			LOG_INFO("  {}", msplat::core::ToString(backtrace[i]));
		}
	}
	return !backtrace.empty();
#else
	return true;        // Always pass in release builds
#endif
}

TEST(get_backtrace_large_request)
{
	auto backtrace = msplat::core::GetBacktrace(1000);

	// Should not crash and should return reasonable number of frames
	return backtrace.size() < 1000;        // Unlikely to have 1000 stack frames
}

// Register all platform tests
void register_platform_tests()
{
	// Tests are automatically registered via static constructors
}