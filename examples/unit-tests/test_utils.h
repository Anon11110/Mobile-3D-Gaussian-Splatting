#pragma once

#include <filesystem>

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	define NOMINMAX
#	include <windows.h>
#elif defined(__APPLE__)
#	include <mach-o/dyld.h>
#endif

inline std::filesystem::path GetExeDirectory()
{
#ifdef _WIN32
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(nullptr, path, MAX_PATH);
	return std::filesystem::path(path).parent_path();
#elif defined(__APPLE__)
	char     path[1024];
	uint32_t size = sizeof(path);
	if (_NSGetExecutablePath(path, &size) == 0)
		return std::filesystem::path(path).parent_path();
	return std::filesystem::current_path();
#else
	// Linux: /proc/self/exe
	return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}

// Find a PLY file for tests that need splat assets.
// Assets are copied to <exe_dir>/assets/ by setup_splat_assets() in CMake.
inline std::filesystem::path FindTestPly()
{
	auto exeDir = GetExeDirectory();

	std::filesystem::path candidates[] = {
	    exeDir / "assets" / "flowers_1.ply",
	    exeDir / "assets" / "train_30000.ply",
	    exeDir / "assets" / "bicycle_7000.ply",
	};

	for (const auto &candidate : candidates)
	{
		if (std::filesystem::exists(candidate))
			return candidate;
	}

	return {};
}
