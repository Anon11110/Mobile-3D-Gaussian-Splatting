/*
 * Memory Performance Test - Based on rpmalloc author's benchmark patterns
 * Tests allocation performance between system malloc and rpmalloc using
 * realistic scattered allocation/deallocation patterns that rpmalloc excels at.
 * This test is designed to run in Release mode for accurate performance measurements.
 */

#include <msplat/core/containers/memory.h>
#include <msplat/core/platform.h>
#include <msplat/core/timer.h>
#include <msplat/core/log.h>

#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <cstdlib>
#include <iostream>
#include <random>
#include <algorithm>


using namespace msplat::core;
using namespace msplat::timer;

namespace {
    // Test parameters based on rpmalloc author's benchmarks (scaled down for reasonable test time)
    constexpr size_t NUM_THREADS = 4;
    constexpr size_t LOOP_COUNT = 2000;   // Reduced from 20000
    constexpr size_t BLOCK_COUNT = 5000;  // Reduced from 50000  
    constexpr size_t OP_COUNT = 500;      // 10% of blocks freed/allocated each iteration
    constexpr size_t CROSS_THREAD_RATE = 2; // Cross-thread ops every 2 iterations
    constexpr size_t MIN_ALLOCATION_SIZE = 16;
    constexpr size_t MAX_ALLOCATION_SIZE = 1000;

    // System malloc/free wrapper for comparison using platform abstraction
    class SystemAllocator : public Allocator {
    public:
        void* allocate(size_t size, size_t alignment = alignof(std::max_align_t)) override {
            return msplat::core::aligned_malloc(size, alignment);
        }

        void deallocate(void* ptr, size_t size) override {
            msplat::core::aligned_free(ptr);
        }

        const char* name() const override { return "SystemAllocator"; }
    };

    // Block data for tracking allocations
    struct Block {
        void* ptr = nullptr;
        size_t size = 0;
        size_t thread_id = 0; // Track which thread allocated this
    };

    // Cross-thread allocation queue (thread-safe)
    class CrossThreadQueue {
    private:
        std::vector<Block> m_queue;
        std::mutex m_mutex;
        
    public:
        void push(const Block& block) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push_back(block);
        }
        
        bool pop(Block& block) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) return false;
            block = m_queue.back();
            m_queue.pop_back();
            return true;
        }
        
        size_t size() const {
            std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_mutex));
            return m_queue.size();
        }
    };

    // Random number generator for each thread (thread-local)
    thread_local std::mt19937 g_rng(static_cast<std::mt19937::result_type>(
        std::hash<std::thread::id>{}(std::this_thread::get_id())));
    
    size_t get_random_size() {
        std::uniform_int_distribution<size_t> dist(MIN_ALLOCATION_SIZE, MAX_ALLOCATION_SIZE);
        return dist(g_rng);
    }

    size_t get_random_index(size_t max_index) {
        std::uniform_int_distribution<size_t> dist(0, max_index - 1);
        return dist(g_rng);
    }

    // Perform scattered deallocation of random blocks
    void perform_scattered_deallocation(std::vector<Block>& blocks, Allocator& allocator) {
        for (size_t op = 0; op < OP_COUNT; ++op) {
            size_t index = get_random_index(BLOCK_COUNT);
            if (blocks[index].ptr != nullptr) {
                allocator.deallocate(blocks[index].ptr, blocks[index].size);
                blocks[index].ptr = nullptr;
                blocks[index].size = 0;
            }
        }
    }

    // Perform scattered allocation in random slots
    size_t perform_scattered_allocation(std::vector<Block>& blocks, Allocator& allocator, size_t thread_id) {
        size_t allocation_count = 0;
        for (size_t op = 0; op < OP_COUNT; ++op) {
            size_t index = get_random_index(BLOCK_COUNT);
            
            // If slot is occupied, free it first
            if (blocks[index].ptr != nullptr) {
                allocator.deallocate(blocks[index].ptr, blocks[index].size);
            }
            
            // Allocate new block
            size_t size = get_random_size();
            void* ptr = allocator.allocate(size);
            if (ptr != nullptr) {
                blocks[index].ptr = ptr;
                blocks[index].size = size;
                blocks[index].thread_id = thread_id;
                allocation_count++;
            }
        }
        return allocation_count;
    }

    // Handle cross-thread operations
    void handle_cross_thread_operations(std::vector<Block>& blocks, Allocator& allocator, 
                                       CrossThreadQueue& cross_thread_queue) {
        // Hand off OP_COUNT blocks to other threads for deallocation
        for (size_t op = 0; op < OP_COUNT; ++op) {
            size_t index = get_random_index(BLOCK_COUNT);
            if (blocks[index].ptr != nullptr) {
                Block block = blocks[index];
                cross_thread_queue.push(block);
                blocks[index].ptr = nullptr;
                blocks[index].size = 0;
            }
        }
        
        // Process cross-thread deallocations from other threads
        Block cross_block;
        for (size_t op = 0; op < OP_COUNT && cross_thread_queue.pop(cross_block); ++op) {
            if (cross_block.ptr != nullptr) {
                allocator.deallocate(cross_block.ptr, cross_block.size);
            }
        }
    }

    // Clean up remaining blocks
    void cleanup_remaining_blocks(const std::vector<Block>& blocks, Allocator& allocator) {
        for (const auto& block : blocks) {
            if (block.ptr != nullptr) {
                allocator.deallocate(block.ptr, block.size);
            }
        }
    }

    // rpmalloc-style benchmark worker - now simplified orchestrator
    void benchmark_worker(size_t thread_id, 
                         Allocator& allocator,
                         CrossThreadQueue& cross_thread_queue,
                         std::chrono::nanoseconds& duration,
                         size_t& total_allocations) {
        
        std::vector<Block> blocks(BLOCK_COUNT);
        size_t allocation_count = 0;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        for (size_t loop = 0; loop < LOOP_COUNT; ++loop) {
            // Perform scattered deallocation and allocation
            perform_scattered_deallocation(blocks, allocator);
            allocation_count += perform_scattered_allocation(blocks, allocator, thread_id);
            
            // Cross-thread operations every CROSS_THREAD_RATE iterations
            if ((loop + 1) % CROSS_THREAD_RATE == 0) {
                handle_cross_thread_operations(blocks, allocator, cross_thread_queue);
            }
        }
        
        // Clean up remaining blocks
        cleanup_remaining_blocks(blocks, allocator);
        
        auto end = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        total_allocations = allocation_count;
    }

    // Run the rpmalloc-style benchmark
    double test_rpmalloc_benchmark(Allocator& allocator) {
        std::vector<std::thread> threads;
        std::vector<std::chrono::nanoseconds> durations(NUM_THREADS);
        std::vector<size_t> allocation_counts(NUM_THREADS);
        CrossThreadQueue cross_thread_queue;
        
        auto start = std::chrono::high_resolution_clock::now();
        
        // Launch worker threads
        for (size_t i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back(benchmark_worker,
                               i,
                               std::ref(allocator),
                               std::ref(cross_thread_queue),
                               std::ref(durations[i]),
                               std::ref(allocation_counts[i]));
        }
        
        // Wait for completion
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        // Calculate statistics
        size_t total_allocations = 0;
        for (size_t count : allocation_counts) {
            total_allocations += count;
        }
        
        LOG_INFO("  Total allocations: {}", total_allocations);
        LOG_INFO("  Allocations per second: {:.0f}", 
                 (double)total_allocations / (total_duration.count() / 1000.0));
        
        return static_cast<double>(total_duration.count());
    }

    // Simple allocation test for baseline comparison
    double test_simple_allocation(Allocator& allocator, size_t iterations) {
        std::vector<void*> ptrs(iterations);
        std::vector<size_t> sizes(iterations);
        
        // Generate random sizes
        for (size_t i = 0; i < iterations; ++i) {
            sizes[i] = get_random_size();
        }
        
        Timer timer;
        timer.start();
        
        // Allocation phase
        for (size_t i = 0; i < iterations; ++i) {
            ptrs[i] = allocator.allocate(sizes[i]);
            if (!ptrs[i]) {
                LOG_ERROR("Allocation failed for size: {}", sizes[i]);
                return -1.0;
            }
        }
        
        // Deallocation phase
        for (size_t i = 0; i < iterations; ++i) {
            allocator.deallocate(ptrs[i], sizes[i]);
        }
        
        timer.stop();
        return timer.elapsedMilliseconds();
    }
}

int memory_performance_main() {
    LOG_INFO("Memory Performance Test - rpmalloc Benchmark Style");
    LOG_INFO("Parameters: {} threads, {} loops, {} blocks per thread", NUM_THREADS, LOOP_COUNT, BLOCK_COUNT);
    LOG_INFO("Operation pattern: {} ops per loop ({}% churn), cross-thread every {} loops", 
             OP_COUNT, (OP_COUNT * 100) / BLOCK_COUNT, CROSS_THREAD_RATE);
    LOG_INFO("Size range: {} - {} bytes", MIN_ALLOCATION_SIZE, MAX_ALLOCATION_SIZE);

    // Create allocators
    SystemAllocator system_allocator;
    HeapAllocator& heap_allocator = get_heap_allocator();

    LOG_INFO("\nRunning rpmalloc-style scattered allocation benchmark...");

    // Test system allocator
    LOG_INFO("Testing {} allocator:", system_allocator.name());
    double system_time = test_rpmalloc_benchmark(system_allocator);
    
    LOG_INFO("Testing {} allocator:", heap_allocator.name());
    double heap_time = test_rpmalloc_benchmark(heap_allocator);

    LOG_INFO("\nScattered allocation benchmark results:");
    LOG_INFO("  System malloc: {:.2f} ms", system_time);
    LOG_INFO("  rpmalloc:      {:.2f} ms", heap_time);

    if (heap_time > 0) {
        double speedup = system_time / heap_time;
        LOG_INFO("  Speedup:       {:.2f}x", speedup);
    }

    LOG_INFO("\nRunning simple allocation baseline (50K iterations)...");
    
    // Baseline simple allocation test
    constexpr size_t simple_iterations = 50000;
    double system_simple = test_simple_allocation(system_allocator, simple_iterations);
    double heap_simple = test_simple_allocation(heap_allocator, simple_iterations);
    
    LOG_INFO("Simple allocation baseline results:");
    LOG_INFO("  System malloc: {:.2f} ms", system_simple);
    LOG_INFO("  rpmalloc:      {:.2f} ms", heap_simple);
    
    if (heap_simple > 0) {
        double simple_speedup = system_simple / heap_simple;
        LOG_INFO("  Speedup:       {:.2f}x", simple_speedup);
    }

    // Basic container test with standard STL
    LOG_INFO("\nTesting standard STL containers...");

    Timer stl_timer;
    stl_timer.start();

    {
        // Test standard vector
        std::vector<int> standard_vector;
        for (int i = 0; i < 10000; ++i) {
            standard_vector.push_back(i);
        }

        // Test standard unordered_map
        std::unordered_map<int, std::string> standard_map;
        for (int i = 0; i < 1000; ++i) {
            standard_map[i] = "test_" + std::to_string(i);
        }
    }

    stl_timer.stop();
    LOG_INFO("Standard STL container test completed in {:.2f} ms", stl_timer.elapsedMilliseconds());

    LOG_INFO("\nMemory performance test completed successfully");
    return 0;
}