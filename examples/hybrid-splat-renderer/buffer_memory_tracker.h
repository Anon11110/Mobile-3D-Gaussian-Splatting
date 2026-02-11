#pragma once

#include <cstdint>
#include <cstring>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <rhi/rhi.h>

// Memory type for GPU buffer allocation
enum class MemoryType
{
	DeviceLocal,        // GPU-only memory (fast, not CPU-accessible)
	HostVisible         // CPU-accessible memory (for uploads/readbacks)
};

struct BufferMemoryEntry
{
	const char *name;
	size_t      size;
	const char *category;        // "Scene", "App", "Sorter"
	MemoryType  memType;
};

struct TextureMemoryEntry
{
	const char        *name;
	uint32_t           width;
	uint32_t           height;
	rhi::TextureFormat format;
	uint32_t           count;        // For arrays like swapchain images
	size_t             totalSize;
	const char        *category;        // "Swapchain", "RenderTarget", "ImGui"
	MemoryType         memType;
};

struct CpuMemoryEntry
{
	const char *name;
	size_t      size;
	const char *category;        // "Scene", "Sorter"
};

class BufferMemoryTracker
{
  public:
	void Clear()
	{
		m_entries.clear();
		m_textureEntries.clear();
		m_cpuEntries.clear();
		m_vmaDeviceLocal = 0;
		m_vmaHostVisible = 0;
		m_cpuRss         = 0;
		m_cpuPrivate     = 0;
	}

	void SetVmaStats(uint64_t deviceLocalUsage, uint64_t hostVisibleUsage)
	{
		m_vmaDeviceLocal = deviceLocalUsage;
		m_vmaHostVisible = hostVisibleUsage;
	}

	void SetCpuStats(uint64_t rssBytes, uint64_t privateBytes)
	{
		m_cpuRss     = rssBytes;
		m_cpuPrivate = privateBytes;
	}

	void AddBuffer(const char *name, rhi::IRHIBuffer *buffer, const char *category,
	               MemoryType memType = MemoryType::DeviceLocal)
	{
		if (buffer)
		{
			m_entries.push_back({name, buffer->GetSize(), category, memType});
		}
	}

	void AddBuffer(const char *name, const rhi::BufferHandle &buffer, const char *category,
	               MemoryType memType = MemoryType::DeviceLocal)
	{
		AddBuffer(name, buffer.Get(), category, memType);
	}

	void AddTexture(const char *name, rhi::IRHITexture *texture, const char *category,
	                uint32_t count = 1, MemoryType memType = MemoryType::DeviceLocal)
	{
		if (texture)
		{
			uint32_t w      = texture->GetWidth();
			uint32_t h      = texture->GetHeight();
			auto     format = texture->GetFormat();
			size_t   size   = CalculateTextureSize(w, h, format) * count;
			m_textureEntries.push_back({name, w, h, format, count, size, category, memType});
		}
	}

	void AddTexture(const char *name, const rhi::TextureHandle &texture, const char *category,
	                uint32_t count = 1, MemoryType memType = MemoryType::DeviceLocal)
	{
		AddTexture(name, texture.Get(), category, count, memType);
	}

	void AddCpuMemory(const char *name, size_t size, const char *category)
	{
		if (size > 0)
		{
			m_cpuEntries.push_back({name, size, category});
		}
	}

	void LogMemoryReport() const
	{
		LOG_INFO("=== Buffer Memory Report ===");

		size_t sceneTotal = 0, appTotal = 0, sorterTotal = 0;
		size_t deviceLocalTotal = 0, hostVisibleTotal = 0;

		// Log Scene buffers
		LOG_INFO("");
		LOG_INFO("Scene Buffers:");
		for (const auto &entry : m_entries)
		{
			if (std::strcmp(entry.category, "Scene") == 0)
			{
				LogEntry(entry);
				sceneTotal += entry.size;
				AccumulateByMemType(entry, deviceLocalTotal, hostVisibleTotal);
			}
		}
		LOG_INFO("  Scene Subtotal: {:.2f} MB", sceneTotal / 1048576.0);

		// Log App buffers
		LOG_INFO("");
		LOG_INFO("App Buffers:");
		for (const auto &entry : m_entries)
		{
			if (std::strcmp(entry.category, "App") == 0)
			{
				LogEntry(entry);
				appTotal += entry.size;
				AccumulateByMemType(entry, deviceLocalTotal, hostVisibleTotal);
			}
		}
		LOG_INFO("  App Subtotal: {:.2f} MB", appTotal / 1048576.0);

		// Log Sorter buffers
		LOG_INFO("");
		LOG_INFO("GPU Sorter Buffers:");
		for (const auto &entry : m_entries)
		{
			if (std::strcmp(entry.category, "Sorter") == 0)
			{
				LogEntry(entry);
				sorterTotal += entry.size;
				AccumulateByMemType(entry, deviceLocalTotal, hostVisibleTotal);
			}
		}
		LOG_INFO("  Sorter Subtotal: {:.2f} MB", sorterTotal / 1048576.0);

		// Log Textures
		size_t textureTotal = 0;
		if (!m_textureEntries.empty())
		{
			LOG_INFO("");
			LOG_INFO("Textures:");
			for (const auto &entry : m_textureEntries)
			{
				LogTextureEntry(entry);
				textureTotal += entry.totalSize;
				if (entry.memType == MemoryType::DeviceLocal)
					deviceLocalTotal += entry.totalSize;
				else
					hostVisibleTotal += entry.totalSize;
			}
			LOG_INFO("  Textures Subtotal: {:.2f} MB", textureTotal / 1048576.0);
		}

		// Log CPU Memory
		size_t cpuTotal = 0;
		if (!m_cpuEntries.empty())
		{
			LOG_INFO("");
			LOG_INFO("CPU Host Memory:");
			for (const auto &entry : m_cpuEntries)
			{
				LogCpuEntry(entry);
				cpuTotal += entry.size;
			}
			LOG_INFO("  CPU Subtotal: {:.2f} MB", cpuTotal / 1048576.0);
		}

		// Summary
		size_t gpuTotal = sceneTotal + appTotal + sorterTotal + textureTotal;
		LOG_INFO("");
		LOG_INFO("=== GPU Memory Summary ===");
		if (gpuTotal > 0)
		{
			LOG_INFO("  Scene:    {:.2f} MB ({:.1f}%)", sceneTotal / 1048576.0, 100.0 * sceneTotal / gpuTotal);
			LOG_INFO("  App:      {:.2f} MB ({:.1f}%)", appTotal / 1048576.0, 100.0 * appTotal / gpuTotal);
			LOG_INFO("  Sorter:   {:.2f} MB ({:.1f}%)", sorterTotal / 1048576.0, 100.0 * sorterTotal / gpuTotal);
			if (textureTotal > 0)
			{
				LOG_INFO("  Textures: {:.2f} MB ({:.1f}%)", textureTotal / 1048576.0, 100.0 * textureTotal / gpuTotal);
			}
		}
		LOG_INFO("  GPU TOTAL: {:.2f} MB", gpuTotal / 1048576.0);
		LOG_INFO("");
		LOG_INFO("=== By Memory Type ===");
		LOG_INFO("  Device-Local:  {:.2f} MB", deviceLocalTotal / 1048576.0);
		LOG_INFO("  Host-Visible:  {:.2f} MB", hostVisibleTotal / 1048576.0);
		if (cpuTotal > 0)
		{
			LOG_INFO("  CPU Host:      {:.2f} MB", cpuTotal / 1048576.0);
		}
		LOG_INFO("===========================");

		if (m_vmaDeviceLocal > 0 || m_vmaHostVisible > 0)
		{
			LOG_INFO("");
			LOG_INFO("=== GPU Memory Tracking Notes ===");

			size_t trackedGpu = deviceLocalTotal + hostVisibleTotal;
			size_t vmaTotal   = m_vmaDeviceLocal + m_vmaHostVisible;

			LOG_INFO("  Tracked GPU:   {:.2f} MB", trackedGpu / 1048576.0);
			LOG_INFO("  VMA Reports:   {:.2f} MB", vmaTotal / 1048576.0);

			if (vmaTotal > trackedGpu)
			{
				size_t gap = vmaTotal - trackedGpu;
				LOG_INFO("  Untracked:     {:.2f} MB (pipelines, descriptors, ImGui, staging pools)",
				         gap / 1048576.0);
			}
			LOG_INFO("=============================");
		}

		if (m_cpuRss > 0)
		{
			LOG_INFO("");
			LOG_INFO("=== CPU Memory Tracking Notes ===");
			LOG_INFO("  Tracked CPU:   {:.2f} MB", cpuTotal / 1048576.0);
			LOG_INFO("  OS RSS:        {:.2f} MB", m_cpuRss / 1048576.0);

			if (m_cpuRss > cpuTotal)
			{
				size_t cpuGap = m_cpuRss - cpuTotal;
				LOG_INFO("  Untracked:     {:.2f} MB (VMA overhead, driver, descriptors, ImGui, libs)",
				         cpuGap / 1048576.0);
			}

			if (m_cpuPrivate > 0)
			{
				LOG_INFO("  OS Private:    {:.2f} MB (includes reserved virtual memory)",
				         m_cpuPrivate / 1048576.0);
			}
			LOG_INFO("=================================");
		}
	}

  private:
	static const char *MemTypeStr(MemoryType memType)
	{
		return memType == MemoryType::DeviceLocal ? "Device" : "Host";
	}

	static void AccumulateByMemType(const BufferMemoryEntry &entry, size_t &deviceLocal, size_t &hostVisible)
	{
		if (entry.memType == MemoryType::DeviceLocal)
			deviceLocal += entry.size;
		else
			hostVisible += entry.size;
	}

	static size_t CalculateTextureSize(uint32_t width, uint32_t height, rhi::TextureFormat format)
	{
		size_t bytesPerPixel = 0;
		switch (format)
		{
			case rhi::TextureFormat::R8_UNORM:
				bytesPerPixel = 1;
				break;
			case rhi::TextureFormat::RG8_UNORM:
				bytesPerPixel = 2;
				break;
			case rhi::TextureFormat::R16_FLOAT:
				bytesPerPixel = 2;
				break;
			case rhi::TextureFormat::RG16_FLOAT:
				bytesPerPixel = 4;
				break;
			case rhi::TextureFormat::R32_FLOAT:
				bytesPerPixel = 4;
				break;
			case rhi::TextureFormat::RG32_FLOAT:
				bytesPerPixel = 8;
				break;
			case rhi::TextureFormat::R8G8B8A8_UNORM:
			case rhi::TextureFormat::R8G8B8A8_SRGB:
			case rhi::TextureFormat::B8G8R8A8_UNORM:
			case rhi::TextureFormat::B8G8R8A8_SRGB:
			case rhi::TextureFormat::R11G11B10_FLOAT:
				bytesPerPixel = 4;
				break;
			case rhi::TextureFormat::RGBA16_FLOAT:
				bytesPerPixel = 8;
				break;
			case rhi::TextureFormat::R32G32B32_FLOAT:
				bytesPerPixel = 12;
				break;
			case rhi::TextureFormat::RGBA32_FLOAT:
				bytesPerPixel = 16;
				break;
			case rhi::TextureFormat::D32_FLOAT:
				bytesPerPixel = 4;
				break;
			case rhi::TextureFormat::D24_UNORM_S8_UINT:
				bytesPerPixel = 4;
				break;
			case rhi::TextureFormat::D32_SFLOAT_S8_UINT:
				bytesPerPixel = 8;
				break;
			default:
				bytesPerPixel = 4;
				break;
		}
		return static_cast<size_t>(width) * height * bytesPerPixel;
	}

	static const char *FormatToStr(rhi::TextureFormat format)
	{
		switch (format)
		{
			case rhi::TextureFormat::R8G8B8A8_UNORM:
				return "RGBA8";
			case rhi::TextureFormat::R8G8B8A8_SRGB:
				return "RGBA8_SRGB";
			case rhi::TextureFormat::B8G8R8A8_UNORM:
				return "BGRA8";
			case rhi::TextureFormat::B8G8R8A8_SRGB:
				return "BGRA8_SRGB";
			case rhi::TextureFormat::D32_FLOAT:
				return "D32F";
			case rhi::TextureFormat::D24_UNORM_S8_UINT:
				return "D24S8";
			case rhi::TextureFormat::D32_SFLOAT_S8_UINT:
				return "D32S8";
			case rhi::TextureFormat::RGBA16_FLOAT:
				return "RGBA16F";
			case rhi::TextureFormat::RGBA32_FLOAT:
				return "RGBA32F";
			default:
				return "Unknown";
		}
	}

	void LogEntry(const BufferMemoryEntry &entry) const
	{
		const char *memTypeStr = MemTypeStr(entry.memType);
		if (entry.size >= 1048576)
		{
			LOG_INFO("  {:<25} {:>10.2f} MB  [{}]", entry.name, entry.size / 1048576.0, memTypeStr);
		}
		else if (entry.size >= 1024)
		{
			LOG_INFO("  {:<25} {:>10.2f} KB  [{}]", entry.name, entry.size / 1024.0, memTypeStr);
		}
		else
		{
			LOG_INFO("  {:<25} {:>10} B   [{}]", entry.name, entry.size, memTypeStr);
		}
	}

	void LogTextureEntry(const TextureMemoryEntry &entry) const
	{
		const char *memTypeStr = MemTypeStr(entry.memType);
		const char *formatStr  = FormatToStr(entry.format);
		if (entry.count > 1)
		{
			LOG_INFO("  {:<25} {:>10.2f} MB  [{}] ({}x{} {} x{})",
			         entry.name, entry.totalSize / 1048576.0, memTypeStr,
			         entry.width, entry.height, formatStr, entry.count);
		}
		else
		{
			LOG_INFO("  {:<25} {:>10.2f} MB  [{}] ({}x{} {})",
			         entry.name, entry.totalSize / 1048576.0, memTypeStr,
			         entry.width, entry.height, formatStr);
		}
	}

	void LogCpuEntry(const CpuMemoryEntry &entry) const
	{
		if (entry.size >= 1048576)
		{
			LOG_INFO("  {:<25} {:>10.2f} MB  [{}]", entry.name, entry.size / 1048576.0, entry.category);
		}
		else if (entry.size >= 1024)
		{
			LOG_INFO("  {:<25} {:>10.2f} KB  [{}]", entry.name, entry.size / 1024.0, entry.category);
		}
		else
		{
			LOG_INFO("  {:<25} {:>10} B   [{}]", entry.name, entry.size, entry.category);
		}
	}

	msplat::container::vector<BufferMemoryEntry>  m_entries;
	msplat::container::vector<TextureMemoryEntry> m_textureEntries;
	msplat::container::vector<CpuMemoryEntry>     m_cpuEntries;

	// VMA-reported stats for gap analysis
	uint64_t m_vmaDeviceLocal = 0;
	uint64_t m_vmaHostVisible = 0;

	// OS-reported CPU stats for gap analysis
	uint64_t m_cpuRss     = 0;
	uint64_t m_cpuPrivate = 0;
};
