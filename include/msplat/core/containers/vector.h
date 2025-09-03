#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <memory_resource>
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
// Custom PMR-based vector implementation
//
// Memory Layout - vector<T> object:
// +------------+----------+------------+------------------+
// | T* mData  | mSize   | mCapacity | memory_resource* |
// | 8 bytes    | 8 bytes  | 8 bytes    | 8 bytes          |
// +------------+----------+------------+------------------+
// Total: 32 bytes per vector instance
//
// Design: Renderer-centric container optimized for 3D Gaussian Splatting data flow patterns
// Growth Strategy: 2x capacity growth, first allocation: 8 elements or requested size
// Performance Focus: Move-first semantics, trivial destructor optimization, zero-cost abstractions
template <typename T>
class vector
{
  private:
	T                         *mData;                  // Pointer to elements - contiguous memory for RHI compatibility
	size_t                     mSize;                  // Number of active elements
	size_t                     mCapacity;              // Allocated capacity
	std::pmr::memory_resource *mMemoryResource;        // Non-owning pointer to memory resource

	// Helper methods
	// Trivial destructor optimization - skip destructor calls for POD types
	// Significant performance improvement for common renderer data (vec3, mat4, etc.)
	void destroy_elements(T *first, T *last)
	{
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (T *ptr = first; ptr != last; ++ptr)
			{
				ptr->~T();
			}
		}
	}

	void deallocate_storage()
	{
		if (mData && mMemoryResource)
		{
			mMemoryResource->deallocate(mData, mCapacity * sizeof(T), alignof(T));
		}
	}

	void allocate_storage(size_t capacity)
	{
		if (capacity == 0 || !mMemoryResource)
		{
			return;
		}

		void *ptr = mMemoryResource->allocate(capacity * sizeof(T), alignof(T));
		if (!ptr)
		{
			throw std::bad_alloc();
		}

		mData     = static_cast<T *>(ptr);
		mCapacity = capacity;
	}

  public:
	// Rule of Five implementation for efficient renderer data flow
	// Exception safety: noexcept move operations, strong guarantee for copy operations

	// Default constructor - uses upstream allocator if none specified
	explicit vector(std::pmr::memory_resource *memres = nullptr) :
	    mData(nullptr), mSize(0), mCapacity(0), mMemoryResource(memres ? memres : pmr::GetUpstreamAllocator())
	{
	}

	// Copy constructor
	vector(const vector &other) :
	    mData(nullptr), mSize(0), mCapacity(0), mMemoryResource(other.mMemoryResource)
	{
		if (other.mSize > 0)
		{
			allocate_storage(other.mSize);
			if constexpr (std::is_trivially_copyable_v<T>)
			{
				std::memcpy(mData, other.mData, other.mSize * sizeof(T));
			}
			else
			{
				std::uninitialized_copy_n(other.mData, other.mSize, mData);
			}
			mSize = other.mSize;
		}
	}

	// Move constructor - noexcept for performance
	vector(vector &&other) noexcept :
	    mData(other.mData), mSize(other.mSize), mCapacity(other.mCapacity), mMemoryResource(other.mMemoryResource)
	{
		other.mData           = nullptr;
		other.mSize           = 0;
		other.mCapacity       = 0;
		other.mMemoryResource = pmr::GetUpstreamAllocator();
	}

	// Destructor
	~vector()
	{
		destroy_elements(mData, mData + mSize);
		deallocate_storage();
	}

	// Copy assignment
	vector &operator=(const vector &other)
	{
		if (this != &other)
		{
			// Strong exception safety
			vector temp(other);
			*this = std::move(temp);
		}
		return *this;
	}

	// Move assignment - noexcept for performance
	vector &operator=(vector &&other) noexcept
	{
		if (this != &other)
		{
			// Clean up current resources
			destroy_elements(mData, mData + mSize);
			deallocate_storage();

			// Move data
			mData           = other.mData;
			mSize           = other.mSize;
			mCapacity       = other.mCapacity;
			mMemoryResource = other.mMemoryResource;

			// Leave other in valid state
			other.mData           = nullptr;
			other.mSize           = 0;
			other.mCapacity       = 0;
			other.mMemoryResource = pmr::GetUpstreamAllocator();
		}
		return *this;
	}

	// Capacity management
	void reserve(size_t new_capacity)
	{
		if (new_capacity <= mCapacity)
		{
			return;
		}

		T     *new_data     = nullptr;
		size_t old_capacity = mCapacity;

		// Allocate new storage
		void *ptr = mMemoryResource->allocate(new_capacity * sizeof(T), alignof(T));
		if (!ptr)
		{
			throw std::bad_alloc();
		}
		new_data = static_cast<T *>(ptr);

		// Move/copy existing elements
		if (mData && mSize > 0)
		{
			if constexpr (std::is_nothrow_move_constructible_v<T>)
			{
				std::uninitialized_move_n(mData, mSize, new_data);
			}
			else if constexpr (std::is_copy_constructible_v<T>)
			{
				try
				{
					std::uninitialized_copy_n(mData, mSize, new_data);
				}
				catch (...)
				{
					mMemoryResource->deallocate(new_data, new_capacity * sizeof(T), alignof(T));
					throw;
				}
			}
			else
			{
				static_assert(std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>,
				              "T must be move or copy constructible");
			}

			// Destroy old elements and deallocate
			destroy_elements(mData, mData + mSize);
			mMemoryResource->deallocate(mData, old_capacity * sizeof(T), alignof(T));
		}

		mData     = new_data;
		mCapacity = new_capacity;
	}

	void resize(size_t new_size)
	{
		if (new_size < mSize)
		{
			// Shrink: destroy excess elements
			destroy_elements(mData + new_size, mData + mSize);
		}
		else if (new_size > mSize)
		{
			// Grow: ensure capacity and default-construct new elements
			if (new_size > mCapacity)
			{
				reserve(std::max(new_size, mCapacity * 2));
			}

			// Default-construct new elements
			if constexpr (std::is_default_constructible_v<T>)
			{
				std::uninitialized_value_construct_n(mData + mSize, new_size - mSize);
			}
			else
			{
				static_assert(std::is_default_constructible_v<T>, "T must be default constructible for resize");
			}
		}

		mSize = new_size;
	}

	void resize(size_t new_size, const T &value)
	{
		if (new_size < mSize)
		{
			// Shrink: destroy excess elements
			destroy_elements(mData + new_size, mData + mSize);
		}
		else if (new_size > mSize)
		{
			// Grow: ensure capacity and construct new elements with value
			if (new_size > mCapacity)
			{
				reserve(std::max(new_size, mCapacity * 2));
			}

			// Construct new elements with provided value
			std::uninitialized_fill_n(mData + mSize, new_size - mSize, value);
		}

		mSize = new_size;
	}

	// Element access
	T &operator[](size_t index) noexcept
	{
		assert(index < mSize);
		return mData[index];
	}

	const T &operator[](size_t index) const noexcept
	{
		assert(index < mSize);
		return mData[index];
	}

	T &at(size_t index)
	{
		if (index >= mSize)
		{
			throw std::out_of_range("vector::at: index out of range");
		}
		return mData[index];
	}

	const T &at(size_t index) const
	{
		if (index >= mSize)
		{
			throw std::out_of_range("vector::at: index out of range");
		}
		return mData[index];
	}

	T &front() noexcept
	{
		assert(mSize > 0);
		return mData[0];
	}

	const T &front() const noexcept
	{
		assert(mSize > 0);
		return mData[0];
	}

	T &back() noexcept
	{
		assert(mSize > 0);
		return mData[mSize - 1];
	}

	const T &back() const noexcept
	{
		assert(mSize > 0);
		return mData[mSize - 1];
	}

	T *data() noexcept
	{
		return mData;
	}

	const T *data() const noexcept
	{
		return mData;
	}

	// Iterators
	T *begin() noexcept
	{
		return mData;
	}

	const T *begin() const noexcept
	{
		return mData;
	}

	const T *cbegin() const noexcept
	{
		return mData;
	}

	T *end() noexcept
	{
		return mData + mSize;
	}

	const T *end() const noexcept
	{
		return mData + mSize;
	}

	const T *cend() const noexcept
	{
		return mData + mSize;
	}

	// Capacity queries
	size_t size() const noexcept
	{
		return mSize;
	}

	size_t capacity() const noexcept
	{
		return mCapacity;
	}

	bool empty() const noexcept
	{
		return mSize == 0;
	}

	// Modifiers
	void push_back(const T &value)
	{
		if (mSize >= mCapacity)
		{
			size_t new_capacity = (mCapacity == 0) ? 8 : mCapacity * 2;
			reserve(new_capacity);
		}

		new (mData + mSize) T(value);
		++mSize;
	}

	void push_back(T &&value)
	{
		if (mSize >= mCapacity)
		{
			size_t new_capacity = (mCapacity == 0) ? 8 : mCapacity * 2;
			reserve(new_capacity);
		}

		new (mData + mSize) T(std::move(value));
		++mSize;
	}

	template <typename... Args>
	T &emplace_back(Args &&...args)
	{
		if (mSize >= mCapacity)
		{
			size_t new_capacity = (mCapacity == 0) ? 8 : mCapacity * 2;
			reserve(new_capacity);
		}

		T *new_element = new (mData + mSize) T(std::forward<Args>(args)...);
		++mSize;
		return *new_element;
	}

	void pop_back() noexcept
	{
		assert(mSize > 0);
		--mSize;
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			mData[mSize].~T();
		}
	}

	void clear() noexcept
	{
		destroy_elements(mData, mData + mSize);
		mSize = 0;
	}

	void shrink_to_fit()
	{
		if (mSize < mCapacity)
		{
			if (mSize == 0)
			{
				deallocate_storage();
				mData     = nullptr;
				mCapacity = 0;
			}
			else
			{
				// Allocate exactly what we need
				void *ptr = mMemoryResource->allocate(mSize * sizeof(T), alignof(T));
				if (!ptr)
				{
					return;        // Shrinking is optional, continue with old allocation
				}

				T *new_data = static_cast<T *>(ptr);

				// Move elements to new storage
				if constexpr (std::is_nothrow_move_constructible_v<T>)
				{
					std::uninitialized_move_n(mData, mSize, new_data);
				}
				else
				{
					try
					{
						std::uninitialized_copy_n(mData, mSize, new_data);
					}
					catch (...)
					{
						mMemoryResource->deallocate(new_data, mSize * sizeof(T), alignof(T));
						return;        // Keep old allocation on failure
					}
				}

				// Clean up old storage
				destroy_elements(mData, mData + mSize);
				deallocate_storage();

				mData     = new_data;
				mCapacity = mSize;
			}
		}
	}

	// Memory resource access
	std::pmr::memory_resource *get_memory_resource() const noexcept
	{
		return mMemoryResource;
	}
};

#endif        // MSPLAT_USE_SYSTEM_STL

}        // namespace msplat::container