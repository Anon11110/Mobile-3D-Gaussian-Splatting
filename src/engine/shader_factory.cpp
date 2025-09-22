#include <msplat/core/log.h>
#include <msplat/engine/shader_factory.h>
#include <sstream>

namespace msplat::engine
{

ShaderFactory::ShaderFactory(rhi::IRHIDevice *device, container::shared_ptr<vfs::IFileSystem> vfs) :
    m_device(device), m_bytecodeCache(container::make_unordered_map_default<container::string, container::vector<uint8_t>>()), m_shaderCache(container::make_unordered_map_default<size_t, rhi::IRHIShader *>())
{
	LOG_ASSERT(device != nullptr, "ShaderFactory requires a valid RHI device");

	if (vfs)
	{
		m_vfs = vfs;
		LOG_DEBUG("ShaderFactory created with provided VFS");
	}
	else
	{
		// Create default VFS with current working directory mounted at root
		auto rootFs = container::make_shared<vfs::RootFileSystem>();
		auto cwd    = container::filesystem::current_path();
		rootFs->mount("/", container::make_shared<vfs::NativeFileSystem>(cwd));
		m_vfs = rootFs;
		LOG_DEBUG("ShaderFactory created with default VFS (cwd: {})", cwd.string());
	}
}

rhi::ShaderHandle ShaderFactory::getOrCreateShader(
    const container::string           &filepath,
    rhi::ShaderStage                   stage,
    container::span<const ShaderMacro> macros)
{
	// Generate cache key for this shader variant
	size_t cacheKey = generateCacheKey(filepath, stage, macros);

	// Check if shader is already cached
	auto it = m_shaderCache.find(cacheKey);
	if (it != m_shaderCache.end())
	{
		LOG_DEBUG("Shader cache hit for: {} (stage: {}, key: {})",
		          container::to_std_string(filepath),
		          static_cast<int>(stage),
		          cacheKey);
		// Note: We can't return the cached pointer directly since we need unique_ptr
		// For now, we'll recreate the shader each time but reuse the bytecode
		// A better approach would be to use shared_ptr in the cache, but that would
		// require changing the return type or the cache design
	}

	// Load shader bytecode (from cache or file)
	container::vector<uint8_t> bytecode = loadShaderBytecode(filepath);
	if (bytecode.empty())
	{
		LOG_ERROR("Failed to load shader bytecode from: {}", container::to_std_string(filepath));
		return nullptr;
	}

	// Create shader descriptor
	rhi::ShaderDesc shaderDesc{};
	shaderDesc.stage      = stage;
	shaderDesc.code       = bytecode.data();
	shaderDesc.codeSize   = bytecode.size();
	shaderDesc.entryPoint = "main";        // SPIR-V shaders typically use "main"

	// Create the shader object
	rhi::ShaderHandle shader = m_device->CreateShader(shaderDesc);
	if (!shader)
	{
		LOG_ERROR("Failed to create shader from: {} (stage: {})",
		          container::to_std_string(filepath),
		          static_cast<int>(stage));
		return nullptr;
	}

	LOG_DEBUG("Successfully created shader from: {} (stage: {}, size: {} bytes)",
	          container::to_std_string(filepath),
	          static_cast<int>(stage),
	          bytecode.size());

	// Cache the raw pointer for existence checking (we don't own it after returning)
	// This is mainly useful for tracking what shaders have been created
	m_shaderCache[cacheKey] = shader.Get();

	return shader;
}

void ShaderFactory::clearCache()
{
	LOG_INFO("Clearing all shader factory caches");
	m_bytecodeCache.clear();
	m_shaderCache.clear();
}

void ShaderFactory::clearBytecodeCache()
{
	LOG_INFO("Clearing shader bytecode cache");
	m_bytecodeCache.clear();
}

size_t ShaderFactory::getBytecodeaCacheSize() const
{
	return m_bytecodeCache.size();
}

size_t ShaderFactory::getShaderCacheSize() const
{
	return m_shaderCache.size();
}

container::vector<uint8_t> ShaderFactory::loadShaderBytecode(const container::string &filepath)
{
	// Check bytecode cache first
	auto it = m_bytecodeCache.find(filepath);
	if (it != m_bytecodeCache.end())
	{
		LOG_DEBUG("Bytecode cache hit for: {}", container::to_std_string(filepath));
		return it->second;
	}

	// Load from file system
	container::vector<uint8_t> bytecode;

	// Convert filepath to filesystem path
	container::filesystem::path filePath(container::to_std_string(filepath));

	// Ensure path starts with "/" for VFS (if it's a relative path)
	if (!filePath.is_absolute() && !filepath.empty() && filepath[0] != '/')
	{
		filePath = container::filesystem::path("/") / filePath;
	}

	// Use VFS to load the file
	auto blob = m_vfs->readFile(filePath);
	if (blob)
	{
		// Convert from IBlob to vector<uint8_t>
		const std::byte *data = blob->data();
		size_t           size = blob->size();
		bytecode.resize(size);
		std::memcpy(bytecode.data(), data, size);
		LOG_DEBUG("Loaded shader via VFS: {} ({} bytes)", container::to_std_string(filepath), size);
	}
	else
	{
		LOG_ERROR("Failed to load shader file: {}", container::to_std_string(filepath));
		return container::vector<uint8_t>();
	}

	if (!bytecode.empty())
	{
		// Cache the loaded bytecode
		m_bytecodeCache[filepath] = bytecode;
		LOG_DEBUG("Cached shader bytecode for: {} ({} bytes)",
		          container::to_std_string(filepath),
		          bytecode.size());
	}

	return bytecode;
}

size_t ShaderFactory::generateCacheKey(const container::string           &filepath,
                                       rhi::ShaderStage                   stage,
                                       container::span<const ShaderMacro> macros) const
{
	// Use the custom hash implementation from container::hash
	container::hash<container::string> stringHasher;
	container::hash<int>               intHasher;

	// Start with filepath hash
	size_t hash = stringHasher(filepath);

	// Combine with stage (using simple XOR + bit rotation for mixing)
	size_t stageHash = intHasher(static_cast<int>(stage));
	hash ^= (stageHash << 1) | (stageHash >> (sizeof(size_t) * 8 - 1));

	// Combine with macros
	for (const auto &macro : macros)
	{
		size_t macroHash = stringHasher(macro.name);
		hash ^= (macroHash << 2) | (macroHash >> (sizeof(size_t) * 8 - 2));

		size_t valueHash = stringHasher(macro.value);
		hash ^= (valueHash << 3) | (valueHash >> (sizeof(size_t) * 8 - 3));
	}

	return hash;
}

}        // namespace msplat::engine