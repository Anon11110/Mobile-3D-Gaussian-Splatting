#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Conditional compilation support
#ifdef MSPLAT_USE_STD_CONTAINERS
#	include <array>
#endif

namespace msplat::container
{

#ifdef MSPLAT_USE_STD_CONTAINERS
// Use standard library array
template <typename T, size_t N>
using array = std::array<T, N>;

#else
// Custom array implementation optimized for mobile rendering
//
// Memory Layout:
// +----------------+
// | T mData[N]     |
// +----------------+
// Total: sizeof(T) * N bytes
//
// Design: Fixed-size container optimized for mobile GPU data patterns
// Features: Aggregate initialization, constexpr operations, zero-overhead abstraction
// Mobile Focus: Cache-line awareness, prefetch hints for streaming access
template <typename T, size_t N>
struct array
{
  public:
	using value_type             = T;
	using size_type              = size_t;
	using difference_type        = ptrdiff_t;
	using reference              = T &;
	using const_reference        = const T &;
	using pointer                = T *;
	using const_pointer          = const T *;
	using iterator               = T *;
	using const_iterator         = const T *;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	// Public data member for aggregate initialization
	// This allows: array<int, 5> a = {{1, 2, 3, 4, 5}};
	T mData[N];

	// Note: No constructors/destructors/assignment operators
	// This keeps the type an aggregate and enables constexpr usage

	// Fill operations
	constexpr void fill(const T &value)
	{
		for (size_t i = 0; i < N; ++i)
		{
			mData[i] = value;
		}
	}

	// Swap - linear time operation
	constexpr void swap(array &other) noexcept(std::is_nothrow_swappable_v<T>)
	{
		std::swap_ranges(mData, mData + N, other.mData);
	}

	// Iterators
	constexpr iterator begin() noexcept
	{
		return mData;
	}
	constexpr const_iterator begin() const noexcept
	{
		return mData;
	}
	constexpr const_iterator cbegin() const noexcept
	{
		return mData;
	}

	constexpr iterator end() noexcept
	{
		return mData + N;
	}
	constexpr const_iterator end() const noexcept
	{
		return mData + N;
	}
	constexpr const_iterator cend() const noexcept
	{
		return mData + N;
	}

	// Reverse iterators
	constexpr reverse_iterator rbegin() noexcept
	{
		return reverse_iterator(end());
	}
	constexpr const_reverse_iterator rbegin() const noexcept
	{
		return const_reverse_iterator(end());
	}
	constexpr const_reverse_iterator crbegin() const noexcept
	{
		return const_reverse_iterator(end());
	}

	constexpr reverse_iterator rend() noexcept
	{
		return reverse_iterator(begin());
	}
	constexpr const_reverse_iterator rend() const noexcept
	{
		return const_reverse_iterator(begin());
	}
	constexpr const_reverse_iterator crend() const noexcept
	{
		return const_reverse_iterator(begin());
	}

	// Size operations
	constexpr size_type size() const noexcept
	{
		return N;
	}
	constexpr size_type max_size() const noexcept
	{
		return N;
	}
	constexpr bool empty() const noexcept
	{
		return N == 0;
	}

	// Element access
	constexpr reference operator[](size_type pos) noexcept
	{
		assert(pos < N);
		return mData[pos];
	}
	constexpr const_reference operator[](size_type pos) const noexcept
	{
		assert(pos < N);
		return mData[pos];
	}

	constexpr reference at(size_type pos)
	{
		if (pos >= N)
		{
			throw std::out_of_range("array::at: index out of range");
		}
		return mData[pos];
	}
	constexpr const_reference at(size_type pos) const
	{
		if (pos >= N)
		{
			throw std::out_of_range("array::at: index out of range");
		}
		return mData[pos];
	}

	constexpr reference front() noexcept
	{
		assert(N > 0);
		return mData[0];
	}
	constexpr const_reference front() const noexcept
	{
		assert(N > 0);
		return mData[0];
	}

	constexpr reference back() noexcept
	{
		assert(N > 0);
		return mData[N - 1];
	}
	constexpr const_reference back() const noexcept
	{
		assert(N > 0);
		return mData[N - 1];
	}

	// Data access
	constexpr T *data() noexcept
	{
		return mData;
	}
	constexpr const T *data() const noexcept
	{
		return mData;
	}

	// Mobile-optimized features
	// Prefetch hint for streaming access patterns
	void prefetch_read() const noexcept
	{
#	ifdef __builtin_prefetch
		__builtin_prefetch(mData, 0, 3);        // Read, high temporal locality
#	endif
	}

	void prefetch_write() noexcept
	{
#	ifdef __builtin_prefetch
		__builtin_prefetch(mData, 1, 3);        // Write, high temporal locality
#	endif
	}

	// Validation (debug builds)
	bool validate() const noexcept
	{
		return true;        // Fixed size array is always valid
	}

	int validate_iterator(const_iterator it) const noexcept
	{
		if (it >= mData && it <= mData + N)
		{
			return 1;        // Valid iterator
		}
		return 0;        // Invalid iterator
	}
};

// Specialization for zero-size arrays
template <typename T>
struct array<T, 0>
{
  public:
	using value_type             = T;
	using size_type              = size_t;
	using difference_type        = ptrdiff_t;
	using reference              = T &;
	using const_reference        = const T &;
	using pointer                = T *;
	using const_pointer          = const T *;
	using iterator               = T *;
	using const_iterator         = const T *;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	// No data member for zero-size array

	constexpr void fill(const T &) noexcept
	{}
	constexpr void swap(array &) noexcept
	{}

	// Iterators all return nullptr
	constexpr iterator begin() noexcept
	{
		return nullptr;
	}
	constexpr const_iterator begin() const noexcept
	{
		return nullptr;
	}
	constexpr const_iterator cbegin() const noexcept
	{
		return nullptr;
	}

	constexpr iterator end() noexcept
	{
		return nullptr;
	}
	constexpr const_iterator end() const noexcept
	{
		return nullptr;
	}
	constexpr const_iterator cend() const noexcept
	{
		return nullptr;
	}

	constexpr reverse_iterator rbegin() noexcept
	{
		return reverse_iterator(nullptr);
	}
	constexpr const_reverse_iterator rbegin() const noexcept
	{
		return const_reverse_iterator(nullptr);
	}
	constexpr const_reverse_iterator crbegin() const noexcept
	{
		return const_reverse_iterator(nullptr);
	}

	constexpr reverse_iterator rend() noexcept
	{
		return reverse_iterator(nullptr);
	}
	constexpr const_reverse_iterator rend() const noexcept
	{
		return const_reverse_iterator(nullptr);
	}
	constexpr const_reverse_iterator crend() const noexcept
	{
		return const_reverse_iterator(nullptr);
	}

	constexpr size_type size() const noexcept
	{
		return 0;
	}
	constexpr size_type max_size() const noexcept
	{
		return 0;
	}
	constexpr bool empty() const noexcept
	{
		return true;
	}

	constexpr reference operator[](size_type) noexcept
	{
		assert(false && "array<T, 0>::operator[] called");
		return *static_cast<T *>(nullptr);
	}
	constexpr const_reference operator[](size_type) const noexcept
	{
		assert(false && "array<T, 0>::operator[] called");
		return *static_cast<const T *>(nullptr);
	}

	constexpr reference at(size_type)
	{
		throw std::out_of_range("array<T, 0>::at: index out of range");
	}
	constexpr const_reference at(size_type) const
	{
		throw std::out_of_range("array<T, 0>::at: index out of range");
	}

	constexpr reference front() noexcept
	{
		assert(false && "array<T, 0>::front called");
		return *static_cast<T *>(nullptr);
	}
	constexpr const_reference front() const noexcept
	{
		assert(false && "array<T, 0>::front called");
		return *static_cast<const T *>(nullptr);
	}

	constexpr reference back() noexcept
	{
		assert(false && "array<T, 0>::back called");
		return *static_cast<T *>(nullptr);
	}
	constexpr const_reference back() const noexcept
	{
		assert(false && "array<T, 0>::back called");
		return *static_cast<const T *>(nullptr);
	}

	constexpr T *data() noexcept
	{
		return nullptr;
	}
	constexpr const T *data() const noexcept
	{
		return nullptr;
	}

	void prefetch_read() const noexcept
	{}
	void prefetch_write() noexcept
	{}

	bool validate() const noexcept
	{
		return true;
	}
	int validate_iterator(const_iterator) const noexcept
	{
		return 0;
	}
};

// Deduction guides (C++17)
#	if __cplusplus >= 201703L && !defined(MSPLAT_USE_STD_CONTAINERS)
template <class T, class... U>
array(T, U...) -> array<T, 1 + sizeof...(U)>;
#	endif

// Comparison operators
template <typename T, size_t N>
constexpr bool operator==(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

template <typename T, size_t N>
constexpr bool operator!=(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return !(lhs == rhs);
}

template <typename T, size_t N>
constexpr bool operator<(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return std::lexicographical_compare(lhs.begin(), lhs.end(), rhs.begin(), rhs.end());
}

template <typename T, size_t N>
constexpr bool operator<=(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return !(rhs < lhs);
}

template <typename T, size_t N>
constexpr bool operator>(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return rhs < lhs;
}

template <typename T, size_t N>
constexpr bool operator>=(const array<T, N> &lhs, const array<T, N> &rhs)
{
	return !(lhs < rhs);
}

// Non-member swap
template <typename T, size_t N>
constexpr void swap(array<T, N> &lhs, array<T, N> &rhs) noexcept(noexcept(lhs.swap(rhs)))
{
	lhs.swap(rhs);
}

// Structured binding support - get functions
template <size_t I, typename T, size_t N>
constexpr T &get(array<T, N> &arr) noexcept
{
	static_assert(I < N, "array index out of bounds");
	return arr.mData[I];
}

template <size_t I, typename T, size_t N>
constexpr T &&get(array<T, N> &&arr) noexcept
{
	static_assert(I < N, "array index out of bounds");
	return std::move(arr.mData[I]);
}

template <size_t I, typename T, size_t N>
constexpr const T &get(const array<T, N> &arr) noexcept
{
	static_assert(I < N, "array index out of bounds");
	return arr.mData[I];
}

template <size_t I, typename T, size_t N>
constexpr const T &&get(const array<T, N> &&arr) noexcept
{
	static_assert(I < N, "array index out of bounds");
	return std::move(arr.mData[I]);
}

#endif        // MSPLAT_USE_STD_CONTAINERS

}        // namespace msplat::container

// Structured binding support (C++17)
#if __cplusplus >= 201703L && !defined(MSPLAT_USE_STD_CONTAINERS)
namespace std
{
template <typename T, size_t N>
struct tuple_size<msplat::container::array<T, N>> : std::integral_constant<size_t, N>
{
};

template <size_t I, typename T, size_t N>
struct tuple_element<I, msplat::container::array<T, N>>
{
	using type = T;
};
}        // namespace std
#endif