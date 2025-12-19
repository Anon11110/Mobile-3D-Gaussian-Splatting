#pragma once

#include "string.h"
#include <filesystem>

// Conditional compilation support
#ifdef MSPLAT_USE_STD_CONTAINERS
namespace msplat::container
{
// Filesystem namespace alias for consistency with LuisaCompute pattern
// Note: This alias is for internal use within implementation files
// Public APIs should use std::filesystem::path directly for MSVC compatibility
namespace filesystem = std::filesystem;

// Type alias for convenience (helps with template instantiation)
using path = std::filesystem::path;

// Convert filesystem path to string using standard string
[[nodiscard]] inline std::string to_string(const filesystem::path &path)
{
	return path.string();
}

// Explicit conversion helpers for ABI boundaries
[[nodiscard]] inline std::filesystem::path to_std_path(const filesystem::path &path)
{
	return path;        // No-op since they're the same type
}

[[nodiscard]] inline filesystem::path from_std_path(const std::filesystem::path &path)
{
	return path;        // No-op since they're the same type
}

}        // namespace msplat::container

#else
// Custom implementation with PMR-based string

namespace msplat::container
{
// Filesystem namespace alias for consistency with LuisaCompute pattern
// Note: This alias is for internal use within implementation files
// Public APIs should use std::filesystem::path directly for MSVC compatibility
namespace filesystem = std::filesystem;

// Type alias for convenience (helps with template instantiation)
using path = std::filesystem::path;

// Convert filesystem path to string using PMR-based string
// Following LuisaCompute pattern: directly use path's templated string() method
[[nodiscard]] inline string to_string(const filesystem::path &path)
{
	// Create a PMR-based string first, then convert to our string class
	auto pmr_str = path.string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>();
	return string(pmr_str.data(), pmr_str.size(), pmr_str.get_allocator());
}

// Explicit conversion helpers for ABI boundaries
[[nodiscard]] inline std::filesystem::path to_std_path(const filesystem::path &path)
{
	return path;        // No-op since they're the same type
}

[[nodiscard]] inline filesystem::path from_std_path(const std::filesystem::path &path)
{
	return path;        // No-op since they're the same type
}

}        // namespace msplat::container

#endif        // MSPLAT_USE_STD_CONTAINERS