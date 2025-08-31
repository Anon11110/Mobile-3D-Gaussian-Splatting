#define VMA_IMPLEMENTATION
#ifdef DEBUG
#	define VMA_DEBUG_LOG(format, ...)                 \
		do                                             \
		{                                              \
			printf("[VMA] " format "\n", __VA_ARGS__); \
		} while (0)
#endif

#include <vk_mem_alloc.h>