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

NativeFileSystem::NativeFileSystem(const std::filesystem::path &basePath) :
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

std::filesystem::path NativeFileSystem::resolve(const std::filesystem::path &path) const
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

container::unique_ptr<IStream> NativeFileSystem::openStream(const std::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return nullptr;
	}

	return FileStream::open(fullPath);
}

container::unique_ptr<IBlob> NativeFileSystem::readFile(const std::filesystem::path &path)
{
	auto stream = openStream(path);
	if (!stream)
	{
		return nullptr;
	}

	// Allocate buffer for entire file
	size_t fileSize = stream->length();
#ifdef MSPLAT_USE_STD_CONTAINERS
	container::vector<std::byte> buffer;
#else
	container::vector<std::byte> buffer(container::pmr::GetUpstreamAllocator());
#endif
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

bool NativeFileSystem::fileExists(const std::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return false;
	}

	return container::filesystem::exists(fullPath) && container::filesystem::is_regular_file(fullPath);
}

bool NativeFileSystem::folderExists(const std::filesystem::path &path)
{
	auto fullPath = resolve(path);
	if (fullPath.empty())
	{
		return false;
	}

	return container::filesystem::exists(fullPath) && container::filesystem::is_directory(fullPath);
}

void NativeFileSystem::enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback)
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

void NativeFileSystem::enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback)
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

RootFileSystem::RootFileSystem()
#ifndef MSPLAT_USE_STD_CONTAINERS
    :
    m_mountPoints(container::pmr::GetUpstreamAllocator())
#endif
{
}

void RootFileSystem::mount(const std::filesystem::path &path, std::shared_ptr<IFileSystem> fs)
{
	if (!fs)
	{
		LOG_ERROR(container::string("Cannot mount null filesystem"));
		return;
	}

	container::string pathStr = container::to_string(path);

	// Remove existing mount at this path if any
#ifdef MSPLAT_USE_STD_CONTAINERS
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts;
#else
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts(container::pmr::GetUpstreamAllocator());
#endif
	for (const auto &mp : m_mountPoints)
	{
		if (mp.first != pathStr)
		{
			newMounts.push_back(mp);
		}
	}
	m_mountPoints = std::move(newMounts);

	// Add new mount point
#ifdef MSPLAT_USE_STD_CONTAINERS
	m_mountPoints.emplace_back(container::string(pathStr), fs);
#else
	m_mountPoints.emplace_back(container::string(pathStr, container::pmr::GetUpstreamAllocator()), fs);
#endif

	// Sort by path length (descending) to handle nested mounts correctly
	std::sort(m_mountPoints.begin(), m_mountPoints.end(),
	          [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });

	LOG_DEBUG(container::string("Mounted filesystem at: ") + pathStr);
}

bool RootFileSystem::unmount(const std::filesystem::path &path)
{
	container::string pathStr = container::to_string(path);

	size_t originalSize = m_mountPoints.size();
#ifdef MSPLAT_USE_STD_CONTAINERS
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts;
#else
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> newMounts(container::pmr::GetUpstreamAllocator());
#endif

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

IFileSystem *RootFileSystem::findMountPoint(const std::filesystem::path &path,
                                            std::filesystem::path       &outRelativePath) const
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

container::unique_ptr<IStream> RootFileSystem::openStream(const std::filesystem::path &path)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return nullptr;
	}

	return fs->openStream(relativePath);
}

container::unique_ptr<IBlob> RootFileSystem::readFile(const std::filesystem::path &path)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		LOG_WARNING(container::string("No mount point found for path: ") + container::to_string(path));
		return nullptr;
	}

	return fs->readFile(relativePath);
}

bool RootFileSystem::fileExists(const std::filesystem::path &path)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		return false;
	}

	return fs->fileExists(relativePath);
}

bool RootFileSystem::folderExists(const std::filesystem::path &path)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

	if (!fs)
	{
		return false;
	}

	return fs->folderExists(relativePath);
}

void RootFileSystem::enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

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

	fs->enumerateFiles(relativePath, [&mountPrefix, &callback](const std::filesystem::path &p) {
		callback(std::filesystem::path(container::to_std_string(mountPrefix)) / p);
	});
}

void RootFileSystem::enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback)
{
	std::filesystem::path relativePath;
	IFileSystem          *fs = findMountPoint(path, relativePath);

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

	fs->enumerateDirectories(relativePath, [&mountPrefix, &callback](const std::filesystem::path &p) {
		callback(std::filesystem::path(container::to_std_string(mountPrefix)) / p);
	});
}

//=========================================================================
// Android Asset FileSystem Implementation
//=========================================================================

#if defined(__ANDROID__)

AndroidAssetStream::AndroidAssetStream(AAsset *asset) :
    m_asset(asset), m_pos(0)
{
	m_length = static_cast<size_t>(AAsset_getLength(asset));
}

AndroidAssetStream::~AndroidAssetStream()
{
	if (m_asset)
	{
		AAsset_close(m_asset);
	}
}

size_t AndroidAssetStream::read(container::span<std::byte> dst)
{
	if (!m_asset)
	{
		return 0;
	}

	int bytesRead = AAsset_read(m_asset, dst.data(), dst.size());
	if (bytesRead > 0)
	{
		m_pos += static_cast<size_t>(bytesRead);
		return static_cast<size_t>(bytesRead);
	}
	return 0;
}

void AndroidAssetStream::seek(size_t pos)
{
	if (m_asset)
	{
		AAsset_seek(m_asset, static_cast<off_t>(pos), SEEK_SET);
		m_pos = pos;
	}
}

size_t AndroidAssetStream::pos() const
{
	return m_pos;
}

size_t AndroidAssetStream::length() const
{
	return m_length;
}

AndroidAssetFileSystem::AndroidAssetFileSystem(AAssetManager *assetManager, const std::string &basePath) :
    m_assetManager(assetManager), m_basePath(basePath)
{
	// Normalize base path by removing leading/trailing slashes
	while (!m_basePath.empty() && m_basePath.front() == '/')
	{
		m_basePath.erase(0, 1);
	}
	while (!m_basePath.empty() && m_basePath.back() == '/')
	{
		m_basePath.pop_back();
	}

	LOG_DEBUG("AndroidAssetFileSystem initialized with base path: {}", m_basePath.empty() ? "(root)" : m_basePath);
}

std::string AndroidAssetFileSystem::resolveAssetPath(const std::filesystem::path &path) const
{
	std::string pathStr = path.string();

	// Normalize base path by removing leading/trailing slashes
	while (!pathStr.empty() && pathStr.front() == '/')
	{
		pathStr.erase(0, 1);
	}

	// Replace backslashes with forward slashes
	std::replace(pathStr.begin(), pathStr.end(), '\\', '/');

	if (!m_basePath.empty())
	{
		return m_basePath + "/" + pathStr;
	}
	return pathStr;
}

container::unique_ptr<IStream> AndroidAssetFileSystem::openStream(const std::filesystem::path &path)
{
	std::string assetPath = resolveAssetPath(path);

	AAsset *asset = AAssetManager_open(m_assetManager, assetPath.c_str(), AASSET_MODE_STREAMING);
	if (!asset)
	{
		LOG_WARNING("Failed to open Android asset: {}", assetPath);
		return nullptr;
	}

	return container::make_unique<AndroidAssetStream>(asset);
}

container::unique_ptr<IBlob> AndroidAssetFileSystem::readFile(const std::filesystem::path &path)
{
	std::string assetPath = resolveAssetPath(path);

	AAsset *asset = AAssetManager_open(m_assetManager, assetPath.c_str(), AASSET_MODE_BUFFER);
	if (!asset)
	{
		LOG_WARNING("Failed to open Android asset for reading: {}", assetPath);
		return nullptr;
	}

	size_t                       size = static_cast<size_t>(AAsset_getLength(asset));
	container::vector<std::byte> buffer;
	buffer.resize(size);

	const void *data = AAsset_getBuffer(asset);
	if (data)
	{
		std::memcpy(buffer.data(), data, size);
	}
	else
	{
		// Fallback to read if getBuffer fails
		int bytesRead = AAsset_read(asset, buffer.data(), size);
		if (bytesRead < 0 || static_cast<size_t>(bytesRead) != size)
		{
			LOG_WARNING("Failed to read Android asset: {}", assetPath);
			AAsset_close(asset);
			return nullptr;
		}
	}

	AAsset_close(asset);
	LOG_DEBUG("Read Android asset: {} ({} bytes)", assetPath, size);
	return container::make_unique<Blob>(std::move(buffer));
}

bool AndroidAssetFileSystem::fileExists(const std::filesystem::path &path)
{
	std::string assetPath = resolveAssetPath(path);

	AAsset *asset = AAssetManager_open(m_assetManager, assetPath.c_str(), AASSET_MODE_UNKNOWN);
	if (asset)
	{
		AAsset_close(asset);
		return true;
	}
	return false;
}

bool AndroidAssetFileSystem::folderExists(const std::filesystem::path &path)
{
	std::string assetPath = resolveAssetPath(path);

	AAssetDir *dir = AAssetManager_openDir(m_assetManager, assetPath.c_str());
	if (dir)
	{
		// Check if directory has any content
		const char *filename = AAssetDir_getNextFileName(dir);
		AAssetDir_close(dir);
		return (filename != nullptr);
	}
	return false;
}

void AndroidAssetFileSystem::enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback)
{
	std::string assetPath = resolveAssetPath(path);

	AAssetDir *dir = AAssetManager_openDir(m_assetManager, assetPath.c_str());
	if (!dir)
	{
		return;
	}

	const char *filename;
	while ((filename = AAssetDir_getNextFileName(dir)) != nullptr)
	{
		std::filesystem::path filePath = path / filename;
		callback(filePath);
	}

	AAssetDir_close(dir);
}

void AndroidAssetFileSystem::enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback)
{
	// Android's AAssetManager doesn't distinguish between files and directories easily
	// Subdirectories are not directly enumerable
	(void) path;
	(void) callback;
	LOG_WARNING("enumerateDirectories is not fully supported on Android assets");
}

#endif        // __ANDROID__

}        // namespace msplat::vfs