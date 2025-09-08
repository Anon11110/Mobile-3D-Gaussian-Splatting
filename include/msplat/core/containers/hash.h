#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
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

// ============================================================================
// Type Traits
// ============================================================================

// Type trait to detect character types
template <typename Char>
struct is_char : std::disjunction<
                     std::is_same<std::remove_cvref_t<Char>, char>,
                     std::is_same<std::remove_cvref_t<Char>, char8_t>,
                     std::is_same<std::remove_cvref_t<Char>, char16_t>,
                     std::is_same<std::remove_cvref_t<Char>, char32_t>,
                     std::is_same<std::remove_cvref_t<Char>, wchar_t>>
{};

template <typename Char>
constexpr bool is_char_v = is_char<Char>::value;

// ============================================================================
// Primary Hash Template
// ============================================================================

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

// ============================================================================
// Fundamental Type Specializations
// ============================================================================

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

// ============================================================================
// String Hash Infrastructure
// ============================================================================

// Generic hash template for all string types
// Provides transparent hashing for heterogeneous lookup in containers
template <typename Char, typename CharTraits>
struct basic_string_hash
{
	using is_transparent = void;        // Enable heterogeneous lookup
	using is_avalanching = void;        // Compatible with ankerl::unordered_dense

	// Hash string_view
	[[nodiscard]] std::size_t operator()(std::basic_string_view<Char, CharTraits> s) const noexcept
	{
		return rapidhash(s.data(), s.size() * sizeof(Char));
	}

	// Hash any allocator-based string
	template <typename Allocator>
	[[nodiscard]] std::size_t operator()(const std::basic_string<Char, CharTraits, Allocator> &s) const noexcept
	{
		return rapidhash(s.data(), s.size() * sizeof(Char));
	}

	// Hash C-string
	[[nodiscard]] std::size_t operator()(const Char *s) const noexcept
	{
		if (!s)
			return 0;
		return rapidhash(s, CharTraits::length(s) * sizeof(Char));
	}
};

// ============================================================================
// String Type Specializations
// ============================================================================

// Standard string types
template <>
struct hash<std::string> : basic_string_hash<char, std::char_traits<char>>
{};

template <>
struct hash<std::string_view> : basic_string_hash<char, std::char_traits<char>>
{};

template <>
struct hash<const char *> : basic_string_hash<char, std::char_traits<char>>
{};

// UTF-8 string types
template <>
struct hash<std::u8string> : basic_string_hash<char8_t, std::char_traits<char8_t>>
{};

template <>
struct hash<std::u8string_view> : basic_string_hash<char8_t, std::char_traits<char8_t>>
{};

// UTF-16 string types
template <>
struct hash<std::u16string> : basic_string_hash<char16_t, std::char_traits<char16_t>>
{};

template <>
struct hash<std::u16string_view> : basic_string_hash<char16_t, std::char_traits<char16_t>>
{};

// UTF-32 string types
template <>
struct hash<std::u32string> : basic_string_hash<char32_t, std::char_traits<char32_t>>
{};

template <>
struct hash<std::u32string_view> : basic_string_hash<char32_t, std::char_traits<char32_t>>
{};

// Wide string types
template <>
struct hash<std::wstring> : basic_string_hash<wchar_t, std::char_traits<wchar_t>>
{};

template <>
struct hash<std::wstring_view> : basic_string_hash<wchar_t, std::char_traits<wchar_t>>
{};

// Generic specializations for character pointers and arrays
template <typename Char>
requires is_char_v<Char>
struct hash<Char *> : basic_string_hash<Char, std::char_traits<Char>>
{};

template <typename Char, size_t N>
requires is_char_v<Char>
struct hash<Char[N]> : basic_string_hash<Char, std::char_traits<Char>>
{};

// ============================================================================
// Container Specializations
// ============================================================================

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

// Specialization for std::array
template <typename T, std::size_t N>
struct hash<std::array<T, N>>
{
	using is_avalanching = void;

	std::size_t operator()(const std::array<T, N> &arr) const noexcept
	{
		if constexpr (std::is_arithmetic_v<T> && sizeof(T) * N <= 64)
		{
			// Direct hash for small arrays of arithmetic types
			return rapidhash(arr.data(), sizeof(T) * N);
		}
		else if constexpr (std::is_arithmetic_v<T>)
		{
			// For larger arrays of arithmetic types, hash the raw data
			return rapidhash(arr.data(), sizeof(T) * N);
		}
		else
		{
			// For non-arithmetic types, hash each element and combine
			std::size_t hashes[N];
			for (std::size_t i = 0; i < N; ++i)
			{
				hashes[i] = hash<T>{}(arr[i]);
			}
			return rapidhash(hashes, sizeof(hashes));
		}
	}
};

// Specialization for std::optional
template <typename T>
struct hash<std::optional<T>>
{
	using is_avalanching = void;

	std::size_t operator()(const std::optional<T> &opt) const noexcept
	{
		if (!opt.has_value())
		{
			// Use a specific value for empty optionals
			return 0;
		}
		// Hash the contained value with a salt to distinguish from raw T
		std::size_t value_hash = hash<T>{}(*opt);
		return rapid_mix(value_hash, 0x9e3779b97f4a7c15ull);        // Golden ratio constant
	}
};

// ============================================================================
// Utility Functions
// ============================================================================

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
	// Check for string-like types (has data() and size() methods)
	else if constexpr (requires { key.data(); key.size(); })
	{
		using value_type = std::remove_cvref_t<decltype(*key.data())>;
		return rapidhash_withSeed(key.data(), key.size() * sizeof(value_type), seed);
	}
	// Check for C-string types
	else if constexpr (std::is_pointer_v<T> && is_char_v<std::remove_pointer_t<T>>)
	{
		using char_type = std::remove_pointer_t<T>;
		if (!key)
			return seed;
		return rapidhash_withSeed(key, std::char_traits<char_type>::length(key) * sizeof(char_type), seed);
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