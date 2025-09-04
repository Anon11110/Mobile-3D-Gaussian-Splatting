#include "test_framework.h"
#include <msplat/core/containers/hash.h>
#include <msplat/core/containers/unordered_map.h>
#include <msplat/core/log.h>
#include <string>
#include <vector>

// Test type for custom struct hashing
struct TestPoint
{
	float x, y, z;

	bool operator==(const TestPoint &other) const
	{
		return x == other.x && y == other.y && z == other.z;
	}
};

// Custom hash specialization for TestPoint
namespace msplat::container
{
#ifndef MSPLAT_USE_SYSTEM_STL
template <>
struct hash<TestPoint>
{
	using is_avalanching = void;

	std::size_t operator()(const TestPoint &p) const noexcept
	{
		return hash_combine(p.x, p.y, p.z);
	}
};
#endif
}        // namespace msplat::container

// For std::hash when using system STL
#ifdef MSPLAT_USE_SYSTEM_STL
namespace std
{
template <>
struct hash<TestPoint>
{
	std::size_t operator()(const TestPoint &p) const
	{
		// Simple hash combining for std
		std::size_t h1 = std::hash<float>{}(p.x);
		std::size_t h2 = std::hash<float>{}(p.y);
		std::size_t h3 = std::hash<float>{}(p.z);
		return h1 ^ (h2 << 1) ^ (h3 << 2);
	}
};
}        // namespace std
#endif

// Basic integer hashing test
TEST(hash_basic_int)
{
	msplat::container::hash<int> hasher;

	// Test that hash produces consistent results
	int value1 = 42;
	int value2 = 42;
	int value3 = 43;

	auto hash1 = hasher(value1);
	auto hash2 = hasher(value2);
	auto hash3 = hasher(value3);

	// Same values should produce same hash
	if (hash1 != hash2)
		return false;

	// Different values should (usually) produce different hashes
	// Note: Hash collisions are possible but unlikely for simple consecutive integers
	if (hash1 == hash3)
	{
		LOG_WARNING("Hash collision detected for consecutive integers (rare but possible)");
	}

	return true;
}

// String hashing test
TEST(hash_basic_string)
{
	msplat::container::hash<std::string> hasher;

	std::string str1 = "hello world";
	std::string str2 = "hello world";
	std::string str3 = "goodbye world";

	auto hash1 = hasher(str1);
	auto hash2 = hasher(str2);
	auto hash3 = hasher(str3);

	// Same strings should produce same hash
	if (hash1 != hash2)
		return false;

	// Different strings should produce different hashes
	if (hash1 == hash3)
		return false;

	// Test empty string
	std::string empty;
	auto        empty_hash = hasher(empty);
	// Empty string should have a valid hash
	(void) empty_hash;        // Avoid unused variable warning

	return true;
}

// Pair hashing test
TEST(hash_pair)
{
	msplat::container::hash<std::pair<int, int>> hasher;

	auto pair1 = std::make_pair(1, 2);
	auto pair2 = std::make_pair(1, 2);
	auto pair3 = std::make_pair(2, 1);

	auto hash1 = hasher(pair1);
	auto hash2 = hasher(pair2);
	auto hash3 = hasher(pair3);

	// Same pairs should produce same hash
	if (hash1 != hash2)
		return false;

	// Different pairs should produce different hashes
	if (hash1 == hash3)
		return false;

	return true;
}

// Pointer hashing test
TEST(hash_pointer)
{
	msplat::container::hash<int *> hasher;

	int value1 = 42;
	int value2 = 43;

	auto hash1     = hasher(&value1);
	auto hash2     = hasher(&value1);
	auto hash3     = hasher(&value2);
	auto hash_null = hasher(nullptr);

	// Same pointer should produce same hash
	if (hash1 != hash2)
		return false;

	// Different pointers should produce different hashes
	if (hash1 == hash3)
		return false;

	// Null pointer should have a valid hash
	(void) hash_null;

	return true;
}

// Custom struct hashing test
TEST(hash_custom_struct)
{
	msplat::container::hash<TestPoint> hasher;

	TestPoint p1{1.0f, 2.0f, 3.0f};
	TestPoint p2{1.0f, 2.0f, 3.0f};
	TestPoint p3{3.0f, 2.0f, 1.0f};

	auto hash1 = hasher(p1);
	auto hash2 = hasher(p2);
	auto hash3 = hasher(p3);

	// Same points should produce same hash
	if (hash1 != hash2)
		return false;

	// Different points should produce different hashes
	if (hash1 == hash3)
		return false;

	return true;
}

// Hash consistency test - verify hashing the same value multiple times gives same result
TEST(hash_consistency)
{
	msplat::container::hash<int>         int_hasher;
	msplat::container::hash<std::string> string_hasher;

	// Test integer consistency
	int                      value = 12345;
	std::vector<std::size_t> int_hashes;
	for (int i = 0; i < 100; ++i)
	{
		int_hashes.push_back(int_hasher(value));
	}

	// All hashes should be the same
	for (size_t i = 1; i < int_hashes.size(); ++i)
	{
		if (int_hashes[i] != int_hashes[0])
			return false;
	}

	// Test string consistency
	std::string              str = "consistency test";
	std::vector<std::size_t> string_hashes;
	for (int i = 0; i < 100; ++i)
	{
		string_hashes.push_back(string_hasher(str));
	}

	// All hashes should be the same
	for (size_t i = 1; i < string_hashes.size(); ++i)
	{
		if (string_hashes[i] != string_hashes[0])
			return false;
	}

	return true;
}

// Basic unordered_map test
TEST(unordered_map_basic)
{
	msplat::container::unordered_map<int, std::string> map;

	// Test empty map
	if (!map.empty())
		return false;
	if (map.size() != 0)
		return false;

	// Test insertion
	map[1] = "one";
	map[2] = "two";
	map[3] = "three";

	if (map.empty())
		return false;
	if (map.size() != 3)
		return false;

	// Test lookup
	if (map[1] != "one")
		return false;
	if (map[2] != "two")
		return false;
	if (map[3] != "three")
		return false;

	// Test find
	auto it = map.find(2);
	if (it == map.end())
		return false;
	if (it->second != "two")
		return false;

	// Test non-existent key
	it = map.find(99);
	if (it != map.end())
		return false;

	return true;
}

// Unordered_map with string keys test
TEST(unordered_map_string_keys)
{
	msplat::container::unordered_map<std::string, int> map;

	map["hello"] = 1;
	map["world"] = 2;
	map["test"]  = 3;

	if (map.size() != 3)
		return false;

	if (map["hello"] != 1)
		return false;
	if (map["world"] != 2)
		return false;
	if (map["test"] != 3)
		return false;

	// Test overwrite
	map["hello"] = 10;
	if (map["hello"] != 10)
		return false;
	if (map.size() != 3)        // Size should remain the same
		return false;

	return true;
}

// Unordered_map operations test
TEST(unordered_map_operations)
{
	msplat::container::unordered_map<int, int> map;

	// Insert some elements
	for (int i = 0; i < 10; ++i)
	{
		map[i] = i * i;
	}

	if (map.size() != 10)
		return false;

	// Test iteration
	int count = 0;
	for (const auto &[key, value] : map)
	{
		if (value != key * key)
			return false;
		++count;
	}
	if (count != 10)
		return false;

	// Test erase
	map.erase(5);
	if (map.size() != 9)
		return false;
	if (map.find(5) != map.end())
		return false;

	// Test clear
	map.clear();
	if (!map.empty())
		return false;
	if (map.size() != 0)
		return false;

	return true;
}

// Unordered_map with custom struct test
TEST(unordered_map_custom_struct)
{
	msplat::container::unordered_map<TestPoint, int> map;

	TestPoint p1{1.0f, 2.0f, 3.0f};
	TestPoint p2{4.0f, 5.0f, 6.0f};
	TestPoint p3{7.0f, 8.0f, 9.0f};

	map[p1] = 100;
	map[p2] = 200;
	map[p3] = 300;

	if (map.size() != 3)
		return false;

	if (map[p1] != 100)
		return false;
	if (map[p2] != 200)
		return false;
	if (map[p3] != 300)
		return false;

	// Test with identical point
	TestPoint p1_copy{1.0f, 2.0f, 3.0f};
	if (map[p1_copy] != 100)
		return false;

	// Overwriting shouldn't change size
	map[p1_copy] = 150;
	if (map.size() != 3)
		return false;
	if (map[p1] != 150)
		return false;

	return true;
}

#ifndef MSPLAT_USE_SYSTEM_STL
// Test hash_combine (only available in custom implementation)
TEST(hash_combine_functionality)
{
	// Test combining multiple values
	auto hash1 = msplat::container::hash_combine(1, 2, 3);
	auto hash2 = msplat::container::hash_combine(1, 2, 3);
	auto hash3 = msplat::container::hash_combine(3, 2, 1);

	// Same inputs should produce same hash
	if (hash1 != hash2)
		return false;

	// Different order should produce different hash
	if (hash1 == hash3)
		return false;

	// Test with mixed types
	auto mixed_hash1 = msplat::container::hash_combine(1, "hello", 3.14f);
	auto mixed_hash2 = msplat::container::hash_combine(1, "hello", 3.14f);
	auto mixed_hash3 = msplat::container::hash_combine(1, "world", 3.14f);

	if (mixed_hash1 != mixed_hash2)
		return false;

	if (mixed_hash1 == mixed_hash3)
		return false;

	return true;
}

// Test hash_with_seed (only available in custom implementation)
TEST(hash_with_seed_functionality)
{
	// Test integer with seed
	int  value      = 42;
	auto hash_seed1 = msplat::container::hash_with_seed(value, 12345);
	auto hash_seed2 = msplat::container::hash_with_seed(value, 12345);
	auto hash_seed3 = msplat::container::hash_with_seed(value, 54321);

	// Same seed should produce same hash
	if (hash_seed1 != hash_seed2)
		return false;

	// Different seed should produce different hash
	if (hash_seed1 == hash_seed3)
		return false;

	// Test string with seed
	std::string str       = "test string";
	auto        str_hash1 = msplat::container::hash_with_seed(str, 999);
	auto        str_hash2 = msplat::container::hash_with_seed(str, 999);
	auto        str_hash3 = msplat::container::hash_with_seed(str, 111);

	if (str_hash1 != str_hash2)
		return false;

	if (str_hash1 == str_hash3)
		return false;

	return true;
}
#endif        // MSPLAT_USE_SYSTEM_STL

// Register all hash tests
void register_hash_tests()
{
	// Tests are auto-registered via the TEST macro
}