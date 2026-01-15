#include <msplat/core/profiling/memory_profiler.h>

#if defined(_WIN32)

#	include <msplat/core/windows_sanitized.h>
#	include <psapi.h>

namespace msplat::profiling
{

ProcessMemoryStats GetProcessMemoryStats()
{
	ProcessMemoryStats         stats{};
	PROCESS_MEMORY_COUNTERS_EX pmc{};
	pmc.cb = sizeof(pmc);

	if (GetProcessMemoryInfo(GetCurrentProcess(),
	                         reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&pmc),
	                         sizeof(pmc)))
	{
		stats.workingSetBytes = pmc.WorkingSetSize;
		stats.privateBytes    = pmc.PrivateUsage;
	}
	return stats;
}

}        // namespace msplat::profiling

#elif defined(__ANDROID__) || defined(__linux__)

#	include <cstdio>
#	include <fstream>
#	include <string>

namespace msplat::profiling
{

ProcessMemoryStats GetProcessMemoryStats()
{
	ProcessMemoryStats stats{};
	std::ifstream      status("/proc/self/status");
	std::string        line;

	uint64_t vmData = 0;
	uint64_t vmStk  = 0;

	while (std::getline(status, line))
	{
		unsigned long long value = 0;
		if (std::sscanf(line.c_str(), "VmRSS: %llu kB", &value) == 1)
		{
			stats.workingSetBytes = value * 1024;
		}
		else if (std::sscanf(line.c_str(), "VmData: %llu kB", &value) == 1)
		{
			vmData = value * 1024;
		}
		else if (std::sscanf(line.c_str(), "VmStk: %llu kB", &value) == 1)
		{
			vmStk = value * 1024;
		}
	}

	// VmData + VmStk = private allocated memory (heap + stack)
	// This includes swapped memory, matching Windows PrivateUsage semantics
	stats.privateBytes = vmData + vmStk;

	return stats;
}

}        // namespace msplat::profiling

#else

// Fallback for unsupported platforms
namespace msplat::profiling
{

ProcessMemoryStats GetProcessMemoryStats()
{
	return {};
}

}        // namespace msplat::profiling

#endif
