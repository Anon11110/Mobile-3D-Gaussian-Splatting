#pragma once

#ifdef MSPLAT_HAS_STD_PMR
#	include <memory_resource>
#endif
#include <unordered_map>

#ifdef MSPLAT_USE_STD_CONTAINERS
namespace msplat::container
{

template <typename K, typename V, typename Hash = std::hash<K>, typename Pred = std::equal_to<K>>
using unordered_map = std::unordered_map<K, V, Hash, Pred>;

// Factory functions for system STL compatibility
template <typename K, typename V, typename Hash = std::hash<K>>
inline auto make_unordered_map_default()
    -> unordered_map<K, V, Hash>
{
	return unordered_map<K, V, Hash>{};
}

template <typename K, typename V, typename Hash = std::hash<K>>
inline auto make_unordered_map()
    -> unordered_map<K, V, Hash>
{
	return unordered_map<K, V, Hash>{};
}

}        // namespace msplat::container
#else
#	include "hash.h"
#	include "memory.h"
#	include "third-party/unordered_dense/unordered_dense.h"

namespace msplat::container
{

// Custom unordered_map that always uses:
// 1. ankerl::unordered_dense::map (not std::unordered_map)
// 2. msplat::container::hash (not std::hash)
// 3. PMR allocator with mimalloc backend (not std::allocator)
template <typename K, typename V, typename Hash = msplat::container::hash<K>>
using unordered_map = ankerl::unordered_dense::map<
    K, V, Hash, std::equal_to<K>,
    std::pmr::polymorphic_allocator<std::pair<K, V>>>;

// Factory function to create an unordered_map with default upstream allocator
// Uses the same memory source as our custom vector implementation (mimalloc-based)
template <typename K, typename V, typename Hash = msplat::container::hash<K>>
inline auto make_unordered_map_default()
    -> unordered_map<K, V, Hash>
{
	return unordered_map<K, V, Hash>{
	    std::pmr::polymorphic_allocator<std::pair<K, V>>{pmr::GetUpstreamAllocator()}};
}

// Factory function to create an unordered_map with custom memory resource
template <typename K, typename V, typename Hash = msplat::container::hash<K>>
inline auto make_unordered_map(std::pmr::memory_resource *resource = nullptr)
    -> unordered_map<K, V, Hash>
{
	// Use provided resource or fallback to our global upstream allocator (mimalloc)
	auto *allocator = resource ? resource : pmr::GetUpstreamAllocator();
	return unordered_map<K, V, Hash>{
	    std::pmr::polymorphic_allocator<std::pair<K, V>>{allocator}};
}

}        // namespace msplat::container
#endif