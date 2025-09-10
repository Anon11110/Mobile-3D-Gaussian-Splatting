#pragma once

#include <atomic>
#include <bit>        // for bit_cast (C++20)
#include <memory>
#include <memory_resource>
#include <mimalloc.h>
#include <span>               // for span (C++20)
#include <type_traits>        // for aligned_storage_t
#include <utility>            // for pair, make_pair

namespace msplat::container
{

// Conditional compilation support - using std:: for both cases
#ifdef MSPLAT_USE_SYSTEM_STL
// Memory utilities
using std::bit_cast;
using std::span;

// Smart pointers
using std::const_pointer_cast;
using std::dynamic_pointer_cast;
using std::enable_shared_from_this;
using std::make_shared;
using std::make_unique;
using std::reinterpret_pointer_cast;
using std::shared_ptr;
using std::static_pointer_cast;
using std::unique_ptr;
using std::weak_ptr;

// Utility types
using std::aligned_storage_t;
using std::make_pair;

#else
// Custom implementation - still using std:: types
using std::bit_cast;
using std::span;

// Smart pointers
using std::const_pointer_cast;
using std::dynamic_pointer_cast;
using std::enable_shared_from_this;
using std::make_shared;
using std::make_unique;
using std::reinterpret_pointer_cast;
using std::shared_ptr;
using std::static_pointer_cast;
using std::unique_ptr;
using std::weak_ptr;

// Utility types
using std::aligned_storage_t;
using std::make_pair;

#endif

}        // namespace msplat::container

namespace msplat::container::pmr
{

/**
 * The root memory source, wrapping mimalloc.
 * This is the application's interface to the OS heap and serves as the
 * ultimate fallback for all other allocators.
 */
class UpstreamAllocator : public std::pmr::memory_resource
{
  protected:
	void *do_allocate(size_t bytes, size_t alignment) override
	{
		return mi_malloc_aligned(bytes, alignment);
	}

	void do_deallocate(void *p, size_t bytes, size_t alignment) override
	{
		mi_free(p);
	}

	bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
	{
		// All UpstreamAllocators are considered equal as it's a singleton.
		return dynamic_cast<const UpstreamAllocator *>(&other) != nullptr;
	}
};

/**
 * Singleton accessor for the single upstream allocator instance.
 */
inline std::pmr::memory_resource *GetUpstreamAllocator()
{
	static UpstreamAllocator instance;
	return &instance;
}

}        // namespace msplat::container::pmr