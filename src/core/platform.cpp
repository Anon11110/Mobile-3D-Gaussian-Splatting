#include "msplat/core/platform.h"

#include <cstdlib>
#include <sstream>
#include <algorithm>

// Platform detection
#if defined(_WIN32) || defined(_WIN64)
#	define MSPLAT_PLATFORM_WINDOWS
#	include <windows.h>
#	include <dbghelp.h>
#	include <malloc.h>
#	pragma comment(lib, "dbghelp.lib")
#elif defined(__APPLE__)
#	define MSPLAT_PLATFORM_APPLE
#	include <TargetConditionals.h>
#	include <dlfcn.h>
#	include <execinfo.h>
#	include <sys/sysctl.h>
#	include <unistd.h>
#	if TARGET_OS_IOS
#		define MSPLAT_PLATFORM_IOS
#	else
#		define MSPLAT_PLATFORM_MACOS
#	endif
#elif defined(__ANDROID__)
#	define MSPLAT_PLATFORM_ANDROID
#	include <dlfcn.h>
#	include <malloc.h>
#	include <unistd.h>
#	include <unwind.h>
#elif defined(__linux__)
#	define MSPLAT_PLATFORM_LINUX
#	include <dlfcn.h>
#	include <execinfo.h>
#	include <unistd.h>
#endif

namespace msplat::core
{

void *aligned_malloc(size_t size, size_t alignment)
{
	if (size == 0 || (alignment & (alignment - 1)) != 0)
	{
		return nullptr;        // Invalid parameters
	}

#ifdef MSPLAT_PLATFORM_WINDOWS
	return _aligned_malloc(size, alignment);
#else
	void *ptr = nullptr;

#	if defined(MSPLAT_PLATFORM_ANDROID)
	// Android supports memalign
	ptr = memalign(alignment, size);
#	else
	// POSIX-compliant systems (macOS, iOS, Linux)
	// posix_memalign requires alignment to be a power of 2 and >= sizeof(void*)
	size_t min_alignment = std::max(alignment, sizeof(void*));
	if (posix_memalign(&ptr, min_alignment, size) != 0)
	{
		ptr = nullptr;
	}
#	endif

	return ptr;
#endif
}

void aligned_free(void *ptr)
{
	if (ptr == nullptr)
	{
		return;
	}

#ifdef MSPLAT_PLATFORM_WINDOWS
	_aligned_free(ptr);
#else
	free(ptr);
#endif
}

size_t get_page_size()
{
#ifdef MSPLAT_PLATFORM_WINDOWS
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return static_cast<size_t>(system_info.dwPageSize);
#else
	// POSIX systems (macOS, iOS, Linux, Android)
	long page_size = sysconf(_SC_PAGESIZE);
	return page_size > 0 ? static_cast<size_t>(page_size) : 4096;        // Default fallback
#endif
}

size_t get_cache_line_size()
{
#ifdef MSPLAT_PLATFORM_WINDOWS
	// Try to get from system info, fallback to typical x64 size
	SYSTEM_LOGICAL_PROCESSOR_INFORMATION *buffer      = nullptr;
	DWORD                                 buffer_size = 0;

	GetLogicalProcessorInformation(buffer, &buffer_size);
	buffer = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION *>(std::malloc(buffer_size));

	if (GetLogicalProcessorInformation(buffer, &buffer_size))
	{
		for (DWORD i = 0; i < buffer_size / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION); ++i)
		{
			if (buffer[i].Relationship == RelationCache && buffer[i].Cache.Level == 1)
			{
				size_t cache_line_size = buffer[i].Cache.LineSize;
				std::free(buffer);
				return cache_line_size;
			}
		}
	}

	if (buffer)
		std::free(buffer);
	return 64;        // Default for x86/x64

#elif defined(MSPLAT_PLATFORM_APPLE)
	size_t cache_line_size = 0;
	size_t size            = sizeof(cache_line_size);

#	ifdef MSPLAT_PLATFORM_IOS
	// iOS: ARM typically uses 64-byte cache lines
	return 64;
#	else
	// macOS: Query system
	if (sysctlbyname("hw.cachelinesize", &cache_line_size, &size, nullptr, 0) == 0)
	{
		return cache_line_size;
	}
	return 64;        // Default fallback
#	endif

#elif defined(MSPLAT_PLATFORM_ANDROID)
	// Android ARM devices typically use 64-byte cache lines
	// Could read from /sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size
	// but 64 bytes is the most common for modern ARM
	return 64;

#elif defined(MSPLAT_PLATFORM_LINUX)
	// Try to read from sysfs first
	FILE *fp = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
	if (fp)
	{
		size_t cache_line_size = 0;
		if (fscanf(fp, "%zu", &cache_line_size) == 1)
		{
			fclose(fp);
			return cache_line_size;
		}
		fclose(fp);
	}

	// Fallback to typical x86/x64 size
	return 64;
#else
	return 64;        // Safe default
#endif
}

std::vector<std::string> get_backtrace(int max_frames)
{
	std::vector<std::string> result;

#ifndef NDEBUG        // Only in debug builds

#	ifdef MSPLAT_PLATFORM_WINDOWS
	void  *stack[256];
	HANDLE process = GetCurrentProcess();

	SymInitialize(process, nullptr, TRUE);

	WORD frames = CaptureStackBackTrace(0, min(max_frames, 256), stack, nullptr);

	for (int i = 0; i < frames; ++i)
	{
		DWORD64 address = reinterpret_cast<DWORD64>(stack[i]);

		char         buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO symbol  = reinterpret_cast<PSYMBOL_INFO>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen   = MAX_SYM_NAME;

		std::ostringstream oss;
		if (SymFromAddr(process, address, nullptr, symbol))
		{
			oss << symbol->Name << " [0x" << std::hex << address << "]";
		}
		else
		{
			oss << "Unknown [0x" << std::hex << address << "]";
		}
		result.push_back(oss.str());
	}

#	elif defined(MSPLAT_PLATFORM_APPLE) || defined(MSPLAT_PLATFORM_LINUX)
	void  *stack[256];
	int    frames  = backtrace(stack, std::min(max_frames, 256));
	char **symbols = backtrace_symbols(stack, frames);

	if (symbols)
	{
		for (int i = 0; i < frames; ++i)
		{
			result.push_back(std::string(symbols[i]));
		}
		std::free(symbols);
	}

#	elif defined(MSPLAT_PLATFORM_ANDROID)
	// Android unwind implementation
	struct BacktraceData
	{
		std::vector<std::string> *result;
		int                       count;
		int                       max_frames;
	};

	BacktraceData data = {&result, 0, max_frames};

	_Unwind_Backtrace([](struct _Unwind_Context *context, void *arg) -> _Unwind_Reason_Code {
		BacktraceData *data = static_cast<BacktraceData *>(arg);
		if (data->count >= data->max_frames)
		{
			return _URC_END_OF_STACK;
		}

		uintptr_t pc = _Unwind_GetIP(context);

		Dl_info info;
		if (dladdr(reinterpret_cast<void *>(pc), &info))
		{
			std::ostringstream oss;
			oss << (info.dli_sname ? info.dli_sname : "Unknown")
			    << " [0x" << std::hex << pc << "]";
			data->result->push_back(oss.str());
		}
		else
		{
			std::ostringstream oss;
			oss << "Unknown [0x" << std::hex << pc << "]";
			data->result->push_back(oss.str());
		}

		data->count++;
		return _URC_NO_REASON;
	},
	                  &data);

#	endif

#endif        // NDEBUG

	return result;
}

}        // namespace msplat::core