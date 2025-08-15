#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace msplat::vfs
{

/// Virtual File System interface for future extensibility
/// Currently provides physical filesystem pass-through
/// Future backends could include archive files, embedded resources, etc.

/// Read entire file into memory
/// @param path File path to read
/// @return File contents as byte vector
/// @throws std::runtime_error if file cannot be read
std::vector<uint8_t> readFile(const std::string &path);

/// Check if file exists
/// @param path File path to check
/// @return true if file exists and is readable
bool fileExists(const std::string &path);

/// Get file size in bytes
/// @param path File path to check
/// @return File size in bytes, or 0 if file doesn't exist
size_t getFileSize(const std::string &path);

/// Resolve path relative to application directory
/// @param relativePath Path relative to executable location
/// @return Absolute path
std::string resolvePath(const std::string &relativePath);

}        // namespace msplat::vfs