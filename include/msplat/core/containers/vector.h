#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <type_traits>
#include <utility>

// Conditional compilation support
#ifdef MSPLAT_USE_SYSTEM_STL
#	include <vector>
#else
#	include <msplat/core/containers/memory.h>
#endif

namespace msplat::container
{

#ifdef MSPLAT_USE_SYSTEM_STL
// Use standard library vector
template <typename T>
using vector = std::vector<T>;

#else
// Custom vector implementation with allocator integration
// Memory Layout: 32 bytes per vector instance (T* m_data: 8 bytes, m_size: 8 bytes, m_capacity: 8 bytes, Allocator*: 8 bytes)
// Design: Renderer-centric container optimized for 3D Gaussian Splatting data flow patterns
// Growth Strategy: 2x capacity growth, first allocation: 8 elements or requested size
// Performance Focus: Move-first semantics, trivial destructor optimization, zero-cost abstractions
template <typename T>
class vector
{
  private:
	T         *m_data;             // Pointer to elements - contiguous memory for RHI compatibility
	size_t     m_size;             // Number of active elements
	size_t     m_capacity;         // Allocated capacity
	Allocator *m_allocator;        // Non-owning pointer to custom allocator

	// Helper methods
	// Trivial destructor optimization - skip destructor calls for POD types
	// Significant performance improvement for common renderer data (vec3, mat4, etc.)
	void destroy_elements(T *first, T *last)
	{
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (T *it = first; it != last; ++it)
			{
				it->~T();
			}
		}
		// For trivial types, destructors are no-ops
	}

	void deallocate_storage()
	{
		if (m_data && m_allocator)
		{
			m_allocator->deallocate(m_data, m_capacity * sizeof(T));
		}
	}

	T *allocate_storage(size_t capacity)
	{
		if (capacity == 0 || !m_allocator)
		{
			return nullptr;
		}
		void *ptr = m_allocator->allocate(capacity * sizeof(T), alignof(T));
		return static_cast<T *>(ptr);
	}

	// Growth strategy: 2.0x factor balances memory usage vs reallocation frequency
	// Optimized for renderer frame data patterns - minimize reallocations in critical loops
	void grow_capacity(size_t requested_size)
	{
		// Growth strategy: max(2 * capacity, requested_size)
		// First allocation: start with 8 elements or requested size
		size_t new_capacity = m_capacity == 0 ? 8 : m_capacity * 2;
		new_capacity        = std::max(new_capacity, requested_size);

		// Allocate new storage
		T *new_data = allocate_storage(new_capacity);
		if (!new_data)
		{
			// Allocation failed - could throw or handle gracefully
			// For now, we'll leave the vector unchanged
			return;
		}

		// Transfer existing elements to new storage
		if (m_data && m_size > 0)
		{
			transfer_elements(m_data, new_data, m_size);

			// Destroy old elements
			destroy_elements(m_data, m_data + m_size);
		}

		// Deallocate old storage
		deallocate_storage();

		// Update to new storage
		m_data     = new_data;
		m_capacity = new_capacity;
	}

	// Optimized element transfer with trivial type specialization
	// Performance critical: uses memcpy for POD types, move semantics for complex types
	// Exception safety: uses std::move_if_noexcept pattern for strong guarantee
	void transfer_elements(T *source, T *dest, size_t count)
	{
		if constexpr (std::is_trivially_copyable_v<T>)
		{
			// For trivial types, use memcpy for maximum performance
			if (count > 0)
			{
				std::memcpy(dest, source, count * sizeof(T));
			}
		}
		else
		{
			// For non-trivial types, use move/copy construction
			for (size_t i = 0; i < count; ++i)
			{
				if constexpr (std::is_move_constructible_v<T> && std::is_nothrow_move_constructible_v<T>)
				{
					new (dest + i) T(std::move(source[i]));
				}
				else
				{
					new (dest + i) T(source[i]);
				}
			}
		}
	}

  public:
	// Rule of Five implementation for efficient renderer data flow
	// Exception safety: noexcept move operations, strong guarantee for copy operations

	// Default constructor - uses heap allocator if none specified
	explicit vector(Allocator *alloc = nullptr) :
	    m_data(nullptr), m_size(0), m_capacity(0), m_allocator(alloc ? alloc : &get_heap_allocator())
	{
	}

	// Copy constructor
	vector(const vector &other) :
	    m_data(nullptr), m_size(0), m_capacity(0), m_allocator(other.m_allocator)
	{
		if (other.m_size > 0)
		{
			m_data = allocate_storage(other.m_size);
			if (m_data)
			{
				m_capacity = other.m_size;
				// Construct elements using copy constructor
				for (size_t i = 0; i < other.m_size; ++i)
				{
					new (m_data + i) T(other.m_data[i]);
				}
				m_size = other.m_size;
			}
		}
	}

	// Move constructor - O(1) ownership transfer, critical for renderer performance
	// noexcept guarantee enables efficient returns from render passes
	vector(vector &&other) noexcept
	    :
	    m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity), m_allocator(other.m_allocator)
	{
		// Reset other to empty state
		other.m_data     = nullptr;
		other.m_size     = 0;
		other.m_capacity = 0;
	}

	// Copy assignment
	vector &operator=(const vector &other)
	{
		if (this != &other)
		{
			// Clean up existing elements
			destroy_elements(m_data, m_data + m_size);
			deallocate_storage();

			// Copy from other
			m_allocator = other.m_allocator;
			m_size      = 0;
			m_capacity  = 0;
			m_data      = nullptr;

			if (other.m_size > 0)
			{
				m_data = allocate_storage(other.m_size);
				if (m_data)
				{
					m_capacity = other.m_size;
					// Construct elements using copy constructor
					for (size_t i = 0; i < other.m_size; ++i)
					{
						new (m_data + i) T(other.m_data[i]);
					}
					m_size = other.m_size;
				}
			}
		}
		return *this;
	}

	// Move assignment
	vector &operator=(vector &&other) noexcept
	{
		if (this != &other)
		{
			// Clean up existing elements
			destroy_elements(m_data, m_data + m_size);
			deallocate_storage();

			// Move from other
			m_data      = other.m_data;
			m_size      = other.m_size;
			m_capacity  = other.m_capacity;
			m_allocator = other.m_allocator;

			// Reset other to empty state
			other.m_data     = nullptr;
			other.m_size     = 0;
			other.m_capacity = 0;
		}
		return *this;
	}

	// Destructor
	~vector()
	{
		destroy_elements(m_data, m_data + m_size);
		deallocate_storage();
	}

	// Basic accessors - zero overhead compared to raw pointer access
	// data() method critical for RHI interoperability and GPU buffer updates
	T *data()
	{
		return m_data;
	}
	const T *data() const
	{
		return m_data;
	}
	size_t size() const
	{
		return m_size;
	}
	size_t capacity() const
	{
		return m_capacity;
	}
	bool empty() const
	{
		return m_size == 0;
	}

	// Element access
	T &operator[](size_t index)
	{
		assert(index < m_size && "Index out of bounds");
		return m_data[index];
	}

	const T &operator[](size_t index) const
	{
		assert(index < m_size && "Index out of bounds");
		return m_data[index];
	}

	T &front()
	{
		assert(m_size > 0 && "Vector is empty");
		return m_data[0];
	}

	const T &front() const
	{
		assert(m_size > 0 && "Vector is empty");
		return m_data[0];
	}

	T &back()
	{
		assert(m_size > 0 && "Vector is empty");
		return m_data[m_size - 1];
	}

	const T &back() const
	{
		assert(m_size > 0 && "Vector is empty");
		return m_data[m_size - 1];
	}

	// Basic iterators - T* pointers for STL compatibility and range-based for loops
	// Compatible with STL algorithms while maintaining zero overhead
	T *begin()
	{
		return m_data;
	}
	const T *begin() const
	{
		return m_data;
	}
	T *end()
	{
		return m_data + m_size;
	}
	const T *end() const
	{
		return m_data + m_size;
	}

	// Capacity management - most important performance function for renderer
	// Pre-allocation prevents reallocations in time-critical render loops
	void reserve(size_t new_capacity)
	{
		if (new_capacity <= m_capacity)
		{
			return;        // No need to grow
		}

		// Allocate new storage
		T *new_data = allocate_storage(new_capacity);
		if (!new_data)
		{
			return;        // Allocation failed
		}

		// Transfer existing elements to new storage
		if (m_data && m_size > 0)
		{
			transfer_elements(m_data, new_data, m_size);

			// Destroy old elements
			destroy_elements(m_data, m_data + m_size);
		}

		// Deallocate old storage
		deallocate_storage();

		// Update to new storage
		m_data     = new_data;
		m_capacity = new_capacity;
	}

	// Element modifiers
	void push_back(const T &value)
	{
		if (m_size >= m_capacity)
		{
			grow_capacity(m_size + 1);
		}

		if (m_size < m_capacity)        // Check again in case growth failed
		{
			new (m_data + m_size) T(value);
			++m_size;
		}
	}

	void push_back(T &&value)
	{
		if (m_size >= m_capacity)
		{
			grow_capacity(m_size + 1);
		}

		if (m_size < m_capacity)        // Check again in case growth failed
		{
			new (m_data + m_size) T(std::move(value));
			++m_size;
		}
	}

	// Clear - destroys elements but keeps capacity for frame reuse pattern
	// Critical for renderer: avoid deallocation between frames
	void clear()
	{
		destroy_elements(m_data, m_data + m_size);
		m_size = 0;
		// Keep capacity for reuse
	}

	// Advanced capacity operations
	// resize() essential for GPU buffer matching - correct element construction/destruction
	void resize(size_t new_size)
	{
		if (new_size == m_size)
		{
			return;        // No change needed
		}

		if (new_size < m_size)
		{
			// Shrinking: destroy excess elements
			destroy_elements(m_data + new_size, m_data + m_size);
			m_size = new_size;
		}
		else
		{
			// Growing: ensure capacity and construct new elements
			if (new_size > m_capacity)
			{
				grow_capacity(new_size);
			}

			if (new_size <= m_capacity)        // Check capacity is sufficient
			{
				// Default-construct new elements
				for (size_t i = m_size; i < new_size; ++i)
				{
					new (m_data + i) T();
				}
				m_size = new_size;
			}
		}
	}

	void resize(size_t new_size, const T &value)
	{
		if (new_size == m_size)
		{
			return;        // No change needed
		}

		if (new_size < m_size)
		{
			// Shrinking: destroy excess elements
			destroy_elements(m_data + new_size, m_data + m_size);
			m_size = new_size;
		}
		else
		{
			// Growing: ensure capacity and construct new elements with value
			if (new_size > m_capacity)
			{
				grow_capacity(new_size);
			}

			if (new_size <= m_capacity)        // Check capacity is sufficient
			{
				// Copy-construct new elements with provided value
				for (size_t i = m_size; i < new_size; ++i)
				{
					new (m_data + i) T(value);
				}
				m_size = new_size;
			}
		}
	}

	// shrink_to_fit() critical for mobile memory management
	// Release unused capacity after large temporary operations to reduce memory pressure
	void shrink_to_fit()
	{
		if (m_size == m_capacity || m_capacity == 0)
		{
			return;        // Already optimal size or empty
		}

		if (m_size == 0)
		{
			// If empty, deallocate everything
			deallocate_storage();
			m_data     = nullptr;
			m_capacity = 0;
			return;
		}

		// Allocate storage for exactly m_size elements
		T *new_data = allocate_storage(m_size);
		if (!new_data)
		{
			return;        // Allocation failed, keep current storage
		}

		// Transfer elements to new storage
		transfer_elements(m_data, new_data, m_size);

		// Destroy old elements and deallocate
		destroy_elements(m_data, m_data + m_size);
		deallocate_storage();

		// Update to new storage
		m_data     = new_data;
		m_capacity = m_size;
	}
};

#endif        // MSPLAT_USE_SYSTEM_STL

}        // namespace msplat::container