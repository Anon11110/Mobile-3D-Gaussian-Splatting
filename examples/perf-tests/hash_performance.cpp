#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/unordered_dense.h>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <random>
#include <string>
#include <unordered_map>
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

// Custom hash for Point when using std::unordered_map
struct PointHashStd
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

}        // namespace

// Specialization for std::hash
namespace std
{
template <>
struct hash<Point>
{
	std::size_t operator()(const Point &p) const
	{
		return PointHashStd{}(p);
	}
};
}        // namespace std

// Specialization for msplat::container::hash
namespace msplat::container
{
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
double benchmark_insertion(const std::vector<Key> &keys, const std::vector<Value> &values)
{
	Timer timer;
	Map   map;

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

// Main benchmark runner for a single map type
template <typename Map, typename Key, typename Value>
void run_single_benchmark(const std::string        &map_name,
                          const std::vector<Key>   &keys,
                          const std::vector<Value> &values,
                          const std::vector<Key>   &lookup_keys,
                          const std::vector<Key>   &delete_keys)
{
	LOG_INFO("  {} Results:", map_name);

	// Insertion benchmark
	double insert_time = benchmark_insertion<Map>(keys, values);
	LOG_INFO("    Insertion: {:.2f} ms ({:.2f} μs/element)",
	         insert_time, insert_time * 1000.0 / keys.size());

	// Create map for other benchmarks
	Map map;
	for (size_t i = 0; i < keys.size(); ++i)
	{
		map[keys[i]] = values[i];
	}

	// Lookup benchmark
	double lookup_time = benchmark_lookup(map, lookup_keys);
	LOG_INFO("    Lookup: {:.2f} ms ({:.2f} ns/lookup)",
	         lookup_time, lookup_time * 1000000.0 / lookup_keys.size());

	// Iteration benchmark
	double iter_time = benchmark_iteration(map);
	LOG_INFO("    Iteration: {:.2f} ms", iter_time);

	// Deletion benchmark
	double delete_time = benchmark_deletion(map, delete_keys);
	LOG_INFO("    Deletion: {:.2f} ms ({:.2f} μs/deletion)",
	         delete_time, delete_time * 1000.0 / delete_keys.size());

	// Total time
	double total_time = insert_time + lookup_time + iter_time + delete_time;
	LOG_INFO("    Total: {:.2f} ms", total_time);
}

// Compare two map implementations
template <typename Key, typename Value>
void compare_maps(const std::string        &test_name,
                  const std::vector<Key>   &keys,
                  const std::vector<Value> &values,
                  const std::vector<Key>   &lookup_keys,
                  const std::vector<Key>   &delete_keys)
{
	LOG_INFO("");
	LOG_INFO("=== {} Benchmark ===", test_name);
	LOG_INFO("Elements: {}, Lookups: {}, Deletions: {}",
	         keys.size(), lookup_keys.size(), delete_keys.size());

	// Test std::unordered_map with std::hash
	using StdMap = std::unordered_map<Key, Value>;
	run_single_benchmark<StdMap>("std::unordered_map",
	                             keys, values, lookup_keys, delete_keys);

	// Test ankerl::unordered_dense::map with RapidHash
	using AnkerlMap = ankerl::unordered_dense::map<Key, Value, msplat::container::hash<Key>>;
	run_single_benchmark<AnkerlMap>("ankerl::unordered_dense + RapidHash",
	                                keys, values, lookup_keys, delete_keys);

	// Also test ankerl with default hash for comparison
	using AnkerlDefaultMap = ankerl::unordered_dense::map<Key, Value>;
	run_single_benchmark<AnkerlDefaultMap>("ankerl::unordered_dense + default hash",
	                                       keys, values, lookup_keys, delete_keys);
}

}        // anonymous namespace

int hash_performance_main()
{
	LOG_INFO("Hash Performance Test Suite");
	LOG_INFO("============================");
	LOG_INFO("Comparing std::unordered_map vs ankerl::unordered_dense with different hash functions");

	// Integer keys benchmark
	{
		auto ints        = generate_random_ints(NUM_ELEMENTS);
		auto values      = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_ints = generate_random_ints(NUM_LOOKUPS);
		auto delete_ints = std::vector<int>(ints.begin(), ints.begin() + NUM_DELETIONS);

		compare_maps("Integer Keys", ints, values, lookup_ints, delete_ints);
	}

	// String keys benchmarks (different lengths)
	for (size_t str_len : STRING_LENGTHS)
	{
		auto strings        = generate_random_strings(NUM_ELEMENTS, str_len);
		auto values         = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_strings = generate_random_strings(NUM_LOOKUPS, str_len);
		auto delete_strings = std::vector<std::string>(strings.begin(),
		                                               strings.begin() + NUM_DELETIONS);

		compare_maps("String Keys (length " + std::to_string(str_len) + ")",
		             strings, values, lookup_strings, delete_strings);
	}

	// Custom struct keys benchmark
	{
		auto points        = generate_random_points(NUM_ELEMENTS);
		auto values        = generate_random_ints(NUM_ELEMENTS, 0, 100);
		auto lookup_points = generate_random_points(NUM_LOOKUPS);
		auto delete_points = std::vector<Point>(points.begin(),
		                                        points.begin() + NUM_DELETIONS);

		compare_maps("Custom Struct Keys (Point)", points, values, lookup_points, delete_points);
	}

	LOG_INFO("");
	LOG_INFO("Performance test completed successfully!");
	LOG_INFO("");
	LOG_INFO("Summary:");
	LOG_INFO("- std::unordered_map uses std::hash");
	LOG_INFO("- ankerl::unordered_dense tested with both RapidHash and its default hash");
	LOG_INFO("- Lower times are better for all metrics");

	return 0;
}