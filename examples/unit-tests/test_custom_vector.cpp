#include "test_framework.h"
#include <msplat/core/containers/memory.h>
#include <msplat/core/containers/vector.h>
#include <msplat/core/log.h>
#include <msplat/core/memory/frame_arena.h>

// Test types
struct NonTrivialType
{
	int        value;
	static int construct_count;
	static int destruct_count;

	NonTrivialType() :
	    value(0)
	{
		++construct_count;
	}
	NonTrivialType(int v) :
	    value(v)
	{
		++construct_count;
	}
	NonTrivialType(const NonTrivialType &other) :
	    value(other.value)
	{
		++construct_count;
	}
	NonTrivialType(NonTrivialType &&other) noexcept :
	    value(other.value)
	{
		other.value = 0;
		++construct_count;
	}
	~NonTrivialType()
	{
		++destruct_count;
	}

	NonTrivialType &operator=(const NonTrivialType &other)
	{
		value = other.value;
		return *this;
	}
	NonTrivialType &operator=(NonTrivialType &&other) noexcept
	{
		value       = other.value;
		other.value = 0;
		return *this;
	}

	static void reset_counters()
	{
		construct_count = 0;
		destruct_count  = 0;
	}

	bool operator==(const NonTrivialType &other) const
	{
		return value == other.value;
	}
};

int NonTrivialType::construct_count = 0;
int NonTrivialType::destruct_count  = 0;

#ifndef MSPLAT_USE_STD_CONTAINERS
// Only test custom vector implementation

TEST(vector_default_construction)
{
	msplat::container::vector<int> vec;
	return vec.size() == 0 && vec.capacity() == 0 && vec.empty() && vec.data() == nullptr;
}

TEST(vector_allocator_construction)
{
	auto                          *upstream = msplat::container::pmr::GetUpstreamAllocator();
	msplat::container::vector<int> vec(upstream);
	return vec.size() == 0 && vec.capacity() == 0 && vec.empty();
}

TEST(vector_push_back_growth)
{
	msplat::container::vector<int> vec;

	// First push_back should allocate
	vec.push_back(42);
	if (vec.size() != 1 || vec.capacity() < 1 || vec[0] != 42)
	{
		return false;
	}

	// Add more elements to test growth
	for (int i = 1; i < 20; ++i)
	{
		vec.push_back(i);
	}

	if (vec.size() != 20)
	{
		return false;
	}

	// Check all values
	for (int i = 0; i < 20; ++i)
	{
		if (vec[i] != (i == 0 ? 42 : i))
		{
			return false;
		}
	}

	return true;
}

TEST(vector_push_back_move)
{
	NonTrivialType::reset_counters();

	msplat::container::vector<NonTrivialType> vec;

	NonTrivialType obj(42);
	int            constructs_before = NonTrivialType::construct_count;

	vec.push_back(std::move(obj));

	// Should have one move construction for the vector element
	return vec.size() == 1 &&
	       vec[0].value == 42 &&
	       obj.value == 0 &&        // obj was moved from
	       NonTrivialType::construct_count == constructs_before + 1;
}

TEST(vector_reserve)
{
	msplat::container::vector<int> vec;

	vec.reserve(100);

	if (vec.capacity() < 100 || vec.size() != 0)
	{
		return false;
	}

	// Should not reallocate when adding elements within capacity
	int *original_data = vec.data();
	for (int i = 0; i < 50; ++i)
	{
		vec.push_back(i);
	}

	return vec.data() == original_data && vec.size() == 50;
}

TEST(vector_clear)
{
	NonTrivialType::reset_counters();

	msplat::container::vector<NonTrivialType> vec;

	for (int i = 0; i < 10; ++i)
	{
		vec.push_back(NonTrivialType(i));
	}

	size_t capacity_before  = vec.capacity();
	int    destructs_before = NonTrivialType::destruct_count;

	vec.clear();

	return vec.size() == 0 &&
	       vec.capacity() == capacity_before &&                            // Capacity preserved
	       NonTrivialType::destruct_count == destructs_before + 10;        // All elements destroyed
}

TEST(vector_resize_grow)
{
	msplat::container::vector<int> vec;
	vec.push_back(1);
	vec.push_back(2);

	vec.resize(5);

	return vec.size() == 5 &&
	       vec[0] == 1 &&
	       vec[1] == 2 &&
	       vec[2] == 0 &&        // Default constructed
	       vec[3] == 0 &&
	       vec[4] == 0;
}

TEST(vector_resize_grow_with_value)
{
	msplat::container::vector<int> vec;
	vec.push_back(1);
	vec.push_back(2);

	vec.resize(5, 42);

	return vec.size() == 5 &&
	       vec[0] == 1 &&
	       vec[1] == 2 &&
	       vec[2] == 42 &&
	       vec[3] == 42 &&
	       vec[4] == 42;
}

TEST(vector_resize_shrink)
{
	NonTrivialType::reset_counters();

	msplat::container::vector<NonTrivialType> vec;
	for (int i = 0; i < 10; ++i)
	{
		vec.push_back(NonTrivialType(i));
	}

	int destructs_before = NonTrivialType::destruct_count;
	vec.resize(5);

	return vec.size() == 5 &&
	       vec[4].value == 4 &&
	       NonTrivialType::destruct_count == destructs_before + 5;        // 5 elements destroyed
}

TEST(vector_shrink_to_fit)
{
	msplat::container::vector<int> vec;
	vec.reserve(100);

	for (int i = 0; i < 10; ++i)
	{
		vec.push_back(i);
	}

	size_t capacity_before = vec.capacity();
	vec.shrink_to_fit();

	return vec.size() == 10 &&
	       vec.capacity() == 10 &&
	       capacity_before > 10 &&        // Had excess capacity
	       vec[9] == 9;                   // Data preserved
}

TEST(vector_copy_constructor)
{
	msplat::container::vector<int> vec1;
	for (int i = 0; i < 5; ++i)
	{
		vec1.push_back(i * 2);
	}

	msplat::container::vector<int> vec2(vec1);

	if (vec2.size() != 5)
	{
		return false;
	}

	// Check deep copy
	for (int i = 0; i < 5; ++i)
	{
		if (vec2[i] != i * 2)
		{
			return false;
		}
	}

	// Modify original, copy should be unchanged
	vec1[0] = 999;
	return vec2[0] == 0;
}

TEST(vector_move_constructor)
{
	msplat::container::vector<int> vec1;
	for (int i = 0; i < 5; ++i)
	{
		vec1.push_back(i * 2);
	}

	int   *original_data     = vec1.data();
	size_t original_capacity = vec1.capacity();

	msplat::container::vector<int> vec2(std::move(vec1));

	return vec2.size() == 5 &&
	       vec2.data() == original_data &&        // Took ownership
	       vec2.capacity() == original_capacity &&
	       vec1.size() == 0 &&        // vec1 was emptied
	       vec1.data() == nullptr &&
	       vec2[4] == 8;
}

TEST(vector_copy_assignment)
{
	msplat::container::vector<int> vec1;
	msplat::container::vector<int> vec2;

	for (int i = 0; i < 3; ++i)
	{
		vec1.push_back(i);
		vec2.push_back(i + 10);        // Different values
	}

	vec2 = vec1;

	return vec2.size() == 3 &&
	       vec2[0] == 0 &&
	       vec2[1] == 1 &&
	       vec2[2] == 2;
}

TEST(vector_move_assignment)
{
	msplat::container::vector<int> vec1;
	msplat::container::vector<int> vec2;

	for (int i = 0; i < 3; ++i)
	{
		vec1.push_back(i * 3);
	}

	int *original_data = vec1.data();

	vec2 = std::move(vec1);

	return vec2.size() == 3 &&
	       vec2.data() == original_data &&
	       vec1.size() == 0 &&
	       vec1.data() == nullptr &&
	       vec2[2] == 6;
}

TEST(vector_element_access)
{
	msplat::container::vector<int> vec;
	for (int i = 0; i < 5; ++i)
	{
		vec.push_back(i * i);
	}

	return vec.front() == 0 &&
	       vec.back() == 16 &&
	       vec[2] == 4;
}

TEST(vector_iterators)
{
	msplat::container::vector<int> vec;
	for (int i = 0; i < 5; ++i)
	{
		vec.push_back(i);
	}

	// Test range-based for loop
	int expected = 0;
	for (const auto &value : vec)
	{
		if (value != expected++)
		{
			return false;
		}
	}

	// Test iterator arithmetic
	return (vec.end() - vec.begin()) == 5;
}

TEST(vector_destructor_calls)
{
	NonTrivialType::reset_counters();

	{
		msplat::container::vector<NonTrivialType> vec;
		for (int i = 0; i < 5; ++i)
		{
			vec.push_back(NonTrivialType(i));
		}
		// vec destructor should be called here
	}

	// All objects should be destroyed
	return NonTrivialType::construct_count == NonTrivialType::destruct_count;
}

TEST(vector_with_frame_arena)
{
	msplat::container::pmr::FrameArena frameArena;
	msplat::container::vector<int>     vec(frameArena.Resource());

	for (int i = 0; i < 10; ++i)
	{
		vec.push_back(i);
	}

	return vec.size() == 10 && vec[9] == 9;
}

#endif        // !MSPLAT_USE_STD_CONTAINERS

// Register all custom vector tests
void register_custom_vector_tests()
{
	// Tests are automatically registered via static constructors
}