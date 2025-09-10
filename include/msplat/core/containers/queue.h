#pragma once

#include <deque>
#include <memory_resource>
#include <msplat/core/containers/memory.h>
#include <queue>

namespace msplat::container
{

#ifdef MSPLAT_USE_SYSTEM_STL
// Use standard library queue
template <typename T, typename Container = std::deque<T>>
using queue = std::queue<T, Container>;

#else
// Custom PMR-based queue implementation using std::queue with PMR deque
template <typename T>
using pmr_deque = std::deque<T, std::pmr::polymorphic_allocator<T>>;

template <typename T, typename Container = pmr_deque<T>>
class queue : public std::queue<T, Container>
{
  private:
	using base_type = std::queue<T, Container>;

  public:
	// Default constructor - uses upstream allocator
	explicit queue(std::pmr::memory_resource *memres = nullptr)
	{
		if constexpr (std::is_same_v<Container, pmr_deque<T>>)
		{
			// Construct with PMR allocator
			auto alloc = std::pmr::polymorphic_allocator<T>(memres ? memres : pmr::GetUpstreamAllocator());
			this->c    = Container(alloc);
		}
	}

	// Constructor with container
	explicit queue(const Container &cont) :
	    base_type(cont)
	{
	}

	// Move constructor with container
	explicit queue(Container &&cont) :
	    base_type(std::move(cont))
	{
	}

	// Copy constructor
	queue(const queue &other) :
	    base_type(other)
	{
	}

	// Move constructor
	queue(queue &&other) noexcept :
	    base_type(std::move(other))
	{
	}

	// Copy assignment
	queue &operator=(const queue &other)
	{
		base_type::operator=(other);
		return *this;
	}

	// Move assignment
	queue &operator=(queue &&other) noexcept
	{
		base_type::operator=(std::move(other));
		return *this;
	}

	// Get memory resource (if using PMR deque)
	std::pmr::memory_resource *get_memory_resource() const noexcept
	{
		if constexpr (std::is_same_v<Container, pmr_deque<T>>)
		{
			return this->c.get_allocator().resource();
		}
		else
		{
			return nullptr;
		}
	}
};

#endif        // MSPLAT_USE_SYSTEM_STL

}        // namespace msplat::container