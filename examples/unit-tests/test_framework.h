#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Simple test framework
struct Test
{
	std::string           name;
	std::function<bool()> test_func;

	Test(const std::string &n, std::function<bool()> f) :
	    name(n), test_func(f)
	{}
};

extern std::vector<Test> g_tests;

#define TEST(name)                               \
	bool        test_##name();                   \
	static bool register_##name = []() {         \
		g_tests.push_back({#name, test_##name}); \
		return true;                             \
	}();                                         \
	bool test_##name()