/*
 * Specialized Allocator Performance Test
 * Tests performance characteristics of LinearAllocator, StackAllocator, and PoolAllocator
 * compared to HeapAllocator for their intended use cases.
 * This test is designed to run in Release mode for accurate performance measurements.
 */

#include <msplat/core/containers/memory.h>
#include <msplat/core/timer.h>
#include <msplat/core/log.h>

#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <algorithm>
#include <iomanip>

using namespace msplat::core;
using namespace msplat::timer;

namespace {
    constexpr size_t LARGE_BUFFER_SIZE = 4 * 1024 * 1024; // 4MB - increased to fit NUM_ITERATIONS
    constexpr size_t SMALL_BUFFER_SIZE = 64 * 1024;   // 64KB
    constexpr size_t NUM_ITERATIONS = 10000;
    constexpr size_t NUM_SMALL_ALLOCS = 1000;
    
    // Helper to run a performance test
    template<typename Func>
    double measure_time_ms(const std::string& test_name, Func&& func) {
        std::cout << "Running " << test_name << "... " << std::flush;
        
        Timer timer;
        timer.start();
        func();
        timer.stop();
        
        double ms = timer.elapsedMilliseconds();
        std::cout << std::fixed << std::setprecision(3) << ms << " ms\n";
        return ms;
    }
    
    // Test linear allocator performance for sequential allocations
    void test_linear_allocator_sequential() {
        std::cout << "\n=== LinearAllocator vs HeapAllocator (Sequential Allocations) ===\n";
        
        // Test with LinearAllocator
        double linear_time = measure_time_ms("LinearAllocator sequential", []() {
            LinearAllocator allocator(LARGE_BUFFER_SIZE);
            std::vector<void*> ptrs;
            ptrs.reserve(NUM_ITERATIONS);
            
            for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
                size_t size = 16 + (i % 512); // Variable sizes 16-528 bytes
                void* ptr = allocator.allocate(size);
                if (ptr) {
                    ptrs.push_back(ptr);
                    // Write to memory to prevent optimization
                    std::memset(ptr, static_cast<int>(i & 0xFF), size);
                }
            }
            
            // Linear allocator doesn't need individual deallocations
            allocator.reset();
        });
        
        // Test with HeapAllocator
        double heap_time = measure_time_ms("HeapAllocator sequential", []() {
            HeapAllocator& allocator = get_heap_allocator();
            std::vector<std::pair<void*, size_t>> ptrs;
            ptrs.reserve(NUM_ITERATIONS);
            
            for (size_t i = 0; i < NUM_ITERATIONS; ++i) {
                size_t size = 16 + (i % 512); // Variable sizes 16-528 bytes
                void* ptr = allocator.allocate(size);
                if (ptr) {
                    ptrs.push_back({ptr, size});
                    // Write to memory to prevent optimization
                    std::memset(ptr, static_cast<int>(i & 0xFF), size);
                }
            }
            
            // Deallocate all
            for (auto [ptr, size] : ptrs) {
                allocator.deallocate(ptr, size);
            }
        });
        
        double speedup = heap_time / linear_time;
        std::cout << "LinearAllocator is " << std::fixed << std::setprecision(2) 
                  << speedup << "x faster than HeapAllocator\n";
    }
    
    // Test stack allocator performance for LIFO allocations
    void test_stack_allocator_lifo() {
        std::cout << "\n=== StackAllocator vs HeapAllocator (LIFO Allocations) ===\n";
        
        // Test with StackAllocator
        double stack_time = measure_time_ms("StackAllocator LIFO", []() {
            StackAllocator allocator(SMALL_BUFFER_SIZE);
            
            for (size_t iteration = 0; iteration < 100; ++iteration) {
                std::vector<std::pair<void*, size_t>> ptrs;
                ptrs.reserve(100);
                
                // Allocate in stack order
                for (size_t i = 0; i < 100; ++i) {
                    size_t size = 32 + (i % 128); // Variable sizes 32-160 bytes
                    void* ptr = allocator.allocate(size);
                    if (ptr) {
                        ptrs.push_back({ptr, size});
                        // Write to memory
                        std::memset(ptr, static_cast<int>(i & 0xFF), size);
                    }
                }
                
                // Deallocate in LIFO order
                for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
                    allocator.deallocate(it->first, it->second);
                }
            }
        });
        
        // Test with HeapAllocator
        double heap_time = measure_time_ms("HeapAllocator LIFO", []() {
            HeapAllocator& allocator = get_heap_allocator();
            
            for (size_t iteration = 0; iteration < 100; ++iteration) {
                std::vector<std::pair<void*, size_t>> ptrs;
                ptrs.reserve(100);
                
                // Allocate
                for (size_t i = 0; i < 100; ++i) {
                    size_t size = 32 + (i % 128); // Variable sizes 32-160 bytes
                    void* ptr = allocator.allocate(size);
                    if (ptr) {
                        ptrs.push_back({ptr, size});
                        // Write to memory
                        std::memset(ptr, static_cast<int>(i & 0xFF), size);
                    }
                }
                
                // Deallocate in LIFO order
                for (auto it = ptrs.rbegin(); it != ptrs.rend(); ++it) {
                    allocator.deallocate(it->first, it->second);
                }
            }
        });
        
        double speedup = heap_time / stack_time;
        std::cout << "StackAllocator is " << std::fixed << std::setprecision(2) 
                  << speedup << "x faster than HeapAllocator\n";
    }
    
    // Test pool allocator performance for fixed-size allocations
    void test_pool_allocator_fixed_size() {
        std::cout << "\n=== PoolAllocator vs HeapAllocator (Fixed-Size Allocations) ===\n";
        
        constexpr size_t CHUNK_SIZE = 64; // 64-byte chunks
        constexpr size_t NUM_CHUNKS = 1000;
        
        // Test with PoolAllocator
        double pool_time = measure_time_ms("PoolAllocator fixed-size", []() {
            PoolAllocator<char[CHUNK_SIZE]> allocator(NUM_CHUNKS);
            
            for (size_t iteration = 0; iteration < 100; ++iteration) {
                std::vector<void*> ptrs;
                ptrs.reserve(NUM_CHUNKS / 2);
                
                // Allocate chunks
                for (size_t i = 0; i < NUM_CHUNKS / 2; ++i) {
                    void* ptr = allocator.allocate(CHUNK_SIZE);
                    if (ptr) {
                        ptrs.push_back(ptr);
                        // Write to memory
                        std::memset(ptr, static_cast<int>(i & 0xFF), CHUNK_SIZE);
                    }
                }
                
                // Deallocate in random order to stress the free list
                std::random_device rd;
                std::mt19937 gen(rd());
                std::shuffle(ptrs.begin(), ptrs.end(), gen);
                
                for (void* ptr : ptrs) {
                    allocator.deallocate(ptr, CHUNK_SIZE);
                }
            }
        });
        
        // Test with HeapAllocator
        double heap_time = measure_time_ms("HeapAllocator fixed-size", []() {
            HeapAllocator& allocator = get_heap_allocator();
            
            for (size_t iteration = 0; iteration < 100; ++iteration) {
                std::vector<void*> ptrs;
                ptrs.reserve(NUM_CHUNKS / 2);
                
                // Allocate chunks
                for (size_t i = 0; i < NUM_CHUNKS / 2; ++i) {
                    void* ptr = allocator.allocate(CHUNK_SIZE);
                    if (ptr) {
                        ptrs.push_back(ptr);
                        // Write to memory
                        std::memset(ptr, static_cast<int>(i & 0xFF), CHUNK_SIZE);
                    }
                }
                
                // Deallocate in random order
                std::random_device rd;
                std::mt19937 gen(rd());
                std::shuffle(ptrs.begin(), ptrs.end(), gen);
                
                for (void* ptr : ptrs) {
                    allocator.deallocate(ptr, CHUNK_SIZE);
                }
            }
        });
        
        double speedup = heap_time / pool_time;
        std::cout << "PoolAllocator is " << std::fixed << std::setprecision(2) 
                  << speedup << "x faster than HeapAllocator\n";
    }
    
    // Test STL container performance baseline  
    void test_stl_baseline() {
        std::cout << "\n=== STL Container Performance Baseline ===\n";
        
        // Test standard vector performance
        double std_stl_time = measure_time_ms("std::vector<int> baseline", []() {
            for (size_t iteration = 0; iteration < 10; ++iteration) {
                std::vector<int> vec;
                vec.reserve(10000);
                
                for (int i = 0; i < 10000; ++i) {
                    vec.push_back(i * i);
                }
                
                // Do some work with the vector
                std::sort(vec.begin(), vec.end(), std::greater<int>());
            }
        });
        
        std::cout << "Results:\n";
        std::cout << "  std::vector baseline: " << std_stl_time << " ms\n";
        std::cout << "\nNote: This baseline demonstrates standard STL performance.\n";
        std::cout << "Direct allocator usage provides specialized allocation patterns\n";
        std::cout << "for specific use cases (linear, stack, pool allocation).\n";
    }
    
    // Test memory usage and fragmentation
    void test_memory_usage() {
        std::cout << "\n=== Memory Usage Analysis ===\n";
        
        // LinearAllocator usage
        {
            LinearAllocator allocator(SMALL_BUFFER_SIZE);
            
            // Allocate various sizes
            std::vector<void*> ptrs;
            size_t total_allocated = 0;
            
            for (size_t i = 0; i < 100; ++i) {
                size_t size = 16 + (i % 256);
                void* ptr = allocator.allocate(size);
                if (ptr) {
                    ptrs.push_back(ptr);
                    total_allocated += size;
                }
            }
            
            std::cout << "LinearAllocator:\n";
            std::cout << "  Buffer size: " << allocator.total_size() << " bytes\n";
            std::cout << "  Bytes used: " << allocator.bytes_used() << " bytes\n";
            std::cout << "  Bytes remaining: " << allocator.bytes_remaining() << " bytes\n";
            std::cout << "  Requested allocation: " << total_allocated << " bytes\n";
            std::cout << "  Overhead: " << (allocator.bytes_used() - total_allocated) << " bytes\n";
            std::cout << "  Efficiency: " << std::fixed << std::setprecision(1) 
                      << (100.0 * total_allocated / allocator.bytes_used()) << "%\n";
        }
        
        // PoolAllocator usage
        {
            PoolAllocator<char[64]> allocator(100);
            
            std::vector<void*> ptrs;
            for (size_t i = 0; i < 50; ++i) {
                void* ptr = allocator.allocate(64);
                if (ptr) {
                    ptrs.push_back(ptr);
                }
            }
            
            std::cout << "\nPoolAllocator<char[64]>:\n";
            std::cout << "  Chunk size: " << allocator.chunk_size() << " bytes\n";
            std::cout << "  Total chunks: " << allocator.chunk_count() << "\n";
            std::cout << "  Allocated chunks: " << allocator.allocated_chunks() << "\n";
            std::cout << "  Free chunks: " << allocator.free_chunks() << "\n";
            std::cout << "  Total memory: " << (allocator.chunk_size() * allocator.chunk_count()) << " bytes\n";
            std::cout << "  Used memory: " << (allocator.chunk_size() * allocator.allocated_chunks()) << " bytes\n";
        }
    }
}

int allocator_performance_main() {
    std::cout << "Specialized Allocator Performance Tests\n";
    std::cout << "======================================\n";
    
    try {
        test_linear_allocator_sequential();
        test_stack_allocator_lifo();
        test_pool_allocator_fixed_size();
        test_stl_baseline();
        test_memory_usage();
        
        std::cout << "\nAll performance tests completed successfully!\n";
        std::cout << "\nNOTE: For accurate performance measurements, run this in Release mode:\n";
        std::cout << "cmake --build build --config Release --target allocator-performance\n";
        std::cout << "cd build/bin/Release && ./allocator-performance\n";
    
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}