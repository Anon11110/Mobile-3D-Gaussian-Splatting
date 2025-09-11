#pragma once

#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/string.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/vfs.h>
#include <rhi/rhi.h>
#include <rhi/rhi_types.h>

namespace msplat::engine
{

/**
 * @struct ShaderMacro
 * @brief A simple key-value pair for shader preprocessor macros.
 *        Useful for shader permutations and conditional compilation.
 */
struct ShaderMacro
{
	container::string name;         // Macro name (e.g., "USE_NORMAL_MAPPING")
	container::string value;        // Macro value (e.g., "1")
};

/**
 * @class ShaderFactory
 * @brief Factory class for efficient shader loading and caching.
 *
 * This class provides centralized shader management with:
 * - Bytecode caching to avoid redundant file I/O
 * - Compiled shader caching for reuse across frames
 * - Optional VFS integration for flexible file loading
 * - Support for shader macros/permutations
 *
 * Example usage:
 * @code
 * ShaderFactory factory(device.get());
 * auto vertexShader = factory.getOrCreateShader(
 *     "shaders/compiled/basic.vert.spv",
 *     rhi::ShaderStage::VERTEX
 * );
 * @endcode
 */
class ShaderFactory
{
  public:
	/**
	 * @brief Constructor.
	 * @param device The RHI device used to create shader objects. Must not be null.
	 * @param vfs Optional virtual file system for loading shader files.
	 *            If nullptr, falls back to direct file loading via vfs::readFile().
	 */
	explicit ShaderFactory(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs = nullptr);

	/**
	 * @brief Loads a shader or returns a cached instance.
	 * @param filepath Path to the compiled shader file (e.g., "shaders/compiled/basic.vert.spv")
	 * @param stage The shader stage (VERTEX, FRAGMENT, or COMPUTE)
	 * @param macros Optional array of macros for shader permutations (currently reserved for future use)
	 * @return A unique_ptr to the shader object, or nullptr on failure
	 *
	 * This method will:
	 * 1. Check if the shader is already cached (based on filepath + stage + macros)
	 * 2. If not cached, load the bytecode (checking bytecode cache first)
	 * 3. Create the shader object via RHI
	 * 4. Cache and return the result
	 */
	container::unique_ptr<rhi::IRHIShader> getOrCreateShader(
	    const container::string           &filepath,
	    rhi::ShaderStage                   stage,
	    container::span<const ShaderMacro> macros = {});

	/**
	 * @brief Clears all internal caches.
	 *
	 * This will:
	 * - Clear the bytecode cache (raw file data)
	 * - Clear the shader cache (compiled shader objects)
	 *
	 * Note: Existing returned shader pointers remain valid as they are unique_ptrs.
	 *       This only affects future cache lookups.
	 */
	void clearCache();

	/**
	 * @brief Clears only the bytecode cache.
	 *
	 * Useful when you want to reload shader files from disk
	 * but keep compiled shader objects.
	 */
	void clearBytecodeCache();

	/**
	 * @brief Gets the number of cached bytecode entries.
	 * @return The number of cached bytecode buffers
	 */
	[[nodiscard]] size_t getBytecodeaCacheSize() const;

	/**
	 * @brief Gets the number of cached shader objects.
	 * @return The number of cached compiled shaders
	 */
	[[nodiscard]] size_t getShaderCacheSize() const;

  private:
	/**
	 * @brief Loads shader bytecode from file system.
	 * @param filepath The path to the shader file
	 * @return The loaded bytecode, or empty vector on failure
	 *
	 * This method will:
	 * 1. Check the bytecode cache first
	 * 2. If not cached, load via VFS (if available) or direct file read
	 * 3. Cache the result for future use
	 */
	container::vector<uint8_t> loadShaderBytecode(const container::string &filepath);

	/**
	 * @brief Generates a unique cache key for a shader variant.
	 * @param filepath The shader file path
	 * @param stage The shader stage
	 * @param macros The shader macros
	 * @return A hash value suitable for use as a map key
	 *
	 * This combines the filepath, stage, and macros into a single hash value
	 * using the custom msplat::container::hash implementation.
	 */
	size_t generateCacheKey(const container::string           &filepath,
	                        rhi::ShaderStage                   stage,
	                        container::span<const ShaderMacro> macros) const;

	// Non-owning pointer to the RHI device
	rhi::IRHIDevice *m_device;

	// Optional VFS for file loading (may be nullptr)
	container::shared_ptr<vfs::IFileSystem> m_vfs;

	// Cache for raw shader bytecode to avoid redundant file reads
	// Key: filepath (e.g., "shaders/compiled/basic.vert.spv")
	// Value: The raw bytecode
	container::unordered_map<container::string, container::vector<uint8_t>> m_bytecodeCache;

	// Cache for compiled shader objects
	// Key: Hash of (filepath + stage + macros)
	// Value: The compiled shader object
	// Note: We store raw pointers here since we're transferring ownership to callers
	container::unordered_map<size_t, rhi::IRHIShader *> m_shaderCache;
};

}        // namespace msplat::engine