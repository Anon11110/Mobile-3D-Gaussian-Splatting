#include "perf_framework.h"
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

}        // anonymous namespace

int vector_performance_main()
{
	perf::log_suite_header("Vector Performance Tests");

#ifdef MSPLAT_USE_STD_CONTAINERS
	LOG_INFO("  ⊘ Custom vector implementation disabled (MSPLAT_USE_STD_CONTAINERS defined)");
	LOG_INFO("  ⊘ Skipping performance comparison benchmarks");
#else
	// Push Back Comparison
	perf::log_section_header("Push Back Comparison");
	LOG_INFO("    ├─ Test size: {:,} | Iterations: {}", TEST_SIZE, ITERATIONS);
	{
		auto custom_time = benchmark_push_back_ints<msplat::container::vector<int>>(TEST_SIZE, ITERATIONS);
		auto std_time    = benchmark_push_back_ints<std::vector<int>>(TEST_SIZE, ITERATIONS);

		LOG_INFO("    ├─ Custom:   {:.3f} ms (avg)", custom_time);
		LOG_INFO("    ├─ STL:      {:.3f} ms (avg)", std_time);

		double ratio = std_time / custom_time;
		auto   perf  = perf::classify_performance(ratio);
		LOG_INFO("    └─ Result:   {:.2f}x {} {}",
		         ratio, perf::performance_string(perf), perf::performance_symbol(perf));
	}

#endif

	perf::log_test_summary("Vector Performance", true);
	return 0;
}