#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Conditional compilation support
#ifdef MSPLAT_USE_STD_CONTAINERS
namespace msplat::container
{
// Use standard library string types
using std::string;
using std::u16string;
using std::u32string;
using std::u8string;
using std::wstring;

using std::string_view;
using std::u16string_view;
using std::u32string_view;
using std::u8string_view;
using std::wstring_view;

// Helper functions for compatibility (just pass through for STL version)
inline std::string to_std_string(const string &str)
{
	return str;
}

inline string to_pmr_string(const std::string &str)
{
	return str;
}

// Generic to_string helper - returns container::string unchanged, converts others
template <typename T>
inline string to_string(T &&t)
{
	if constexpr (std::is_same_v<std::decay_t<T>, string>)
	{
		return std::forward<T>(t);
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
	{
		return std::forward<T>(t);        // string is aliased to std::string here
	}
	else if constexpr (std::is_convertible_v<T, std::string>)
	{
		return string(std::forward<T>(t));
	}
	else
	{
		return string(t);
	}
}

}        // namespace msplat::container

#else
// Custom PMR-based string implementation
#	include "hash.h"
#	include "memory.h"
#	include <memory_resource>

namespace msplat::container
{
// Helper class that adds implicit conversion to std::string for LOG_* macro compatibility
class string : public std::basic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>
{
  public:
	using base = std::basic_string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>;
	using base::base;             // Inherit all constructors
	using base::operator=;        // Inherit assignment operators

	// Implicit conversion to std::string for logging and other std::string APIs
	operator std::string() const
	{
		return std::string(this->data(), this->size());
	}

	// String concatenation operators
	string operator+(const string &rhs) const
	{
		string result(*this);
		result.append(rhs);
		return result;
	}

	string operator+(const std::string &rhs) const
	{
		string result(*this);
		result.append(rhs.data(), rhs.size());
		return result;
	}

	string operator+(const char *rhs) const
	{
		string result(*this);
		result.append(rhs);
		return result;
	}
};

// PMR-based string types for custom memory allocation
using u8string  = std::basic_string<char8_t, std::char_traits<char8_t>, std::pmr::polymorphic_allocator<char8_t>>;
using u16string = std::basic_string<char16_t, std::char_traits<char16_t>, std::pmr::polymorphic_allocator<char16_t>>;
using u32string = std::basic_string<char32_t, std::char_traits<char32_t>, std::pmr::polymorphic_allocator<char32_t>>;
using wstring   = std::basic_string<wchar_t, std::char_traits<wchar_t>, std::pmr::polymorphic_allocator<wchar_t>>;

// String view types (always use standard library)
using std::string_view;
using std::u16string_view;
using std::u32string_view;
using std::u8string_view;
using std::wstring_view;

// Helper functions for conversion between std::string and PMR string
inline string to_pmr_string(const std::string &str, std::pmr::memory_resource *res = nullptr)
{
	if (!res)
		res = pmr::GetUpstreamAllocator();
	return string(str.data(), str.size(), res);
}

inline std::string to_std_string(const string &str)
{
	return std::string(str.data(), str.size());
}

// Note: Concatenation operators are now member functions of the string class
// to avoid ambiguity with implicit conversions

// Generic to_string helper - returns container::string unchanged, converts others
template <typename T>
inline string to_string(T &&t)
{
	if constexpr (std::is_same_v<std::decay_t<T>, string>)
	{
		return std::forward<T>(t);
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, std::string>)
	{
		return to_pmr_string(std::forward<T>(t));
	}
	else if constexpr (std::is_same_v<std::decay_t<T>, std::filesystem::path>)
	{
		return to_pmr_string(t.string());
	}
	else if constexpr (std::is_convertible_v<T, std::string>)
	{
		return to_pmr_string(std::string(std::forward<T>(t)));
	}
	else
	{
		// For other types, try to construct via std::string
		return to_pmr_string(std::string(t));
	}
}

// Hash specializations for PMR string types (delegating to basic_string_hash in hash.h)
template <>
struct hash<string> : basic_string_hash<char, std::char_traits<char>>
{};

template <>
struct hash<u8string> : basic_string_hash<char8_t, std::char_traits<char8_t>>
{};

template <>
struct hash<u16string> : basic_string_hash<char16_t, std::char_traits<char16_t>>
{};

template <>
struct hash<u32string> : basic_string_hash<char32_t, std::char_traits<char32_t>>
{};

template <>
struct hash<wstring> : basic_string_hash<wchar_t, std::char_traits<wchar_t>>
{};

}        // namespace msplat::container

#endif        // MSPLAT_USE_STD_CONTAINERS