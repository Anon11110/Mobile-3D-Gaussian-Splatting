#include <cstring>
#include <random>
#include <algorithm>

#include "test_framework.h"
#include <msplat/core/containers/memory.h>
#include <msplat/core/log.h>


TEST(linear_allocator_basic)
{
    msplat::core::LinearAllocator allocator(1024);
    
    // Test basic allocation
    void* ptr1 = allocator.allocate(100);
    if (!ptr1) return false;
    
    void* ptr2 = allocator.allocate(200);
    if (!ptr2) return false;
    
    // Check that pointers are different and properly aligned
    if (ptr1 == ptr2) return false;
    if (reinterpret_cast<uintptr_t>(ptr1) % alignof(std::max_align_t) != 0) return false;
    if (reinterpret_cast<uintptr_t>(ptr2) % alignof(std::max_align_t) != 0) return false;
    
    // Test usage tracking
    if (allocator.bytes_used() < 300) return false; // At least 300 bytes used
    if (allocator.bytes_remaining() > 724) return false; // At most 724 bytes remaining
    if (allocator.total_size() != 1024) return false;
    
    return true;
}

TEST(linear_allocator_alignment)
{
    msplat::core::LinearAllocator allocator(1024);
    
    // Test various alignments
    void* ptr1 = allocator.allocate(10, 1);
    void* ptr2 = allocator.allocate(10, 16);
    void* ptr3 = allocator.allocate(10, 64);
    
    if (!ptr1 || !ptr2 || !ptr3) return false;
    
    // Check alignments
    if (reinterpret_cast<uintptr_t>(ptr2) % 16 != 0) return false;
    if (reinterpret_cast<uintptr_t>(ptr3) % 64 != 0) return false;
    
    return true;
}

TEST(linear_allocator_out_of_memory)
{
    msplat::core::LinearAllocator allocator(100);
    
    // Allocate most of the space
    void* ptr1 = allocator.allocate(80);
    if (!ptr1) return false;
    
    // This should succeed (20 bytes left)
    void* ptr2 = allocator.allocate(20);
    if (!ptr2) return false;
    
    // This should fail (no space left)
    void* ptr3 = allocator.allocate(1);
    if (ptr3) return false; // Should be null
    
    return true;
}

TEST(linear_allocator_reset)
{
    msplat::core::LinearAllocator allocator(1024);
    
    // Allocate some memory
    void* ptr1 = allocator.allocate(500);
    if (!ptr1) return false;
    
    size_t used_before = allocator.bytes_used();
    if (used_before < 500) return false;
    
    // Reset allocator
    allocator.reset();
    
    // Check that memory is reclaimed
    if (allocator.bytes_used() != 0) return false;
    if (allocator.bytes_remaining() != 1024) return false;
    
    // Should be able to allocate again
    void* ptr2 = allocator.allocate(1000);
    if (!ptr2) return false;
    
    return true;
}

TEST(linear_allocator_external_buffer)
{
    char buffer[1024];
    msplat::core::LinearAllocator allocator(buffer, sizeof(buffer));
    
    void* ptr = allocator.allocate(100);
    if (!ptr) return false;
    
    // Pointer should be within our buffer
    if (ptr < buffer || ptr >= buffer + sizeof(buffer)) return false;
    
    return true;
}

TEST(stack_allocator_basic)
{
    msplat::core::StackAllocator allocator(1024);
    
    void* ptr1 = allocator.allocate(100);
    void* ptr2 = allocator.allocate(200);
    
    if (!ptr1 || !ptr2) return false;
    if (ptr1 == ptr2) return false;
    
    size_t used_before = allocator.bytes_used();
    
    // Deallocate in LIFO order
    allocator.deallocate(ptr2, 200);
    if (allocator.bytes_used() >= used_before) return false;
    
    allocator.deallocate(ptr1, 100);
    if (allocator.bytes_used() != 0) return false;
    
    return true;
}

TEST(stack_allocator_markers)
{
    msplat::core::StackAllocator allocator(1024);
    
    void* ptr1 = allocator.allocate(100);
    if (!ptr1) return false;
    
    auto marker = allocator.get_marker();
    
    void* ptr2 = allocator.allocate(200);
    void* ptr3 = allocator.allocate(300);
    if (!ptr2 || !ptr3) return false;
    
    size_t used_before = allocator.bytes_used();
    if (used_before < 600) return false;
    
    // Reset to marker - should deallocate ptr2 and ptr3
    allocator.reset_to_marker(marker);
    
    if (allocator.bytes_used() >= used_before) return false;
    
    // Should be able to allocate again from marker point
    void* ptr4 = allocator.allocate(500);
    if (!ptr4) return false;
    
    return true;
}

TEST(stack_allocator_full_reset)
{
    msplat::core::StackAllocator allocator(1024);
    
    allocator.allocate(100);
    allocator.allocate(200);
    allocator.allocate(300);
    
    if (allocator.bytes_used() < 600) return false;
    
    allocator.reset();
    
    if (allocator.bytes_used() != 0) return false;
    if (allocator.bytes_remaining() != 1024) return false;
    
    return true;
}

TEST(pool_allocator_basic)
{
    msplat::core::PoolAllocator<int> allocator(100); // 100 int-sized chunks
    
    if (allocator.chunk_count() != 100) return false;
    if (allocator.allocated_chunks() != 0) return false;
    if (allocator.free_chunks() != 100) return false;
    
    // Allocate some chunks
    void* ptr1 = allocator.allocate(sizeof(int));
    void* ptr2 = allocator.allocate(sizeof(int));
    
    if (!ptr1 || !ptr2) return false;
    if (ptr1 == ptr2) return false;
    
    if (allocator.allocated_chunks() != 2) return false;
    if (allocator.free_chunks() != 98) return false;
    
    return true;
}

TEST(pool_allocator_deallocation)
{
    msplat::core::PoolAllocator<int> allocator(10);
    
    std::vector<void*> ptrs;
    
    // Allocate all chunks
    for (int i = 0; i < 10; ++i) {
        void* ptr = allocator.allocate(sizeof(int));
        if (!ptr) return false;
        ptrs.push_back(ptr);
    }
    
    if (allocator.free_chunks() != 0) return false;
    
    // Next allocation should fail
    void* ptr = allocator.allocate(sizeof(int));
    if (ptr) return false;
    
    // Deallocate some chunks
    allocator.deallocate(ptrs[0], sizeof(int));
    allocator.deallocate(ptrs[5], sizeof(int));
    
    if (allocator.free_chunks() != 2) return false;
    if (allocator.allocated_chunks() != 8) return false;
    
    // Should be able to allocate again
    void* new_ptr = allocator.allocate(sizeof(int));
    if (!new_ptr) return false;
    
    return true;
}

TEST(pool_allocator_wrong_size)
{
    msplat::core::PoolAllocator<int> allocator(10);
    
    // Try to allocate larger than chunk size (chunk size is 8 bytes due to FreeNode, so request 16 bytes)
    void* ptr = allocator.allocate(sizeof(int) * 4);
    if (ptr) return false; // Should fail
    
    // Try to allocate with wrong alignment (64 bytes should be larger than max_align_t on most systems)
    void* ptr2 = allocator.allocate(sizeof(int), 64);
    if (ptr2) return false; // Should fail
    
    return true;
}

TEST(pool_allocator_external_buffer)
{
    const size_t buffer_size = 1024;
    char buffer[buffer_size];
    
    msplat::core::PoolAllocator<int> allocator(buffer, buffer_size);
    
    if (allocator.chunk_count() == 0) return false;
    
    void* ptr = allocator.allocate(sizeof(int));
    if (!ptr) return false;
    
    // Pointer should be within our buffer
    if (ptr < buffer || ptr >= buffer + buffer_size) return false;
    
    return true;
}


TEST(memory_write_read_verification)
{
    msplat::core::LinearAllocator allocator(1024);
    
    // Allocate and write a pattern
    size_t size = 256;
    char* ptr = static_cast<char*>(allocator.allocate(size));
    if (!ptr) return false;
    
    // Write a pattern
    for (size_t i = 0; i < size; ++i) {
        ptr[i] = static_cast<char>(i & 0xFF);
    }
    
    // Verify the pattern
    for (size_t i = 0; i < size; ++i) {
        if (ptr[i] != static_cast<char>(i & 0xFF)) return false;
    }
    
    return true;
}

// Registration function to be called from main
void register_allocator_tests()
{
    // Tests are automatically registered via static initialization
}