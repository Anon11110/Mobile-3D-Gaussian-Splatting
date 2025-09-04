#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

// Conditional compilation support
#ifdef MSPLAT_USE_SYSTEM_STL
#	include <functional>
#else
#	include "third-party/rapidhash/rapidhash.h"
#endif

namespace msplat::container
{

#ifdef MSPLAT_USE_SYSTEM_STL
// Use standard library hash
template <typename T>
using hash = std::hash<T>;

#else
// Custom RapidHash-based implementation

// Primary template for hash - uses RapidHash for generic types
template <typename T, typename Enable = void>
struct hash
{
	// Mark as avalanching for compatibility with ankerl::unordered_dense
	using is_avalanching = void;

	std::size_t operator()(const T &key) const noexcept
	{
		// For generic types, hash the raw bytes
		return rapidhash(&key, sizeof(T));
	}
};

// Specialization for arithmetic types
template <typename T>
struct hash<T, std::enable_if_t<std::is_arithmetic_v<T>>>
{
	using is_avalanching = void;

	std::size_t operator()(T key) const noexcept
	{
		// For types that fit in uint64_t, use rapid_mix directly for optimal performance
		// This avoids the overhead of buffer hashing for simple integer types
		if constexpr (sizeof(T) <= sizeof(uint64_t))
		{
			// Use rapidhash's fast mixing function directly
			// rapid_mix is exposed as public inline function in rapidhash.h
			// Using first value from rapid_secret array as mixing constant
			return rapid_mix(static_cast<uint64_t>(key), 0x2d358dccaa6c78a5ull);
		}
		else
		{
			// For larger arithmetic types (e.g., long double on some platforms)
			// Fall back to buffer hashing
			return rapidhash(&key, sizeof(T));
		}
	}
};

// Specialization for pointers
template <typename T>
struct hash<T *>
{
	using is_avalanching = void;

	std::size_t operator()(T *ptr) const noexcept
	{
		// Hash the pointer value using rapid_mix directly
		// This matches how unordered_dense handles pointers efficiently
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
		return rapid_mix(static_cast<uint64_t>(addr), 0x2d358dccaa6c78a5ull);
	}
};

// Specialization for std::string
template <>
struct hash<std::string>
{
	using is_avalanching = void;

	std::size_t operator()(const std::string &str) const noexcept
	{
		return rapidhash(str.data(), str.size());
	}
};

// Specialization for std::string_view
template <>
struct hash<std::string_view>
{
	using is_avalanching = void;

	std::size_t operator()(const std::string_view &str) const noexcept
	{
		return rapidhash(str.data(), str.size());
	}
};

// Specialization for C-strings
template <>
struct hash<const char *>
{
	using is_avalanching = void;

	std::size_t operator()(const char *str) const noexcept
	{
		if (!str)
			return 0;
		return rapidhash(str, std::strlen(str));
	}
};

// Specialization for std::pair
template <typename First, typename Second>
struct hash<std::pair<First, Second>>
{
	using is_avalanching = void;

	std::size_t operator()(const std::pair<First, Second> &p) const noexcept
	{
		// Combine hashes of both elements
		std::size_t h1 = hash<First>{}(p.first);
		std::size_t h2 = hash<Second>{}(p.second);

		// Combine using RapidHash for better mixing
		std::size_t combined[2] = {h1, h2};
		return rapidhash(combined, sizeof(combined));
	}
};

// Utility function for seeded hashing
template <typename T>
inline std::size_t hash_with_seed(const T &key, uint64_t seed) noexcept
{
	if constexpr (std::is_arithmetic_v<T>)
	{
		if constexpr (sizeof(T) <= sizeof(uint64_t))
		{
			// Use rapid_mix with seed for optimal performance
			return rapid_mix(static_cast<uint64_t>(key), seed);
		}
		else
		{
			return rapidhash_withSeed(&key, sizeof(T), seed);
		}
	}
	else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>)
	{
		return rapidhash_withSeed(key.data(), key.size(), seed);
	}
	else if constexpr (std::is_pointer_v<T>)
	{
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(key);
		// Use rapid_mix with seed for optimal performance
		return rapid_mix(static_cast<uint64_t>(addr), seed);
	}
	else
	{
		return rapidhash_withSeed(&key, sizeof(T), seed);
	}
}

// Utility to combine multiple hashes (useful for composite keys)
template <typename... Args>
inline std::size_t hash_combine(Args... args) noexcept
{
	std::size_t hashes[] = {hash<std::decay_t<Args>>{}(args)...};
	return rapidhash(hashes, sizeof(hashes));
}

#endif        // MSPLAT_USE_SYSTEM_STL

}        // namespace msplat::container