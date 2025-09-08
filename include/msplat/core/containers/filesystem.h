#pragma once

#include "string.h"
#include <filesystem>

// Conditional compilation support
#ifdef MSPLAT_USE_SYSTEM_STL
namespace msplat::container
{
// Filesystem namespace alias for consistency with LuisaCompute pattern
namespace filesystem = std::filesystem;

// Convert filesystem path to string using standard string
[[nodiscard]] inline std::string to_string(const filesystem::path &path)
{
	return path.string();
}

}        // namespace msplat::container

#else
// Custom implementation with PMR-based string

namespace msplat::container
{
// Filesystem namespace alias for consistency with LuisaCompute pattern
namespace filesystem = std::filesystem;

// Convert filesystem path to string using PMR-based string
// Following LuisaCompute pattern: directly use path's templated string() method
[[nodiscard]] inline string to_string(const filesystem::path &path)
{
	return path.string<char, std::char_traits<char>, std::pmr::polymorphic_allocator<char>>();
}

}        // namespace msplat::container

#endif        // MSPLAT_USE_SYSTEM_STL