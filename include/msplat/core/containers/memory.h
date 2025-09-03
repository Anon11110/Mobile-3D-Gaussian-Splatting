#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <memory_resource>
#include <mimalloc.h>

namespace msplat::container::pmr
{

/**
 * The root memory source, wrapping mimalloc.
 * This is the application's interface to the OS heap and serves as the
 * ultimate fallback for all other allocators.
 */
class UpstreamAllocator : public std::pmr::memory_resource
{
  protected:
	void *do_allocate(size_t bytes, size_t alignment) override
	{
		return mi_malloc_aligned(bytes, alignment);
	}

	void do_deallocate(void *p, size_t bytes, size_t alignment) override
	{
		mi_free(p);
	}

	bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
	{
		// All UpstreamAllocators are considered equal as it's a singleton.
		return dynamic_cast<const UpstreamAllocator *>(&other) != nullptr;
	}
};

/**
 * Singleton accessor for the single upstream allocator instance.
 */
inline std::pmr::memory_resource *GetUpstreamAllocator()
{
	static UpstreamAllocator instance;
	return &instance;
}

/**
 * A multi-buffered, monotonic (bump) allocator for all per-frame transient data.
 * This is the primary tool for high-performance, short-lived allocations.
 *
 * The number of arenas should be ≥ max GPU in-flight frames that can touch the arena.
 *
 * THREAD SAFETY: This class is NOT thread-safe. BeginFrame() and Resource()
 * must be called from the same thread (typically the main render thread).
 * Multiple threads can safely allocate from the same Resource() concurrently,
 * but BeginFrame() must not be called concurrently with any other operations.
 */
template <size_t NumArenas = 3>
class FrameArenaResource
{
  private:
	static_assert(NumArenas >= 2, "Must have at least 2 arenas for double-buffering");

	using Mon = std::pmr::monotonic_buffer_resource;

	// Backing buffers
	std::array<std::unique_ptr<std::byte[]>, NumArenas> mBuffers;

	// Raw storage for monotonic_buffer_resource objects (not constructed yet)
	std::array<std::aligned_storage_t<sizeof(Mon), alignof(Mon)>, NumArenas> mArenaStorage;

	static Mon *ArenaAt(void *slot)
	{
		return std::launder(reinterpret_cast<Mon *>(slot));
	}

	uint32_t     mFrameIndex = 0;
	const size_t mArenaSize;

  public:
	explicit FrameArenaResource(size_t arenaBytes = 64 * 1024 * 1024) :
	    mArenaSize(arenaBytes)
	{
		for (size_t i = 0; i < NumArenas; ++i)
		{
			mBuffers[i] = std::make_unique<std::byte[]>(mArenaSize);
			new (&mArenaStorage[i]) Mon(mBuffers[i].get(), mArenaSize, GetUpstreamAllocator());
		}
	}

	~FrameArenaResource()
	{
		for (size_t i = 0; i < NumArenas; ++i)
		{
			ArenaAt(&mArenaStorage[i])->~Mon();
		}
	}

	/// Call this once at the beginning of each new frame.
	/// MUST be called from the same thread as Resource().
	/// NOT thread-safe with concurrent calls to Resource().
	void BeginFrame()
	{
		mFrameIndex = (mFrameIndex + 1) % NumArenas;
		ArenaAt(&mArenaStorage[mFrameIndex])->release();
	}

	/// Get the memory resource for the current frame's allocations.
	/// The returned resource is safe for concurrent allocations from multiple threads.
	/// However, this method itself is NOT thread-safe with BeginFrame().
	std::pmr::memory_resource *Resource()
	{
		return ArenaAt(&mArenaStorage[mFrameIndex]);
	}

	/// Get the number of arenas configured for this instance
	constexpr size_t GetNumArenas() const noexcept
	{
		return NumArenas;
	}

	/// Get the size of each arena in bytes
	constexpr size_t GetArenaSize() const noexcept
	{
		return mArenaSize;
	}
};

// Common type aliases for typical use cases
using FrameArena       = FrameArenaResource<3>;        // Triple-buffered (default 64MB)
using FrameArenaDouble = FrameArenaResource<2>;        // Double-buffered (default 64MB)
using FrameArenaQuad   = FrameArenaResource<4>;        // Quad-buffered (default 64MB)

}        // namespace msplat::container::pmr