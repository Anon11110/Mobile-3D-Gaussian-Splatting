#include "perf_framework.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/containers/unordered_set.h>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <random>
#include <string>
#include <vector>

using namespace msplat::timer;

namespace
{

// Test data structures
struct Point
{
	float x, y, z;

	bool operator==(const Point &other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
};

}        // namespace

// Hash specialization for Point - need both for comparison
#ifndef MSPLAT_USE_SYSTEM_STL
namespace msplat::container
{
// Custom hash implementation using hash_combine
template <>
struct hash<Point>
{
	using is_avalanching = void;

	std::size_t operator()(const Point &p) const noexcept
	{
		return hash_combine(p.x, p.y, p.z);
	}
};
}        // namespace msplat::container
#endif

// Specialization for std::hash - always needed for comparison
namespace std
{
template <>
struct hash<Point>
{
	std::size_t operator()(const Point &p) const
	{
		// Simple hash combining for std
		std::size_t h1 = std::hash<float>{}(p.x);
		std::size_t h2 = std::hash<float>{}(p.y);
		std::size_t h3 = std::hash<float>{}(p.z);
		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};
}        // namespace std

namespace
{

// Test parameters
constexpr size_t NUM_ELEMENTS       = 100000;
constexpr size_t NUM_LOOKUPS        = 500000;
constexpr size_t NUM_DELETIONS      = 10000;
constexpr size_t NUM_STRING_LENGTHS = 3;
const size_t     STRING_LENGTHS[]   = {8, 32, 128};

// Generate random data
std::vector<int> generate_random_ints(size_t count, int min_val = 0, int max_val = 1000000)
{
	std::random_device              rd;
	std::mt19937                    gen(rd());
	std::uniform_int_distribution<> dis(min_val, max_val);

	std::vector<int> result;
	result.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		result.push_back(dis(gen));
	}
	return result;
}

std::vector<std::string> generate_random_strings(size_t count, size_t length)
{
	std::random_device              rd;
	std::mt19937                    gen(rd());
	std::uniform_int_distribution<> dis('a', 'z');

	std::vector<std::string> result;
	result.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		std::string str;
		str.reserve(length);
		for (size_t j = 0; j < length; ++j)
		{
			str += static_cast<char>(dis(gen));
		}
		result.push_back(std::move(str));
	}
	return result;
}

std::vector<Point> generate_random_points(size_t count)
{
	std::random_device                    rd;
	std::mt19937                          gen(rd());
	std::uniform_real_distribution<float> dis(-1000.0f, 1000.0f);

	std::vector<Point> result;
	result.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		result.push_back({dis(gen), dis(gen), dis(gen)});
	}
	return result;
}

// Benchmark functions
template <typename Map, typename Key, typename Value>
double benchmark_insertion(const std::vector<Key> &keys, const std::vector<Value> &values, Map map = Map{})
{
	Timer timer;

	timer.start();
	for (size_t i = 0; i < keys.size(); ++i)
	{
		map[keys[i]] = values[i];
	}
	timer.stop();

	return timer.elapsedMilliseconds();
}

template <typename Map, typename Key>
double benchmark_lookup(Map &map, const std::vector<Key> &lookup_keys)
{
	Timer  timer;
	size_t found = 0;

	timer.start();
	for (const auto &key : lookup_keys)
	{
		if (map.find(key) != map.end())
		{
			++found;
		}
	}
	timer.stop();

	return timer.elapsedMilliseconds();
}

template <typename Map>
double benchmark_iteration(Map &map)
{
	Timer  timer;
	size_t sum = 0;

	timer.start();
	for (const auto &[key, value] : map)
	{
		sum += value;
	}
	timer.stop();

	// Prevent optimization
	if (sum == 0)
	{
		LOG_DEBUG("Sum: {}", sum);
	}

	return timer.elapsedMilliseconds();
}

template <typename Map, typename Key>
double benchmark_deletion(Map &map, const std::vector<Key> &delete_keys)
{
	Timer timer;

	timer.start();
	for (const auto &key : delete_keys)
	{
		map.erase(key);
	}
	timer.stop();

	return timer.elapsedMilliseconds();
}

// Benchmark runner for comparison
#ifndef MSPLAT_USE_SYSTEM_STL
template <typename Key, typename Value>
void run_comparison_benchmark(const std::string        &test_name,
                              const std::vector<Key>   &keys,
                              const std::vector<Value> &values,
                              const std::vector<Key>   &lookup_keys,
                              const std::vector<Key>   &delete_keys)
{
	perf::log_benchmark_header(test_name + " (" + std::to_string(keys.size()) + " elements)",
	                           keys.size(), lookup_keys.size(), delete_keys.size());

	using CustomMap = msplat::container::unordered_map<Key, Value>;
	using StdMap    = std::unordered_map<Key, Value>;

	// Insertion benchmarks
	// For custom map, use factory function to ensure proper allocator initialization
	double custom_insert_time = benchmark_insertion<CustomMap>(keys, values,
	                                                           msplat::container::make_unordered_map_default<Key, Value>());
	double std_insert_time    = benchmark_insertion<StdMap>(keys, values, StdMap{});
	perf::log_comparison("Insertion", custom_insert_time, std_insert_time);

	// Create maps for other benchmarks
	// Use factory function for custom map to ensure proper allocator initialization
	CustomMap custom_map = msplat::container::make_unordered_map_default<Key, Value>();
	StdMap    std_map;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		custom_map[keys[i]] = values[i];
		std_map[keys[i]]    = values[i];
	}

	// Lookup benchmarks
	double custom_lookup_time = benchmark_lookup(custom_map, lookup_keys);
	double std_lookup_time    = benchmark_lookup(std_map, lookup_keys);
	perf::log_comparison("Lookup", custom_lookup_time, std_lookup_time);

	// Iteration benchmarks
	double custom_iter_time = benchmark_iteration(custom_map);
	double std_iter_time    = benchmark_iteration(std_map);
	perf::log_comparison("Iteration", custom_iter_time, std_iter_time);

	// Deletion benchmarks
	double custom_delete_time = benchmark_deletion(custom_map, delete_keys);
	double std_delete_time    = benchmark_deletion(std_map, delete_keys);
	perf::log_comparison("Deletion", custom_delete_time, std_delete_time);

	// Total time comparison
	double custom_total = custom_insert_time + custom_lookup_time + custom_iter_time + custom_delete_time;
	double std_total    = std_insert_time + std_lookup_time + std_iter_time + std_delete_time;
	perf::log_final_comparison("Overall", custom_total, std_total);
}

// Benchmark runner for set comparison
template <typename T>
void run_set_comparison_benchmark(const std::string    &test_name,
                                  const std::vector<T> &values,
                                  const std::vector<T> &lookup_values,
                                  const std::vector<T> &delete_values)
{
	perf::log_benchmark_header(test_name + " Set (" + std::to_string(values.size()) + " elements)",
	                           values.size(), lookup_values.size(), delete_values.size());

	using CustomSet = msplat::container::unordered_set<T>;
	using StdSet    = std::unordered_set<T>;

	// Insertion benchmarks
	Timer timer;

	// Custom set insertion
	CustomSet custom_set = msplat::container::make_unordered_set_default<T>();
	timer.start();
	for (const auto &value : values)
	{
		custom_set.insert(value);
	}
	timer.stop();
	double custom_insert_time = timer.elapsedMilliseconds();

	// Std set insertion
	StdSet std_set;
	timer.start();
	for (const auto &value : values)
	{
		std_set.insert(value);
	}
	timer.stop();
	double std_insert_time = timer.elapsedMilliseconds();
	perf::log_comparison("Insertion", custom_insert_time, std_insert_time);

	// Lookup benchmarks
	double custom_lookup_time = benchmark_lookup(custom_set, lookup_values);
	double std_lookup_time    = benchmark_lookup(std_set, lookup_values);
	perf::log_comparison("Lookup", custom_lookup_time, std_lookup_time);

	// Iteration benchmarks
	Timer  iter_timer;
	size_t sum = 0;

	iter_timer.start();
	for (const auto &value : custom_set)
	{
		sum += std::hash<T>{}(value);
	}
	iter_timer.stop();
	double custom_iter_time = iter_timer.elapsedMilliseconds();

	sum = 0;
	iter_timer.start();
	for (const auto &value : std_set)
	{
		sum += std::hash<T>{}(value);
	}
	iter_timer.stop();
	double std_iter_time = iter_timer.elapsedMilliseconds();

	// Prevent optimization
	if (sum == 0)
	{
		LOG_DEBUG("Sum: {}", sum);
	}

	perf::log_comparison("Iteration", custom_iter_time, std_iter_time);

	// Deletion benchmarks
	double custom_delete_time = benchmark_deletion(custom_set, delete_values);
	double std_delete_time    = benchmark_deletion(std_set, delete_values);
	perf::log_comparison("Deletion", custom_delete_time, std_delete_time);

	// Total time comparison
	double custom_total = custom_insert_time + custom_lookup_time + custom_iter_time + custom_delete_time;
	double std_total    = std_insert_time + std_lookup_time + std_iter_time + std_delete_time;
	perf::log_final_comparison("Overall", custom_total, std_total);
}
#endif

}        // anonymous namespace

int hash_performance_main()
{
	perf::log_suite_header("Unordered Map/Set Performance Tests");

#ifdef MSPLAT_USE_SYSTEM_STL
	LOG_INFO("  ⊘ Custom unordered_map implementation disabled (MSPLAT_USE_SYSTEM_STL defined)");
	LOG_INFO("  ⊘ Skipping performance comparison benchmarks");
#else
	LOG_INFO("  Comparing: Custom (ankerl::unordered_dense + RapidHash) vs STL");

	// Integer keys benchmark
	{
		auto ints        = generate_random_ints(NUM_ELEMENTS);
		auto values      = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_ints = generate_random_ints(NUM_LOOKUPS);
		auto delete_ints = std::vector<int>(ints.begin(), ints.begin() + NUM_DELETIONS);

		run_comparison_benchmark("Integer Keys", ints, values, lookup_ints, delete_ints);
	}

	// String keys benchmarks (different lengths)
	for (size_t str_len : STRING_LENGTHS)
	{
		auto strings        = generate_random_strings(NUM_ELEMENTS, str_len);
		auto values         = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_strings = generate_random_strings(NUM_LOOKUPS, str_len);
		auto delete_strings = std::vector<std::string>(strings.begin(),
		                                               strings.begin() + NUM_DELETIONS);

		run_comparison_benchmark("String Keys (len=" + std::to_string(str_len) + ")",
		                         strings, values, lookup_strings, delete_strings);
	}

	// Custom struct keys benchmark
	{
		auto points        = generate_random_points(NUM_ELEMENTS);
		auto values        = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_points = generate_random_points(NUM_LOOKUPS);
		auto delete_points = std::vector<Point>(points.begin(),
		                                        points.begin() + NUM_DELETIONS);

		run_comparison_benchmark("Custom Struct (Point)", points, values, lookup_points, delete_points);
	}

	// Set benchmarks
	LOG_INFO("\n  Set Benchmarks:");
	LOG_INFO("  Comparing: Custom (ankerl::unordered_dense + RapidHash) vs STL");

	// Integer set benchmark
	{
		auto ints        = generate_random_ints(NUM_ELEMENTS);
		auto lookup_ints = generate_random_ints(NUM_LOOKUPS);
		auto delete_ints = std::vector<int>(ints.begin(), ints.begin() + NUM_DELETIONS);

		run_set_comparison_benchmark("Integer", ints, lookup_ints, delete_ints);
	}

	// String set benchmarks (different lengths)
	for (size_t str_len : STRING_LENGTHS)
	{
		auto strings        = generate_random_strings(NUM_ELEMENTS, str_len);
		auto lookup_strings = generate_random_strings(NUM_LOOKUPS, str_len);
		auto delete_strings = std::vector<std::string>(strings.begin(),
		                                               strings.begin() + NUM_DELETIONS);

		run_set_comparison_benchmark("String (len=" + std::to_string(str_len) + ")",
		                             strings, lookup_strings, delete_strings);
	}

	// Custom struct set benchmark
	{
		auto points        = generate_random_points(NUM_ELEMENTS);
		auto lookup_points = generate_random_points(NUM_LOOKUPS);
		auto delete_points = std::vector<Point>(points.begin(),
		                                        points.begin() + NUM_DELETIONS);

		run_set_comparison_benchmark("Custom Struct (Point)", points, lookup_points, delete_points);
	}

	perf::log_test_summary("Hash Performance", true);
#endif

	return 0;
}