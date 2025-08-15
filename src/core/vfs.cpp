#include "core/vfs.h"
#include "core/log.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace msplat::vfs
{

std::vector<uint8_t> readFile(const std::string &path)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		LOG_ERROR("Failed to open file: " + path);
		throw std::runtime_error("Failed to open file: " + path);
	}

	// Get file size
	std::streamsize fileSize = file.tellg();
	if (fileSize < 0)
	{
		LOG_ERROR("Failed to get file size: " + path);
		throw std::runtime_error("Failed to get file size: " + path);
	}

	// Allocate buffer
	std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));

	// Read file
	file.seekg(0, std::ios::beg);
	if (!file.read(reinterpret_cast<char *>(buffer.data()), fileSize))
	{
		LOG_ERROR("Failed to read file: " + path);
		throw std::runtime_error("Failed to read file: " + path);
	}

	LOG_DEBUG("Successfully loaded file: " + path + " (" + std::to_string(fileSize) + " bytes)");
	return buffer;
}

bool fileExists(const std::string &path)
{
	return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

size_t getFileSize(const std::string &path)
{
	if (!fileExists(path))
	{
		return 0;
	}

	std::error_code ec;
	auto            size = std::filesystem::file_size(path, ec);
	if (ec)
	{
		LOG_WARNING("Failed to get file size for: " + path + " - " + ec.message());
		return 0;
	}

	return static_cast<size_t>(size);
}

std::string resolvePath(const std::string &relativePath)
{
	// Get current working directory
	std::error_code ec;
	auto            currentPath = std::filesystem::current_path(ec);
	if (ec)
	{
		LOG_WARNING("Failed to get current path, using relative path as-is: " + ec.message());
		return relativePath;
	}

	// Resolve relative path
	auto resolvedPath = currentPath / relativePath;
	resolvedPath      = std::filesystem::canonical(resolvedPath, ec);
	if (ec)
	{
		// If canonical fails, just return the joined path
		LOG_DEBUG("Canonical path resolution failed, using joined path: " + ec.message());
		return (currentPath / relativePath).string();
	}

	return resolvedPath.string();
}

}        // namespace msplat::vfs