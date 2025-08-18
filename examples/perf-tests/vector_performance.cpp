#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <vector>

using namespace msplat::timer;

namespace
{

constexpr size_t TEST_SIZE  = 10000;
constexpr int    ITERATIONS = 5;

template <typename VectorType>
double benchmark_push_back_ints(size_t size, int iterations)
{
	Timer timer;
	timer.start();

	for (int i = 0; i < iterations; ++i)
	{
		VectorType vec;
		for (size_t j = 0; j < size; ++j)
		{
			vec.push_back(static_cast<int>(j));
		}
	}

	timer.stop();
	return timer.elapsedMilliseconds() / iterations;
}

void compare_performance(const char *test_name, double custom_time, double std_time)
{
	double      ratio       = std_time / custom_time;
	const char *performance = (ratio > 1.05) ? "FASTER" : (ratio < 0.95) ? "SLOWER" :
	                                                                       "SIMILAR";

	LOG_INFO("{}: Custom={:.3f}ms, Std={:.3f}ms, Ratio={:.2f}x [{}]",
	         test_name, custom_time, std_time, ratio, performance);
}

}        // anonymous namespace

int vector_performance_main()
{
	LOG_INFO("Vector Performance Benchmarks");
	LOG_INFO("==========================================");

	LOG_INFO("Configuration:");
	LOG_INFO("- Test size: {}", TEST_SIZE);
	LOG_INFO("- Iterations: {}", ITERATIONS);

#ifndef MSPLAT_USE_SYSTEM_STL
	LOG_INFO("\n--- Basic Push Back Benchmarks ---");
	{
		auto custom_time = benchmark_push_back_ints<msplat::container::vector<int>>(TEST_SIZE, ITERATIONS);
		auto std_time    = benchmark_push_back_ints<std::vector<int>>(TEST_SIZE, ITERATIONS);
		compare_performance("Push Back (int)", custom_time, std_time);
	}

	LOG_INFO("\n--- Allocator Integration Tests ---");
	{
		msplat::container::LinearAllocator linear_alloc(1024 * 1024);        // 1MB buffer

		Timer timer;
		timer.start();

		for (int i = 0; i < ITERATIONS; ++i)
		{
			linear_alloc.reset();
			msplat::container::vector<int> vec(&linear_alloc);

			for (size_t j = 0; j < TEST_SIZE / 10; ++j)
			{        // Smaller size for LinearAllocator
				vec.push_back(static_cast<int>(j));
			}
		}

		timer.stop();
		LOG_INFO("LinearAllocator Integration: {:.3f}ms per iteration", timer.elapsedMilliseconds() / ITERATIONS);
	}

#else
	LOG_INFO("Custom vector implementation disabled (MSPLAT_USE_SYSTEM_STL defined)");
	LOG_INFO("Skipping performance benchmarks");
#endif

	LOG_INFO("\nVector performance benchmarks completed.");
	return 0;
}