#include <algorithm>
#include <cstring>
#include <memory_resource>
#include <vector>

#include "test_framework.h"
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>

TEST(upstream_allocator_basic)
{
	auto *upstream = msplat::container::pmr::GetUpstreamAllocator();
	if (!upstream)
		return false;

	// Test basic allocation
	void *ptr1 = upstream->allocate(1024, alignof(std::max_align_t));
	if (!ptr1)
		return false;

	void *ptr2 = upstream->allocate(2048, 64);        // Test larger alignment
	if (!ptr2)
		return false;

	// Check alignment
	if (reinterpret_cast<uintptr_t>(ptr1) % alignof(std::max_align_t) != 0)
		return false;
	if (reinterpret_cast<uintptr_t>(ptr2) % 64 != 0)
		return false;

	// Test memory writes
	std::memset(ptr1, 0xAA, 1024);
	std::memset(ptr2, 0xBB, 2048);

	// Cleanup
	upstream->deallocate(ptr1, 1024, alignof(std::max_align_t));
	upstream->deallocate(ptr2, 2048, 64);

	return true;
}

TEST(pmr_vector_basic)
{
	auto                          *upstream = msplat::container::pmr::GetUpstreamAllocator();
	msplat::container::vector<int> vec(upstream);

	// Test basic operations
	if (!vec.empty())
		return false;
	if (vec.size() != 0)
		return false;

	vec.push_back(42);
	if (vec.size() != 1 || vec[0] != 42)
		return false;

	vec.push_back(84);
	vec.push_back(168);
	if (vec.size() != 3)
		return false;

	if (vec[0] != 42 || vec[1] != 84 || vec[2] != 168)
		return false;

	// Test memory resource access
	if (vec.get_memory_resource() != upstream)
		return false;

	return true;
}

TEST(pmr_pool_allocator_basic)
{
	// Test std::pmr::unsynchronized_pool_resource
	std::pmr::unsynchronized_pool_resource pool;

	// Allocate several chunks of the same size
	std::vector<void *> ptrs;
	constexpr size_t    chunkSize = 64;
	constexpr size_t    numChunks = 100;

	for (size_t i = 0; i < numChunks; ++i)
	{
		void *ptr = pool.allocate(chunkSize, alignof(std::max_align_t));
		if (!ptr)
			return false;

		// Test memory write
		std::memset(ptr, static_cast<int>(i & 0xFF), chunkSize);
		ptrs.push_back(ptr);
	}

	// Deallocate some chunks
	for (size_t i = 0; i < numChunks / 2; ++i)
	{
		pool.deallocate(ptrs[i], chunkSize, alignof(std::max_align_t));
	}

	// Allocate again - should reuse deallocated chunks
	for (size_t i = 0; i < numChunks / 2; ++i)
	{
		void *ptr = pool.allocate(chunkSize, alignof(std::max_align_t));
		if (!ptr)
			return false;
		ptrs[i] = ptr;
	}

	// Cleanup remaining
	for (size_t i = 0; i < numChunks; ++i)
	{
		pool.deallocate(ptrs[i], chunkSize, alignof(std::max_align_t));
	}

	return true;
}

TEST(pmr_vector_move_semantics)
{
	auto *upstream = msplat::container::pmr::GetUpstreamAllocator();

	// Create and populate vector
	msplat::container::vector<int> vec1(upstream);
	for (int i = 0; i < 100; ++i)
	{
		vec1.push_back(i);
	}

	// Test move constructor
	msplat::container::vector<int> vec2 = std::move(vec1);

	if (vec2.size() != 100)
		return false;
	if (!vec1.empty())        // vec1 should be in valid but empty state
		return false;

	// Verify data moved correctly
	for (size_t i = 0; i < vec2.size(); ++i)
	{
		if (vec2[i] != static_cast<int>(i))
			return false;
	}

	// Test move assignment
	msplat::container::vector<int> vec3(upstream);
	vec3 = std::move(vec2);

	if (vec3.size() != 100)
		return false;
	if (!vec2.empty())
		return false;

	return true;
}

// Register all PMR allocator tests
void register_pmr_allocator_tests()
{
}