#pragma once

#include <functional>
#include <iomanip>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <string>
#include <vector>

namespace perf
{

// Performance comparison result
enum class Performance
{
	FASTER,
	SLOWER,
	SIMILAR
};

// Benchmark result structure
struct BenchmarkResult
{
	std::string name;
	double      custom_time;
	double      std_time;
	double      ratio;
	Performance performance;
};

// Performance comparison utilities
inline Performance classify_performance(double ratio)
{
	if (ratio > 1.05)
		return Performance::FASTER;
	else if (ratio < 0.95)
		return Performance::SLOWER;
	else
		return Performance::SIMILAR;
}

inline const char *performance_string(Performance perf)
{
	switch (perf)
	{
		case Performance::FASTER:
			return "FASTER";
		case Performance::SLOWER:
			return "SLOWER";
		case Performance::SIMILAR:
			return "SIMILAR";
		default:
			return "UNKNOWN";
	}
}

inline const char *performance_symbol(Performance perf)
{
	switch (perf)
	{
		case Performance::FASTER:
			return "✓";
		case Performance::SLOWER:
			return "✗";
		case Performance::SIMILAR:
			return "≈";
		default:
			return "?";
	}
}

// Compare performance and return classification
inline Performance compare_performance(double custom_time, double std_time)
{
	double ratio = std_time / custom_time;
	return classify_performance(ratio);
}

// Log a performance comparison with consistent formatting
inline void log_comparison(const std::string &operation, double custom_time, double std_time)
{
	double      ratio = std_time / custom_time;
	Performance perf  = classify_performance(ratio);

	// Format the operation name with padding for alignment
	std::string padded_op = operation;
	if (padded_op.length() < 12)
	{
		padded_op.append(12 - padded_op.length(), ' ');
	}

	LOG_INFO("    ├─ {}: Custom={:6.2f}ms | STL={:6.2f}ms | {:.2f}x {} {}",
	         padded_op, custom_time, std_time, ratio,
	         performance_string(perf), performance_symbol(perf));
}

// Log the final/total comparison
inline void log_final_comparison(const std::string &operation, double custom_time, double std_time)
{
	double      ratio = std_time / custom_time;
	Performance perf  = classify_performance(ratio);

	std::string padded_op = operation;
	if (padded_op.length() < 12)
	{
		padded_op.append(12 - padded_op.length(), ' ');
	}

	LOG_INFO("    └─ {}: Custom={:6.2f}ms | STL={:6.2f}ms | {:.2f}x {} {}",
	         padded_op, custom_time, std_time, ratio,
	         performance_string(perf), performance_symbol(perf));
}

// Formatted output helpers
inline void log_suite_header(const std::string &title)
{
	LOG_INFO("");
	LOG_INFO("┌─────────────────────────────────────────────────────────────┐");
	LOG_INFO("│ {:<59} │", title);
	LOG_INFO("└─────────────────────────────────────────────────────────────┘");
}

inline void log_section_header(const std::string &title)
{
	LOG_INFO("");
	LOG_INFO("  ▶ {}", title);
}

inline void log_benchmark_info(const std::string &info)
{
	LOG_INFO("    ├─ {}", info);
}

inline void log_benchmark_result(const std::string &result)
{
	LOG_INFO("    └─ {}", result);
}

inline void log_benchmark_header(const std::string &name, size_t elements,
                                 size_t lookups = 0, size_t deletions = 0)
{
	log_section_header(name);
	if (lookups > 0 && deletions > 0)
	{
		LOG_INFO("    ├─ Elements: {:,} | Lookups: {:,} | Deletions: {:,}",
		         elements, lookups, deletions);
	}
	else
	{
		LOG_INFO("    ├─ Elements: {:,}", elements);
	}
}

inline void log_test_summary(const std::string &test_name, bool passed)
{
	if (passed)
	{
		LOG_INFO("");
		LOG_INFO("  ✓ {} completed successfully", test_name);
	}
	else
	{
		LOG_ERROR("");
		LOG_ERROR("  ✗ {} failed", test_name);
	}
}

inline void log_main_header()
{
	LOG_INFO("══════════════════════════════════════════════════════════════");
	LOG_INFO("                    PERFORMANCE TEST SUITE                     ");
	LOG_INFO("══════════════════════════════════════════════════════════════");
}

inline void log_main_footer(int total_tests, int passed_tests, double total_time)
{
	LOG_INFO("");
	LOG_INFO("══════════════════════════════════════════════════════════════");
	LOG_INFO("Summary: {}/{} test suites passed | Total time: {:.0f}ms",
	         passed_tests, total_tests, total_time);
	LOG_INFO("══════════════════════════════════════════════════════════════");
}

// Simple benchmark registration system (optional, for future use)
struct Benchmark
{
	std::string           name;
	std::function<void()> func;
	bool                  requires_custom_impl;
	std::function<bool()> should_run;

	Benchmark(
	    const std::string &n, std::function<void()> f,
	    bool                  req_custom = false,
	    std::function<bool()> sr         = []() { return true; }) :
	    name(n),
	    func(f),
	    requires_custom_impl(req_custom),
	    should_run(sr)
	{}
};

// Global benchmark registry
inline std::vector<Benchmark> &get_benchmarks()
{
	static std::vector<Benchmark> benchmarks;
	return benchmarks;
}

inline void register_benchmark(const Benchmark &b)
{
	get_benchmarks().push_back(b);
}

inline void run_all_benchmarks()
{
	auto &benchmarks = get_benchmarks();

	for (const auto &benchmark : benchmarks)
	{
#ifdef MSPLAT_USE_SYSTEM_STL
		if (benchmark.requires_custom_impl)
		{
			LOG_INFO("  ⊘ {} skipped (requires custom implementation)", benchmark.name);
			continue;
		}
#endif
		if (!benchmark.should_run())
		{
			LOG_INFO("  ⊘ {} skipped", benchmark.name);
			continue;
		}

		log_section_header(benchmark.name);
		benchmark.func();
	}
}

// Macro for benchmark registration (optional, for future use)
#define BENCHMARK(name, requires_custom)                   \
	static void benchmark_##name();                        \
	static bool register_##name = []() {                   \
		perf::register_benchmark({#name, benchmark_##name, \
		                          requires_custom});       \
		return true;                                       \
	}();                                                   \
	static void benchmark_##name()

}        // namespace perf