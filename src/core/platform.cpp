#include "msplat/core/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>

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
#	include <cxxabi.h>
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
#	include <cxxabi.h>
#endif

namespace msplat::core
{

// Helper function to demangle C++ symbols
namespace
{
#if defined(MSPLAT_PLATFORM_APPLE) || defined(MSPLAT_PLATFORM_LINUX)
msplat::container::string demangle(const char *name) noexcept
{
	int                       status = 0;
	char                     *buffer = abi::__cxa_demangle(name, nullptr, nullptr, &status);
	msplat::container::string demangled{buffer == nullptr ? name : buffer};
	free(buffer);
	return demangled;
}
#elif defined(MSPLAT_PLATFORM_WINDOWS)
msplat::container::string demangle(const char *name) noexcept
{
	// Windows symbols from SymFromAddr are already somewhat readable
	// For full demangling, would need UnDecorateSymbolName
	return msplat::container::string(name);
}
#else
msplat::container::string demangle(const char *name) noexcept
{
	return msplat::container::string(name);
}
#endif
}        // anonymous namespace

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
	size_t min_alignment = std::max(alignment, sizeof(void *));
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

std::string to_string(const TraceItem &item) noexcept
{
	std::ostringstream oss;
	oss << item.module << "(" << item.symbol;
	if (item.offset > 0)
	{
		oss << "+0x" << std::hex << item.offset;
	}
	oss << ") [0x" << std::hex << item.address << "]";
	return oss.str();
}

msplat::container::vector<TraceItem> get_backtrace(int max_frames)
{
	msplat::container::vector<TraceItem> result;

#ifndef NDEBUG        // Only in debug builds

#	ifdef MSPLAT_PLATFORM_WINDOWS
	void  *stack[256];
	HANDLE process = GetCurrentProcess();

	SymInitialize(process, nullptr, TRUE);

	WORD frames = CaptureStackBackTrace(0, (std::min) (max_frames, 256), stack, nullptr);

	result.reserve(frames > 0 ? frames - 1 : 0);
	for (int i = 1; i < frames; ++i)        // Skip first frame (current function)
	{
		TraceItem item{};
		item.address = reinterpret_cast<uint64_t>(stack[i]);

		char         buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
		PSYMBOL_INFO symbol  = reinterpret_cast<PSYMBOL_INFO>(buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen   = MAX_SYM_NAME;

		DWORD64 displacement = 0;
		if (SymFromAddr(process, item.address, &displacement, symbol))
		{
			item.symbol = demangle(symbol->Name);
			item.offset = static_cast<size_t>(displacement);

			// Get module name
			HMODULE hModule = nullptr;
			if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                      reinterpret_cast<LPCTSTR>(item.address), &hModule))
			{
				char module_name[MAX_PATH];
				if (GetModuleFileNameA(hModule, module_name, MAX_PATH))
				{
					// Extract just the filename from the full path
					const char *last_slash = strrchr(module_name, '\\');
					item.module            = msplat::container::string(last_slash ? last_slash + 1 : module_name);
				}
				else
				{
					item.module = msplat::container::string("unknown");
				}
			}
			else
			{
				item.module = msplat::container::string("unknown");
			}
		}
		else
		{
			item.symbol = msplat::container::string("unknown");
			item.module = msplat::container::string("unknown");
			item.offset = 0;
		}
		result.emplace_back(std::move(item));
	}

#	elif defined(MSPLAT_PLATFORM_APPLE) || defined(MSPLAT_PLATFORM_LINUX)
	void  *stack[256];
	int    frames  = backtrace(stack, std::min(max_frames, 256));
	char **symbols = backtrace_symbols(stack, frames);

	if (symbols)
	{
		result.reserve(frames > 0 ? frames - 1 : 0);
		for (int i = 1; i < frames; ++i)        // Skip first frame (current function)
		{
			TraceItem item{};
#		ifdef MSPLAT_PLATFORM_APPLE
			// Apple format: "index module address symbol + offset"
			std::istringstream iss{symbols[i]};
			int                index = 0;
			char               plus  = '+';
			std::string        module_str, symbol_str;
			iss >> index >> module_str >> std::hex >> item.address >> symbol_str >> plus >> std::dec >> item.offset;
			item.module = msplat::container::string(module_str.c_str());
			item.symbol = demangle(symbol_str.c_str());
#		else
			// Linux format: "binary_name(function_name+offset) [address]"
			std::string_view raw_item{symbols[i]};
			if (!raw_item.empty())
			{
				// Parse address
				auto right_bracket = raw_item.rfind(']');
				auto left_bracket  = raw_item.rfind("[0x");
				if (right_bracket != std::string::npos &&
				    left_bracket != std::string::npos &&
				    right_bracket > left_bracket + 3)
				{
					left_bracket += 3;
					auto address_str = raw_item.substr(left_bracket, right_bracket - left_bracket);
					item.address     = std::strtoull(address_str.data(), nullptr, 16);

					// Parse function name and offset
					raw_item               = raw_item.substr(0, left_bracket - 3);
					auto right_parenthesis = raw_item.rfind(')');
					auto left_parenthesis  = raw_item.rfind('(');
					if (right_parenthesis != std::string::npos &&
					    left_parenthesis != std::string::npos &&
					    right_parenthesis > left_parenthesis)
					{
						auto function_and_offset = raw_item.substr(left_parenthesis + 1, right_parenthesis - left_parenthesis - 1);
						auto plus                = function_and_offset.rfind("+0x");
						if (plus != std::string::npos)
						{
							plus += 3;
							auto offset_str = function_and_offset.substr(plus);
							auto function   = function_and_offset.substr(0, plus - 3);
							item.offset     = std::strtoull(offset_str.data(), nullptr, 16);
							item.symbol     = function.empty() ? msplat::container::string("unknown") : demangle(std::string(function).c_str());
						}
						else
						{
							item.symbol = function_and_offset.empty() ? msplat::container::string("unknown") : demangle(std::string(function_and_offset).c_str());
							item.offset = 0;
						}
						// Binary name
						auto binary_name = raw_item.substr(0, left_parenthesis);
						item.module      = msplat::container::string(binary_name.data(), binary_name.size());
					}
					else
					{
						item.module = msplat::container::string("unknown");
						item.symbol = msplat::container::string("unknown");
						item.offset = 0;
					}
				}
				else
				{
					item.address = reinterpret_cast<uint64_t>(stack[i]);
					item.module  = msplat::container::string("unknown");
					item.symbol  = msplat::container::string("unknown");
					item.offset  = 0;
				}
			}
			else
			{
				item.address = reinterpret_cast<uint64_t>(stack[i]);
				item.module  = msplat::container::string("unknown");
				item.symbol  = msplat::container::string("unknown");
				item.offset  = 0;
			}
#		endif
			result.emplace_back(std::move(item));
		}
		std::free(symbols);
	}

#	elif defined(MSPLAT_PLATFORM_ANDROID)
	// Android unwind implementation
	struct BacktraceData
	{
		msplat::container::vector<TraceItem> *result;
		int                                   count;
		int                                   max_frames;
		bool                                  skip_first;
	};

	BacktraceData data = {&result, 0, max_frames, true};

	_Unwind_Backtrace([](struct _Unwind_Context *context, void *arg) -> _Unwind_Reason_Code {
		BacktraceData *data = static_cast<BacktraceData *>(arg);

		// Skip first frame
		if (data->skip_first)
		{
			data->skip_first = false;
			return _URC_NO_REASON;
		}

		if (data->count >= data->max_frames)
		{
			return _URC_END_OF_STACK;
		}

		uintptr_t pc = _Unwind_GetIP(context);

		TraceItem item{};
		item.address = static_cast<uint64_t>(pc);

		Dl_info info;
		if (dladdr(reinterpret_cast<void *>(pc), &info))
		{
			if (info.dli_fname)
			{
				// Extract just the filename from the full path
				const char *last_slash = strrchr(info.dli_fname, '/');
				item.module            = msplat::container::string(last_slash ? last_slash + 1 : info.dli_fname);
			}
			else
			{
				item.module = msplat::container::string("unknown");
			}

			if (info.dli_sname)
			{
				item.symbol = demangle(info.dli_sname);
				item.offset = pc - reinterpret_cast<uintptr_t>(info.dli_saddr);
			}
			else
			{
				item.symbol = msplat::container::string("unknown");
				item.offset = 0;
			}
		}
		else
		{
			item.module = msplat::container::string("unknown");
			item.symbol = msplat::container::string("unknown");
			item.offset = 0;
		}

		data->result->emplace_back(std::move(item));
		data->count++;
		return _URC_NO_REASON;
	},
	                  &data);

#	endif

#endif        // NDEBUG

	return result;
}

}        // namespace msplat::core