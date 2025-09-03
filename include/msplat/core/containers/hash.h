#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>

#include "third-party/rapidhash/rapidhash.h"

namespace msplat::container
{

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
		// For small types, hash directly
		if constexpr (sizeof(T) <= sizeof(std::size_t))
		{
			// Mix bits for better distribution on small values
			std::size_t h = static_cast<std::size_t>(key);
			return rapidhash(&h, sizeof(h));
		}
		else
		{
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
		// Hash the pointer value
		std::uintptr_t addr = reinterpret_cast<std::uintptr_t>(ptr);
		return rapidhash(&addr, sizeof(addr));
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
		if constexpr (sizeof(T) <= sizeof(std::size_t))
		{
			std::size_t h = static_cast<std::size_t>(key);
			return rapidhash_withSeed(&h, sizeof(h), seed);
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
		return rapidhash_withSeed(&addr, sizeof(addr), seed);
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

}        // namespace msplat::container