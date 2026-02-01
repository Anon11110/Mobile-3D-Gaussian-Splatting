#pragma once

#include <cstddef>
#include <cstring>
#include <memory>

#include <bit>
#ifdef MSPLAT_HAS_STD_PMR
#	include <memory_resource>
#	include <mimalloc.h>
#endif
#include <span>
#include <type_traits>
#include <utility>

namespace msplat::container
{

// Use std::bit_cast if available, else provide fallback
#if defined(__cpp_lib_bit_cast) && __cpp_lib_bit_cast >= 201806L
using std::bit_cast;
#else
template <typename To, typename From>
inline To bit_cast(const From &src) noexcept
{
	static_assert(sizeof(To) == sizeof(From), "bit_cast requires same size types");
	static_assert(std::is_trivially_copyable_v<To>, "bit_cast requires trivially copyable target");
	static_assert(std::is_trivially_copyable_v<From>, "bit_cast requires trivially copyable source");
	To dst;
	std::memcpy(&dst, &src, sizeof(To));
	return dst;
}
#endif

// Use std::reinterpret_pointer_cast if available, else provide fallback
#if __cplusplus >= 201703L && defined(__cpp_lib_shared_ptr_arrays) && __cpp_lib_shared_ptr_arrays >= 201611L
using std::reinterpret_pointer_cast;
#else
template <typename T, typename U>
std::shared_ptr<T> reinterpret_pointer_cast(const std::shared_ptr<U> &ptr) noexcept
{
	auto p = reinterpret_cast<typename std::shared_ptr<T>::element_type *>(ptr.get());
	return std::shared_ptr<T>(ptr, p);
}
#endif

using std::span;

// Smart pointers
using std::const_pointer_cast;
using std::dynamic_pointer_cast;
using std::enable_shared_from_this;
using std::make_shared;
using std::make_unique;
using std::shared_ptr;
using std::static_pointer_cast;
using std::unique_ptr;
using std::weak_ptr;

// Utility types
using std::aligned_storage_t;
using std::make_pair;

}        // namespace msplat::container

#ifdef MSPLAT_HAS_STD_PMR
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
	void *do_allocate(std::size_t bytes, std::size_t alignment) override
	{
		if (alignment < alignof(void *))
			alignment = alignof(void *);
		void *p = mi_malloc_aligned(bytes == 0 ? 1 : bytes, alignment);
		if (!p)
			throw std::bad_alloc{};
		return p;
	}

	void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override
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
#endif        // MSPLAT_HAS_STD_PMR