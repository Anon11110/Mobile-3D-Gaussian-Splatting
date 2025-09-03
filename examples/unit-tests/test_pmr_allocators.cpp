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

TEST(frame_arena_basic)
{
	msplat::container::pmr::FrameArena frameArena;
	auto                              *resource = frameArena.Resource();

	if (!resource)
		return false;

	// Test basic allocation from arena
	void *ptr1 = resource->allocate(100, alignof(int));
	if (!ptr1)
		return false;

	void *ptr2 = resource->allocate(200, alignof(double));
	if (!ptr2)
		return false;

	// Test memory writes
	std::memset(ptr1, 0xCC, 100);
	std::memset(ptr2, 0xDD, 200);

	// Test frame cycling
	frameArena.BeginFrame();
	auto *resource2 = frameArena.Resource();

	// Should be different buffer now
	void *ptr3 = resource2->allocate(50, alignof(int));
	if (!ptr3)
		return false;

	// The deallocate calls are no-op for monotonic buffer resource
	// Memory is reclaimed when BeginFrame() cycles through buffers
	resource->deallocate(ptr1, 100, alignof(int));
	resource->deallocate(ptr2, 200, alignof(double));
	resource2->deallocate(ptr3, 50, alignof(int));

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

TEST(pmr_vector_with_frame_arena)
{
	msplat::container::pmr::FrameArena frameArena;
	msplat::container::vector<float>   vec(frameArena.Resource());

	// Fill vector with data
	for (int i = 0; i < 1000; ++i)
	{
		vec.push_back(static_cast<float>(i) * 0.5f);
	}

	if (vec.size() != 1000)
		return false;

	// Verify data
	for (size_t i = 0; i < vec.size(); ++i)
	{
		float expected = static_cast<float>(i) * 0.5f;
		if (std::abs(vec[i] - expected) > 1e-6f)
			return false;
	}

	// Test iteration
	int count = 0;
	for (const auto &val : vec)
	{
		float expected = static_cast<float>(count) * 0.5f;
		if (std::abs(val - expected) > 1e-6f)
			return false;
		++count;
	}

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

TEST(frame_arena_triple_buffering)
{
	msplat::container::pmr::FrameArena frameArena;

	// Get pointers to resources from different frames
	std::pmr::memory_resource *frame0 = frameArena.Resource();

	frameArena.BeginFrame();
	std::pmr::memory_resource *frame1 = frameArena.Resource();

	frameArena.BeginFrame();
	std::pmr::memory_resource *frame2 = frameArena.Resource();

	frameArena.BeginFrame();
	std::pmr::memory_resource *frame3 = frameArena.Resource();

	// frame3 should be the same as frame0 (triple buffering cycles)
	if (frame3 != frame0)
		return false;

	// All three buffers should be different
	if (frame0 == frame1 || frame1 == frame2 || frame0 == frame2)
		return false;

	return true;
}

TEST(frame_arena_configurable)
{
	// Test custom configuration: double-buffered, 1MB arenas
	msplat::container::pmr::FrameArenaResource<2> customArena(1024 * 1024);

	// Verify configuration
	if (customArena.GetNumArenas() != 2)
		return false;
	if (customArena.GetArenaSize() != 1024 * 1024)
		return false;

	// Test basic functionality
	auto *resource = customArena.Resource();
	if (!resource)
		return false;

	// Allocate something
	void *ptr = resource->allocate(1000, alignof(int));
	if (!ptr)
		return false;

	// Test frame cycling with double-buffering
	std::pmr::memory_resource *frame0 = customArena.Resource();

	customArena.BeginFrame();
	std::pmr::memory_resource *frame1 = customArena.Resource();

	customArena.BeginFrame();
	std::pmr::memory_resource *frame2 = customArena.Resource();

	// With double-buffering, frame2 should be same as frame0
	if (frame2 != frame0)
		return false;
	if (frame0 == frame1)        // But frame0 and frame1 should be different
		return false;

	return true;
}

// Register all PMR allocator tests
void register_pmr_allocator_tests()
{
}