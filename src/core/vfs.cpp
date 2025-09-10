#include "msplat/core/vfs.h"
#include "msplat/core/containers/filesystem.h"
#include "msplat/core/log.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace msplat::vfs
{

//=========================================================================
// Blob Implementation
//=========================================================================

Blob::Blob(container::vector<std::byte> &&data) :
    m_data(std::move(data))
{
}

const std::byte *Blob::data() const
{
	return m_data.data();
}

size_t Blob::size() const
{
	return m_data.size();
}

//=========================================================================
// FileStream Implementation
//=========================================================================

FileStream::FileStream(FILE *handle, size_t length) :
    m_handle(handle), m_length(length)
{
}

FileStream::~FileStream()
{
	if (m_handle)
	{
		std::fclose(m_handle);
	}
}

container::unique_ptr<FileStream> FileStream::open(const container::filesystem::path &path)
{
	// Open file in binary mode
#ifdef _WIN32
	FILE             *handle  = nullptr;
	container::string pathStr = container::to_string(path);
	errno_t           err     = fopen_s(&handle, pathStr.c_str(), "rb");
	if (err != 0 || !handle)
	{
		LOG_WARNING(container::string("Failed to open file: ") + pathStr);
		return nullptr;
	}
#else
	container::string pathStr = container::to_string(path);
	FILE             *handle  = std::fopen(pathStr.c_str(), "rb");
	if (!handle)
	{
		LOG_WARNING(container::string("Failed to open file: ") + pathStr);
		return nullptr;
	}
#endif

	// Get file size
	std::fseek(handle, 0, SEEK_END);
	long fileSize = std::ftell(handle);
	std::fseek(handle, 0, SEEK_SET);

	if (fileSize < 0)
	{
		std::fclose(handle);
		LOG_WARNING(container::string("Failed to get file size: ") + container::to_string(path));
		return nullptr;
	}

	return container::make_unique<FileStream>(handle, static_cast<size_t>(fileSize));
}

size_t FileStream::read(container::span<std::byte> dst)
{
	if (!m_handle)
	{
		return 0;
	}

	size_t currentPos = static_cast<size_t>(std::ftell(m_handle));
	size_t available  = (currentPos < m_length) ? (m_length - currentPos) : 0;
	size_t toRead     = std::min(dst.size(), available);

	if (toRead == 0)
	{
		return 0;
	}

	size_t bytesRead = std::fread(dst.data(), 1, toRead, m_handle);
	return bytesRead;
}

void FileStream::seek(size_t pos)
{
	if (m_handle)
	{
		std::fseek(m_handle, static_cast<long>(pos), SEEK_SET);
	}
}

size_t FileStream::pos() const
{
	if (!m_handle)
	{
		return 0;
	}
	long position = std::ftell(m_handle);
	return (position >= 0) ? static_cast<size_t>(position) : 0;
}

size_t FileStream::length() const
{
	return m_length;
}

//=========================================================================
// MemoryStream Implementation
//=========================================================================

MemoryStream::MemoryStream(container::span<const std::byte> data) :
    m_data(data), m_pos(0)
{
}

size_t MemoryStream::read(container::span<std::byte> dst)
{
	size_t available = (m_pos < m_data.size()) ? (m_data.size() - m_pos) : 0;
	size_t toRead    = std::min(dst.size(), available);

	if (toRead > 0)
	{
		std::memcpy(dst.data(), m_data.data() + m_pos, toRead);
		m_pos += toRead;
	}

	return toRead;
}

void MemoryStream::seek(size_t pos)
{
	m_pos = std::min(pos, m_data.size());
}

size_t MemoryStream::pos() const
{
	return m_pos;
}

size_t MemoryStream::length() const
{
	return m_data.size();
}

//=========================================================================
// NativeFileSystem Implementation
//=========================================================================

NativeFileSystem::NativeFileSystem(const container::filesystem::path &basePath) :
    m_basePath(container::filesystem::canonical(basePath))
{
	if (!container::filesystem::exists(m_basePath))
	{
		LOG_ERROR(container::string("Base path does not exist: ") + container::to_string(m_basePath));
		throw std::runtime_error(container::to_std_string(container::string("Base path does not exist: ") + container::to_string(m_basePath)));
	}

	if (!container::filesystem::is_directory(m_basePath))
	{
		LOG_ERROR(container::string("Base path is not a directory: ") + container::to_string(m_basePath));
		throw std::runtime_error(container::to_std_string(container::string("Base path is not a directory: ") + container::to_string(m_basePath)));
	}

	LOG_DEBUG(container::string("NativeFileSystem initialized with base path: ") + container::to_string(m_basePath));
}

container::filesystem::path NativeFileSystem::resolve(const container::filesystem::path &path) const
{
	// Combine base path with requested path
	auto fullPath = m_basePath / path;

	// Canonicalize to resolve .. and . components
	std::error_code ec;
	auto            canonical = container::filesystem::canonical(fullPath, ec);

	if (ec)
	{
		// If file doesn't exist, we can't canonicalize, so use weakly_canonical
		canonical = container::filesystem::weakly_canonical(fullPath);
	}

	// Security check: ensure the resolved path is within our base path
	auto [mismatch1, mismatch2] = std::mismatch(
	    m_basePath.begin(), m_basePath.end(),
	    canonical.begin(), canonical.end());

	if (mismatch1 != m_basePath.end())
	{
		LOG_WARNING(container::string("Path traversal attempt detected: ") + container::to_string(path));
		return {};        // Return empty path to indicate invalid
	}

	return canonical;
}

container::unique_ptr<IStream> NativeFileSystem::openStream(const container::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return nullptr;
	}

	return FileStream::open(fullPath);
}

container::unique_ptr<IBlob> NativeFileSystem::readFile(const container::filesystem::path &path)
{
	auto stream = openStream(path);
	if (!stream)
	{
		return nullptr;
	}

	// Allocate buffer for entire file
	size_t                       fileSize = stream->length();
	container::vector<std::byte> buffer(container::pmr::GetUpstreamAllocator());
	buffer.resize(fileSize);

	// Read entire file
	size_t bytesRead = stream->read(container::span<std::byte>(buffer.data(), fileSize));
	if (bytesRead != fileSize)
	{
		LOG_WARNING(container::string("Failed to read entire file: ") + container::to_string(path));
		return nullptr;
	}

	return container::make_unique<Blob>(std::move(buffer));
}

bool NativeFileSystem::fileExists(const container::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return false;
	}

	return container::filesystem::exists(fullPath) && container::filesystem::is_regular_file(fullPath);
}

bool NativeFileSystem::folderExists(const container::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return false;
	}

	return container::filesystem::exists(fullPath) && container::filesystem::is_directory(fullPath);
}

void NativeFileSystem::enumerateFiles(const container::filesystem::path &path, enumerate_callback_t callback)
{
	auto fullPath = resolve(path);
	if (fullPath.empty() || !container::filesystem::is_directory(fullPath))
	{
		return;
	}

	std::error_code ec;
	for (const auto &entry : container::filesystem::directory_iterator(fullPath, ec))
	{
		if (ec)
		{
			LOG_WARNING(container::string("Error enumerating directory: ") + ec.message());
			break;
		}

		if (entry.is_regular_file())
		{
			// Return path relative to the requested directory
			auto relativePath = path / entry.path().filename();
			callback(relativePath);
		}
	}
}

void NativeFileSystem::enumerateDirectories(const container::filesystem::path &path, enumerate_callback_t callback)
{
	auto fullPath = resolve(path);
	if (fullPath.empty() || !container::filesystem::is_directory(fullPath))
	{
		return;
	}

	std::error_code ec;
	for (const auto &entry : container::filesystem::directory_iterator(fullPath, ec))
	{
		if (ec)
		{
			LOG_WARNING(container::string("Error enumerating directory: ") + ec.message());
			break;
		}

		if (entry.is_directory())
		{
			// Return path relative to the requested directory
			auto relativePath = path / entry.path().filename();
			callback(relativePath);
		}
	}
}

//=========================================================================
// RootFileSystem Implementation
//=========================================================================

RootFileSystem::RootFileSystem() :
    m_mountPoints(container::pmr::GetUpstreamAllocator())
{
}

void RootFileSystem::mount(const container::filesystem::path &path, container::shared_ptr<IFileSystem> fs)
{
	if (!fs)
	{
		LOG_ERROR(container::string("Cannot mount null filesystem"));
		return;
	}

	container::string pathStr = container::to_string(path);

	// Remove existing mount at this path if any
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts(container::pmr::GetUpstreamAllocator());
	for (const auto &mp : m_mountPoints)
	{
		if (mp.first != pathStr)
		{
			newMounts.push_back(mp);
		}
	}
	m_mountPoints = std::move(newMounts);

	// Add new mount point
	m_mountPoints.emplace_back(container::string(pathStr, container::pmr::GetUpstreamAllocator()), fs);

	// Sort by path length (descending) to handle nested mounts correctly
	std::sort(m_mountPoints.begin(), m_mountPoints.end(),
	          [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });

	LOG_DEBUG(container::string("Mounted filesystem at: ") + pathStr);
}

bool RootFileSystem::unmount(const container::filesystem::path &path)
{
	container::string pathStr = container::to_string(path);

	size_t                                                                              originalSize = m_mountPoints.size();
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts(container::pmr::GetUpstreamAllocator());

	for (const auto &mp : m_mountPoints)
	{
		if (mp.first != pathStr)
		{
			newMounts.push_back(mp);
		}
	}

	bool removed  = (newMounts.size() < originalSize);
	m_mountPoints = std::move(newMounts);

	if (removed)
	{
		LOG_DEBUG(container::string("Unmounted filesystem at: ") + pathStr);
	}

	return removed;
}

IFileSystem *RootFileSystem::findMountPoint(const container::filesystem::path &path,
                                            container::filesystem::path       &outRelativePath) const
{
	container::string pathStr = container::to_string(path);

	// Replace backslashes with forward slashes for consistency
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

	// Search for the longest matching mount point
	for (const auto &[mountPath, fs] : m_mountPoints)
	{
		std::string mountPathStr(mountPath.data(), mountPath.size());
		if (pathStr.starts_with(mountPathStr))
		{
			// Calculate relative path
			if (pathStr.length() > mountPathStr.length())
			{
				// Ensure there's a separator after the mount point
				if (pathStr[mountPathStr.length()] == '/')
				{
					outRelativePath = pathStr.substr(mountPathStr.length() + 1);
				}
				else if (mountPathStr.back() == '/')
				{
					outRelativePath = pathStr.substr(mountPathStr.length());
				}
				else
				{
					continue;        // Not a proper prefix match
				}
			}
			else
			{
				outRelativePath = ".";
			}

			return fs.get();
		}
	}

	return nullptr;
}

container::unique_ptr<IStream> RootFileSystem::openStream(const container::filesystem::path &path)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return nullptr;
	}

	return fs->openStream(relativePath);
}

container::unique_ptr<IBlob> RootFileSystem::readFile(const container::filesystem::path &path)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return nullptr;
	}

	return fs->readFile(relativePath);
}

bool RootFileSystem::fileExists(const container::filesystem::path &path)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		return false;
	}

	return fs->fileExists(relativePath);
}

bool RootFileSystem::folderExists(const container::filesystem::path &path)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		return false;
	}

	return fs->folderExists(relativePath);
}

void RootFileSystem::enumerateFiles(const container::filesystem::path &path, enumerate_callback_t callback)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return;
	}

	// Wrap callback to prepend the mount path
	container::string mountPrefix = container::to_string(path);
	if (!mountPrefix.empty() && mountPrefix.back() != '/')
	{
		mountPrefix += '/';
	}

	fs->enumerateFiles(relativePath, [&mountPrefix, &callback](const container::filesystem::path &p) {
		callback(container::filesystem::path(mountPrefix) / p);
	});
}

void RootFileSystem::enumerateDirectories(const container::filesystem::path &path, enumerate_callback_t callback)
{
	container::filesystem::path relativePath;
	IFileSystem                *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return;
	}

	// Wrap callback to prepend the mount path
	container::string mountPrefix = container::to_string(path);
	if (!mountPrefix.empty() && mountPrefix.back() != '/')
	{
		mountPrefix += '/';
	}

	fs->enumerateDirectories(relativePath, [&mountPrefix, &callback](const container::filesystem::path &p) {
		callback(container::filesystem::path(mountPrefix) / p);
	});
}

//=========================================================================
// Utility Functions
//=========================================================================

container::vector<uint8_t> readFile(const container::string &path)
{
	// Create a global singleton filesystem for convenience
	static RootFileSystem fs;
	static bool           initialized = false;

	if (!initialized)
	{
		// Mount current working directory at root
		auto cwd = container::filesystem::current_path();
		fs.mount("/", container::make_shared<NativeFileSystem>(cwd));
		initialized = true;
	}

	// Convert string to filesystem path
	container::filesystem::path filePath{std::string(path.data(), path.size())};

	// For relative paths, add leading "/" to match our mount point
	if (!filePath.is_absolute() && !path.empty() && path[0] != '/')
	{
		filePath = container::filesystem::path("/") / filePath;
	}

	// Try to read the file
	auto blob = fs.readFile(filePath);

	if (!blob)
	{
		LOG_ERROR(container::string("Failed to read file: ") + path);
		throw std::runtime_error(container::to_std_string(container::string("Failed to read file: ") + path));
	}

	// Convert from byte vector to uint8_t vector
	container::vector<uint8_t> result(container::pmr::GetUpstreamAllocator());
	result.resize(blob->size());

	if (blob->size() > 0)
	{
		std::memcpy(result.data(), blob->data(), blob->size());
	}

	LOG_DEBUG(container::string("Successfully loaded file: ") + path + " (" + container::to_string(std::to_string(blob->size())) + " bytes)");
	return result;
}

}        // namespace msplat::vfs