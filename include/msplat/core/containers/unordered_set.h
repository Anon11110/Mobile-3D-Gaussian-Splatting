#pragma once

#if !defined(__ANDROID__)
#	include <memory_resource>
#endif
#include <unordered_set>

#ifdef MSPLAT_USE_STD_CONTAINERS
namespace msplat::container
{

template <typename T, typename Hash = std::hash<T>, typename Pred = std::equal_to<T>>
using unordered_set = std::unordered_set<T, Hash, Pred>;

}
#else
#	include "hash.h"
#	include "memory.h"
#	include <msplat/core/memory/frame_arena.h>
#	include "third-party/unordered_dense/unordered_dense.h"

namespace msplat::container
{

// Custom unordered_set that always uses:
// 1. ankerl::unordered_dense::set (not std::unordered_set)
// 2. msplat::container::hash (not std::hash)
// 3. PMR allocator with mimalloc backend (not std::allocator)
template <typename T, typename Hash = msplat::container::hash<T>>
using unordered_set = ankerl::unordered_dense::set<
    T, Hash, std::equal_to<T>,
    std::pmr::polymorphic_allocator<T>>;

// Factory function to create an unordered_set with default upstream allocator
// Uses the same memory source as our custom vector implementation (mimalloc-based)
template <typename T, typename Hash = msplat::container::hash<T>>
inline auto make_unordered_set_default()
    -> unordered_set<T, Hash>
{
	return unordered_set<T, Hash>{
	    std::pmr::polymorphic_allocator<T>{pmr::GetUpstreamAllocator()}};
}

// Factory function to create an unordered_set with custom memory resource
template <typename T, typename Hash = msplat::container::hash<T>>
inline auto make_unordered_set(std::pmr::memory_resource *resource = nullptr)
    -> unordered_set<T, Hash>
{
	// Use provided resource or fallback to our global upstream allocator (mimalloc)
	auto *allocator = resource ? resource : pmr::GetUpstreamAllocator();
	return unordered_set<T, Hash>{
	    std::pmr::polymorphic_allocator<T>{allocator}};
}

// Factory function for creating an unordered_set with frame arena allocation
// Useful for temporary/per-frame data that doesn't need to persist
template <typename T, typename Hash = msplat::container::hash<T>>
inline auto make_frame_unordered_set(pmr::FrameArena &arena)
    -> unordered_set<T, Hash>
{
	return unordered_set<T, Hash>{
	    std::pmr::polymorphic_allocator<T>{arena.Resource()}};
}

}        // namespace msplat::container
#endif