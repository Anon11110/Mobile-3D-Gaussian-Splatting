#pragma once

#include <cstdint>

namespace msplat::profiling
{

// Process memory statistics
struct ProcessMemoryStats
{
	// Physical RAM currently in use
	// Windows: WorkingSetSize, Linux/Android: VmRSS
	uint64_t workingSetBytes = 0;

	// Private allocated memory, including swapped memory
	// Windows: PrivateUsage, Linux/Android: VmData + VmStk
	uint64_t privateBytes = 0;
};

ProcessMemoryStats GetProcessMemoryStats();

}        // namespace msplat::profiling
