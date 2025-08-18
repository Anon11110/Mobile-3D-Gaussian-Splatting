#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <cassert>
#include <algorithm>
#include <typeinfo>

// Include logging for debug output
#include <msplat/core/log.h>

// Forward declarations from platform.h
namespace msplat::core
{
void  *aligned_malloc(size_t size, size_t alignment);
void   aligned_free(void *ptr);
}        // namespace msplat::core

namespace msplat::core
{

// Abstract base class for all allocators
class Allocator
{
  public:
	virtual ~Allocator() = default;

	// Allocate memory with specified size and alignment
	// Returns nullptr on failure
	virtual void *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) = 0;

	// Deallocate memory previously allocated by this allocator
	// ptr may be nullptr (safe to call)
	// size must match the size used in allocate()
	virtual void deallocate(void *ptr, size_t size) = 0;

	// Optional: get name for debugging
	virtual const char *name() const = 0;
};

// High-performance heap allocator using rpmalloc
class HeapAllocator : public Allocator
{
  public:
	HeapAllocator();
	~HeapAllocator();

	void       *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
	void        deallocate(void *ptr, size_t size) override;
	const char *name() const override
	{
		return "HeapAllocator";
	}

	// Non-copyable and non-movable
	HeapAllocator(const HeapAllocator &)            = delete;
	HeapAllocator &operator=(const HeapAllocator &) = delete;
	HeapAllocator(HeapAllocator &&)                 = delete;
	HeapAllocator &operator=(HeapAllocator &&)      = delete;
};

// Global heap allocator instance
extern HeapAllocator &get_heap_allocator();

// Linear allocator using bump pointer allocation
// - Very fast O(1) allocation via pointer increment
// - No individual deallocation (deallocate() is no-op)
// - Use reset() to reclaim all memory at once
// - Ideal for per-frame allocations or temporary objects
// - NOT thread-safe
class LinearAllocator : public Allocator
{
  private:
	void   *m_buffer;      // Start of buffer
	size_t  m_size;        // Total buffer size
	size_t  m_offset;      // Current allocation offset
	bool    m_owns_buffer; // Whether we own the buffer

  public:
	// Constructor with buffer size - allocates own buffer
	explicit LinearAllocator(size_t buffer_size);

	// Constructor with external buffer - does not own buffer
	LinearAllocator(void *buffer, size_t buffer_size);

	~LinearAllocator();

	void       *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
	void        deallocate(void *ptr, size_t size) override; // No-op
	const char *name() const override
	{
		return "LinearAllocator";
	}

	// Reset to beginning of buffer
	void reset();

	// Get current usage statistics
	size_t bytes_used() const
	{
		return m_offset;
	}
	size_t bytes_remaining() const
	{
		return m_size - m_offset;
	}
	size_t total_size() const
	{
		return m_size;
	}

	// Non-copyable and non-movable
	LinearAllocator(const LinearAllocator &)            = delete;
	LinearAllocator &operator=(const LinearAllocator &) = delete;
	LinearAllocator(LinearAllocator &&)                 = delete;
	LinearAllocator &operator=(LinearAllocator &&)      = delete;
};

// Stack allocator with LIFO deallocation and markers
// - Fast O(1) allocation via pointer increment like LinearAllocator
// - Supports LIFO deallocation to rewind to previous allocation points
// - Use markers to save/restore allocation points
// - Ideal for hierarchical temporary allocations
// - NOT thread-safe
class StackAllocator : public Allocator
{
  public:
	using Marker = size_t; // Type for allocation markers

  private:
	void   *m_buffer;      // Start of buffer
	size_t  m_size;        // Total buffer size
	size_t  m_offset;      // Current allocation offset
	bool    m_owns_buffer; // Whether we own the buffer

  public:
	// Constructor with buffer size - allocates own buffer
	explicit StackAllocator(size_t buffer_size);

	// Constructor with external buffer - does not own buffer
	StackAllocator(void *buffer, size_t buffer_size);

	~StackAllocator();

	void       *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
	void        deallocate(void *ptr, size_t size) override; // Validates LIFO order
	const char *name() const override
	{
		return "StackAllocator";
	}

	// Marker operations for bulk deallocation
	Marker get_marker() const
	{
		return m_offset;
	}
	void reset_to_marker(Marker marker);

	// Reset to beginning of buffer
	void reset()
	{
		reset_to_marker(0);
	}

	// Get current usage statistics
	size_t bytes_used() const
	{
		return m_offset;
	}
	size_t bytes_remaining() const
	{
		return m_size - m_offset;
	}
	size_t total_size() const
	{
		return m_size;
	}

	// Non-copyable and non-movable
	StackAllocator(const StackAllocator &)            = delete;
	StackAllocator &operator=(const StackAllocator &) = delete;
	StackAllocator(StackAllocator &&)                 = delete;
	StackAllocator &operator=(StackAllocator &&)      = delete;
};

// Pool allocator for fixed-size allocations
// - Very fast O(1) allocation and deallocation
// - Pre-allocates chunks of fixed size
// - Maintains free list for O(1) operations
// - Ideal for high-frequency same-size allocations
// - Thread-safe with internal synchronization
template <typename T>
class PoolAllocator : public Allocator
{
  private:
	struct FreeNode
	{
		FreeNode *next;
	};

	void     *m_buffer;       // Start of buffer
	size_t    m_chunk_size;   // Size of each chunk (>= sizeof(T))
	size_t    m_chunk_count;  // Total number of chunks
	FreeNode *m_free_head;    // Head of free list
	bool      m_owns_buffer;  // Whether we own the buffer
	size_t    m_allocated;    // Number of allocated chunks

  public:
	// Constructor with chunk count - allocates own buffer
	explicit PoolAllocator(size_t chunk_count);

	// Constructor with external buffer
	PoolAllocator(void *buffer, size_t buffer_size);

	~PoolAllocator();

	void       *allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override;
	void        deallocate(void *ptr, size_t size) override;
	const char *name() const override
	{
		return "PoolAllocator";
	}

	// Pool-specific operations
	size_t chunk_size() const
	{
		return m_chunk_size;
	}
	size_t chunk_count() const
	{
		return m_chunk_count;
	}
	size_t allocated_chunks() const
	{
		return m_allocated;
	}
	size_t free_chunks() const
	{
		return m_chunk_count - m_allocated;
	}

	// Non-copyable and non-movable
	PoolAllocator(const PoolAllocator &)            = delete;
	PoolAllocator &operator=(const PoolAllocator &) = delete;
	PoolAllocator(PoolAllocator &&)                 = delete;
	PoolAllocator &operator=(PoolAllocator &&)      = delete;

  private:
	void initialize_free_list()
	{
		// Initialize free list by linking all chunks
		char *buffer_ptr = static_cast<char *>(m_buffer);
		m_free_head      = nullptr;

		for (size_t i = 0; i < m_chunk_count; ++i)
		{
			FreeNode *node = reinterpret_cast<FreeNode *>(buffer_ptr + i * m_chunk_size);
			node->next     = m_free_head;
			m_free_head    = node;
		}
	}
};

// PoolAllocator template implementation
template <typename T>
PoolAllocator<T>::PoolAllocator(size_t chunk_count)
    :
    m_buffer(nullptr), m_chunk_size(std::max(sizeof(T), sizeof(FreeNode))),
    m_chunk_count(chunk_count), m_free_head(nullptr), m_owns_buffer(true), m_allocated(0)
{
	if (chunk_count == 0)
	{
		throw std::invalid_argument("PoolAllocator chunk count cannot be zero");
	}

	// Ensure chunk size is properly aligned for the type, but also compatible with general allocator interface
	// We want chunks to be at least sizeof(T) but properly aligned
	size_t type_alignment = alignof(T);
	m_chunk_size = ((m_chunk_size + type_alignment - 1) / type_alignment) * type_alignment;

	size_t buffer_size = m_chunk_size * m_chunk_count;
	m_buffer           = msplat::core::aligned_malloc(buffer_size, type_alignment);
	if (!m_buffer)
	{
		throw std::bad_alloc();
	}

	initialize_free_list();
}

template <typename T>
PoolAllocator<T>::PoolAllocator(void *buffer, size_t buffer_size)
    :
    m_buffer(buffer), m_chunk_size(std::max(sizeof(T), sizeof(FreeNode))),
    m_chunk_count(0), m_free_head(nullptr), m_owns_buffer(false), m_allocated(0)
{
	if (!buffer || buffer_size == 0)
	{
		throw std::invalid_argument("PoolAllocator buffer and size must be valid");
	}

	// Ensure chunk size is properly aligned for the type, but also compatible with general allocator interface
	// We want chunks to be at least sizeof(T) but properly aligned
	size_t type_alignment = alignof(T);
	m_chunk_size = ((m_chunk_size + type_alignment - 1) / type_alignment) * type_alignment;

	// Calculate how many chunks fit in the buffer
	m_chunk_count = buffer_size / m_chunk_size;
	if (m_chunk_count == 0)
	{
		throw std::invalid_argument("Buffer too small for even one chunk");
	}

	initialize_free_list();
}

template <typename T>
PoolAllocator<T>::~PoolAllocator()
{
	if (m_owns_buffer && m_buffer)
	{
		msplat::core::aligned_free(m_buffer);
	}
}

template <typename T>
void *PoolAllocator<T>::allocate(size_t size, size_t alignment)
{
	// Pool allocator only supports allocations up to the chunk size
	// and alignment up to what we allocated with (max_align_t)
	if (size > m_chunk_size)
	{
		return nullptr;
	}
	
	// Since our chunks are aligned to at least alignof(T), we can handle any alignment 
	// that's compatible with our chunk alignment
	if (alignment > m_chunk_size)
	{
		return nullptr;
	}

	if (!m_free_head)
	{
		// No free chunks available
		return nullptr;
	}

	// Remove from free list
	FreeNode *node = m_free_head;
	m_free_head    = m_free_head->next;
	++m_allocated;

	return node;
}

template <typename T>
void PoolAllocator<T>::deallocate(void *ptr, size_t size)
{
	if (!ptr)
	{
		return;
	}

	// Validate that the pointer is within our buffer range
	char *buffer_start = static_cast<char *>(m_buffer);
	char *buffer_end   = buffer_start + (m_chunk_count * m_chunk_size);
	char *ptr_char     = static_cast<char *>(ptr);

	if (ptr_char < buffer_start || ptr_char >= buffer_end)
	{
		// Pointer not from this allocator
		assert(false && "Pointer not from this PoolAllocator");
		return;
	}

	// Validate alignment
	size_t offset = ptr_char - buffer_start;
	if (offset % m_chunk_size != 0)
	{
		// Invalid alignment - not start of a chunk
		assert(false && "Invalid pointer alignment in PoolAllocator");
		return;
	}

	// Add back to free list
	FreeNode *node = static_cast<FreeNode *>(ptr);
	node->next     = m_free_head;
	m_free_head    = node;
	--m_allocated;
}


}        // namespace msplat::core