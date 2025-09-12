#pragma once

// Main include for the msplat Virtual File System
// This is a single, consolidated header for convenience.

#include <cstddef>        // for std::byte
#include <cstdio>         // For FILE*
#include <msplat/core/containers/filesystem.h>
#include <msplat/core/containers/functional.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/string.h>
#include <msplat/core/containers/vector.h>

namespace msplat::vfs
{
//=========================================================================
// Forward Declarations & Core Types
//=========================================================================

class IBlob;
class IStream;
class IFileSystem;

/// @brief Callback type for file/directory enumeration, using the project's custom function wrapper.
/// Note: Uses std::filesystem::path for ABI stability across translation units
using enumerate_callback_t = container::function<void(const std::filesystem::path &)>;

//=========================================================================
// Blob Interface & Implementation
//=========================================================================

/**
 * @class IBlob
 * @brief An interface for a block of untyped data, typically from a file.
 * Represents an immutable, contiguous block of memory.
 */
class IBlob
{
  public:
	virtual ~IBlob() = default;

	/// @brief Get a pointer to the beginning of the data.
	[[nodiscard]] virtual const std::byte *data() const = 0;

	/// @brief Get the size of the data in bytes.
	[[nodiscard]] virtual size_t size() const = 0;

	/// @brief Check if the blob is empty.
	[[nodiscard]] bool empty() const
	{
		return size() == 0;
	}
};

/**
 * @class Blob
 * @brief A concrete implementation of IBlob that owns its data, using
 * msplat's PMR-aware vector for storage.
 */
class Blob final : public IBlob
{
  public:
	/// @brief Constructs a Blob by taking ownership of a vector of bytes.
	/// @param data The vector of bytes to own.
	explicit Blob(container::vector<std::byte> &&data);

	/// @brief IBlob interface implementation.
	[[nodiscard]] const std::byte *data() const override;
	[[nodiscard]] size_t           size() const override;

  private:
	container::vector<std::byte> m_data;
};

//=========================================================================
// Stream Interface & Implementations
//=========================================================================

/**
 * @class IStream
 * @brief An interface for stream-based reading of data.
 * Allows for sequential access and seeking within a data source.
 */
class IStream
{
  public:
	virtual ~IStream() = default;

	/// @brief Reads up to dst.size() bytes from the stream into the buffer.
	/// @param dst The destination buffer span.
	/// @return The number of bytes actually read.
	virtual size_t read(container::span<std::byte> dst) = 0;

	/// @brief Sets the current position in the stream.
	/// @param pos The offset from the beginning of the stream.
	virtual void seek(size_t pos) = 0;

	/// @brief Gets the current position in the stream.
	[[nodiscard]] virtual size_t pos() const = 0;

	/// @brief Gets the total length of the stream in bytes.
	[[nodiscard]] virtual size_t length() const = 0;
};

/**
 * @class FileStream
 * @brief An IStream implementation for reading from an OS file.
 */
class FileStream final : public IStream
{
  public:
	/// @brief Attempts to open a file and create a stream.
	/// @param path The path to the file.
	/// @return A unique_ptr to the stream, or nullptr on failure.
	static container::unique_ptr<FileStream> open(const std::filesystem::path &path);

	/// @brief Closes the file handle upon destruction.
	~FileStream() override;

	// Rule of 5: Forbid copy/move to simplify resource management.
	FileStream(const FileStream &)            = delete;
	FileStream &operator=(const FileStream &) = delete;
	FileStream(FileStream &&)                 = delete;
	FileStream &operator=(FileStream &&)      = delete;

	/// @brief Constructor, typically called via `open`.
	FileStream(FILE *handle, size_t length);

	/// @brief IStream interface implementation.
	size_t               read(container::span<std::byte> dst) override;
	void                 seek(size_t pos) override;
	[[nodiscard]] size_t pos() const override;
	[[nodiscard]] size_t length() const override;

  private:
	FILE  *m_handle;
	size_t m_length;
};

/**
 * @class MemoryStream
 * @brief An IStream implementation for reading from a non-owning memory buffer.
 */
class MemoryStream final : public IStream
{
  public:
	/// @brief Constructs a stream from a span of bytes. The stream
	///        does not take ownership of the memory.
	/// @param data The span of bytes to read from.
	explicit MemoryStream(container::span<const std::byte> data);

	/// @brief IStream interface implementation.
	size_t               read(container::span<std::byte> dst) override;
	void                 seek(size_t pos) override;
	[[nodiscard]] size_t pos() const override;
	[[nodiscard]] size_t length() const override;

  private:
	container::span<const std::byte> m_data;
	size_t                           m_pos;
};

//=========================================================================
// FileSystem Interface & Implementations
//=========================================================================

/**
 * @class IFileSystem
 * @brief The core interface for a virtual file system.
 * Provides methods for accessing files and directories abstractly.
 */
class IFileSystem
{
  public:
	virtual ~IFileSystem() = default;

	/// @brief Opens a file for stream-based reading.
	/// @param path The virtual path to the file.
	/// @return A unique_ptr to an IStream, or nullptr if the file cannot be opened.
	virtual container::unique_ptr<IStream> openStream(const std::filesystem::path &path) = 0;

	/// @brief Reads an entire file into a blob.
	/// @param path The virtual path to the file.
	/// @return A unique_ptr to an IBlob, or nullptr if the file cannot be read.
	virtual container::unique_ptr<IBlob> readFile(const std::filesystem::path &path) = 0;

	/// @brief Checks if a file exists at the given virtual path.
	virtual bool fileExists(const std::filesystem::path &path) = 0;

	/// @brief Checks if a folder exists at the given virtual path.
	virtual bool folderExists(const std::filesystem::path &path) = 0;

	/// @brief Enumerates all files in a directory.
	/// @param path The virtual path to the directory.
	/// @param callback A function to be called for each file found.
	virtual void enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback) = 0;

	/// @brief Enumerates all subdirectories in a directory.
	/// @param path The virtual path to the directory.
	/// @param callback A function to be called for each directory found.
	virtual void enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback) = 0;
};

/**
 * @class NativeFileSystem
 * @brief An IFileSystem implementation that maps directly to the OS file system.
 */
class NativeFileSystem final : public IFileSystem
{
  public:
	/// @brief Constructs a native file system rooted at a specific base path.
	/// @param basePath The root path on the physical file system.
	explicit NativeFileSystem(const std::filesystem::path &basePath);

	/// @brief IFileSystem interface implementation.
	container::unique_ptr<IStream> openStream(const std::filesystem::path &path) override;
	container::unique_ptr<IBlob>   readFile(const std::filesystem::path &path) override;
	bool                           fileExists(const std::filesystem::path &path) override;
	bool                           folderExists(const std::filesystem::path &path) override;
	void                           enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback) override;
	void                           enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback) override;

  private:
	/// @brief Resolves a virtual path to a physical path and validates it.
	std::filesystem::path resolve(const std::filesystem::path &path) const;

	std::filesystem::path m_basePath;
};

/**
 * @class RootFileSystem
 * @brief A file system that allows mounting other file systems at virtual paths.
 * It acts as the main entry point for all file operations in the engine.
 */
class RootFileSystem final : public IFileSystem
{
  public:
	RootFileSystem();

	/// @brief Mounts a file system at a given virtual path.
	/// @param path The virtual path to mount at (e.g., "/assets").
	/// @param fs The file system instance to mount.
	void mount(const std::filesystem::path &path, container::shared_ptr<IFileSystem> fs);

	/// @brief Unmounts a file system at a given virtual path.
	/// @param path The virtual path to unmount.
	/// @return True if successful, false otherwise.
	bool unmount(const std::filesystem::path &path);

	/// @brief IFileSystem interface implementation.
	/// These methods find the correct mount point and forward the call.
	container::unique_ptr<IStream> openStream(const std::filesystem::path &path) override;
	container::unique_ptr<IBlob>   readFile(const std::filesystem::path &path) override;
	bool                           fileExists(const std::filesystem::path &path) override;
	bool                           folderExists(const std::filesystem::path &path) override;
	void                           enumerateFiles(const std::filesystem::path &path, enumerate_callback_t callback) override;
	void                           enumerateDirectories(const std::filesystem::path &path, enumerate_callback_t callback) override;

  private:
	/// @brief Finds the file system and relative path for a given virtual path.
	/// @param path The full virtual path.
	/// @param[out] outRelativePath The path relative to the found file system.
	/// @return A pointer to the file system that handles this path, or nullptr.
	IFileSystem *findMountPoint(const std::filesystem::path &path, std::filesystem::path &outRelativePath) const;

	// Using a vector of pairs sorted by path length (desc) to handle nested mounts correctly.
	// e.g., if "/assets" and "/assets/textures" are mounted, a lookup for
	// "/assets/textures/diffuse.png" should resolve to the latter.
	container::vector<std::pair<container::string, container::shared_ptr<IFileSystem>>> m_mountPoints;
};

}        // namespace msplat::vfs