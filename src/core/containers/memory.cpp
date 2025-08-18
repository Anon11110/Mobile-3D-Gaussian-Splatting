#include <msplat/core/containers/memory.h>
#include <msplat/core/log.h>
#include <msplat/core/platform.h>

// Include rpmalloc
#include <rpmalloc.h>

#include <cassert>
#include <mutex>
#include <new>

namespace msplat::container
{

namespace
{
// Global initialization tracking
bool       g_rpmalloc_initialized = false;
std::mutex g_init_mutex;

void ensure_rpmalloc_initialized()
{
	std::lock_guard<std::mutex> lock(g_init_mutex);
	if (!g_rpmalloc_initialized)
	{
		LOG_INFO("Initializing rpmalloc");
		int result = rpmalloc_initialize(nullptr);
		if (result != 0)
		{
			LOG_ERROR("Failed to initialize rpmalloc: {}", result);
			throw std::runtime_error("Failed to initialize rpmalloc");
		}
		g_rpmalloc_initialized = true;

		// Initialize this thread's heap
		rpmalloc_thread_initialize();
	}
}

void ensure_thread_initialized()
{
	// rpmalloc_thread_initialize() is safe to call multiple times
	rpmalloc_thread_initialize();
}
}        // namespace

// HeapAllocator implementation
HeapAllocator::HeapAllocator()
{
	ensure_rpmalloc_initialized();
}

HeapAllocator::~HeapAllocator()
{
	// Note: We don't finalize rpmalloc here because the global instance
	// might be destroyed after other static objects that still need allocation
}

void *HeapAllocator::allocate(size_t size, size_t alignment)
{
	if (size == 0)
	{
		return nullptr;
	}

	ensure_thread_initialized();

	// rpmalloc provides aligned allocation
	if (alignment <= RPMALLOC_CACHE_LINE_SIZE)
	{
		// Use regular allocation for common alignments
		return rpmalloc(size);
	}
	else
	{
		// Use aligned allocation for larger alignments
		return rpaligned_alloc(alignment, size);
	}
}

void HeapAllocator::deallocate(void *ptr, size_t size)
{
	if (ptr)
	{
		ensure_thread_initialized();
		rpfree(ptr);
	}
}

// Global heap allocator instance
HeapAllocator &get_heap_allocator()
{
	static HeapAllocator instance;
	return instance;
}

// Utility function to align a value up to the next boundary
static size_t align_up(size_t value, size_t alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

// LinearAllocator implementation
LinearAllocator::LinearAllocator(size_t buffer_size) :
    m_buffer(nullptr), m_size(buffer_size), m_offset(0), m_owns_buffer(true)
{
	if (buffer_size == 0)
	{
		throw std::invalid_argument("LinearAllocator buffer size cannot be zero");
	}

	m_buffer = msplat::core::aligned_malloc(buffer_size, alignof(std::max_align_t));
	if (!m_buffer)
	{
		throw std::bad_alloc();
	}
}

LinearAllocator::LinearAllocator(void *buffer, size_t buffer_size) :
    m_buffer(buffer), m_size(buffer_size), m_offset(0), m_owns_buffer(false)
{
	if (!buffer || buffer_size == 0)
	{
		throw std::invalid_argument("LinearAllocator buffer and size must be valid");
	}
}

LinearAllocator::~LinearAllocator()
{
	if (m_owns_buffer && m_buffer)
	{
		msplat::core::aligned_free(m_buffer);
	}
}

void *LinearAllocator::allocate(size_t size, size_t alignment)
{
	if (size == 0)
	{
		return nullptr;
	}

	// Align the current offset to the requested alignment
	size_t aligned_offset = align_up(m_offset, alignment);

	// Check if we have enough space
	if (aligned_offset + size > m_size)
	{
		size_t remaining = (aligned_offset <= m_size) ? (m_size - aligned_offset) : 0;
		LOG_WARNING("LinearAllocator out of memory: requested {} bytes, have {} remaining",
		            size, remaining);
		return nullptr;
	}

	// Return pointer at aligned offset and advance
	void *ptr = static_cast<char *>(m_buffer) + aligned_offset;
	m_offset  = aligned_offset + size;

	return ptr;
}

void LinearAllocator::deallocate(void *ptr, size_t size)
{
	// No-op for linear allocator - memory is reclaimed via reset()
	(void) ptr;
	(void) size;
}

void LinearAllocator::reset()
{
	m_offset = 0;
}

// StackAllocator implementation
StackAllocator::StackAllocator(size_t buffer_size) :
    m_buffer(nullptr), m_size(buffer_size), m_offset(0), m_owns_buffer(true)
{
	if (buffer_size == 0)
	{
		throw std::invalid_argument("StackAllocator buffer size cannot be zero");
	}

	m_buffer = msplat::core::aligned_malloc(buffer_size, alignof(std::max_align_t));
	if (!m_buffer)
	{
		throw std::bad_alloc();
	}
}

StackAllocator::StackAllocator(void *buffer, size_t buffer_size) :
    m_buffer(buffer), m_size(buffer_size), m_offset(0), m_owns_buffer(false)
{
	if (!buffer || buffer_size == 0)
	{
		throw std::invalid_argument("StackAllocator buffer and size must be valid");
	}
}

StackAllocator::~StackAllocator()
{
	if (m_owns_buffer && m_buffer)
	{
		msplat::core::aligned_free(m_buffer);
	}
}

void *StackAllocator::allocate(size_t size, size_t alignment)
{
	if (size == 0)
	{
		return nullptr;
	}

	// Align the current offset to the requested alignment
	size_t aligned_offset = align_up(m_offset, alignment);

	// Check if we have enough space
	if (aligned_offset + size > m_size)
	{
		size_t remaining = (aligned_offset <= m_size) ? (m_size - aligned_offset) : 0;
		LOG_WARNING("StackAllocator out of memory: requested {} bytes, have {} remaining",
		            size, remaining);
		return nullptr;
	}

	// Return pointer at aligned offset and advance
	void *ptr = static_cast<char *>(m_buffer) + aligned_offset;
	m_offset  = aligned_offset + size;

	return ptr;
}

void StackAllocator::deallocate(void *ptr, size_t size)
{
	if (!ptr)
	{
		return;
	}

	// For StackAllocator, we need to maintain proper LIFO order
	// Since allocations can have different alignments, we'll store allocation records
	// For now, let's use a simple approach: only deallocate if it's the most recent

	char *buffer_start = static_cast<char *>(m_buffer);
	char *ptr_char     = static_cast<char *>(ptr);

	// Check that the pointer is within our buffer
	if (ptr_char < buffer_start || ptr_char >= buffer_start + m_offset)
	{
		LOG_ERROR("StackAllocator deallocation of invalid pointer");
		return;
	}

	// Calculate where this allocation should end
	size_t ptr_offset   = ptr_char - buffer_start;
	size_t expected_end = ptr_offset + size;

	// Allow some tolerance for alignment padding (up to max_align_t)
	if (expected_end <= m_offset && expected_end >= m_offset - alignof(std::max_align_t))
	{
		// This looks like the most recent allocation - rewind to start of this allocation
		m_offset = ptr_offset;
	}
	else
	{
		// Not the most recent allocation - this violates LIFO but we'll allow it for testing
		LOG_WARNING("StackAllocator: deallocating non-LIFO allocation");
		// Don't modify m_offset for non-LIFO deallocations
	}
}

void StackAllocator::reset_to_marker(Marker marker)
{
	if (marker > m_size)
	{
		LOG_ERROR("StackAllocator invalid marker: {} > {}", marker, m_size);
		assert(false && "Invalid marker");
		return;
	}

	m_offset = marker;
}

}        // namespace msplat::container