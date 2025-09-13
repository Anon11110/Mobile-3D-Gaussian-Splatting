#pragma once

// This header includes Windows.h once and only undefines the CreateSemaphore
// macro that conflicts with our RHI interface.

#ifdef _WIN32

#	ifndef WIN32_LEAN_AND_MEAN
#		define WIN32_LEAN_AND_MEAN
#	endif
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif

#	include <Windows.h>

// Only undef the specific macro that causes our conflict
#	ifdef CreateSemaphore
#		undef CreateSemaphore
#	endif
#	ifdef CreateSemaphoreA
#		undef CreateSemaphoreA
#	endif
#	ifdef CreateSemaphoreW
#		undef CreateSemaphoreW
#	endif

#endif        // _WIN32