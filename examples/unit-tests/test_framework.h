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

// Simple wildcard pattern matching function
inline bool matches_pattern(const std::string &test_name, const std::string &pattern)
{
	// Handle exact matches and "all" pattern
	if (pattern == test_name || pattern == "all" || pattern == "*")
		return true;

	// Simple wildcard matching: supports * as wildcard for any sequence
	size_t pattern_pos = 0;
	size_t name_pos    = 0;

	while (pattern_pos < pattern.length() && name_pos < test_name.length())
	{
		if (pattern[pattern_pos] == '*')
		{
			// Skip the asterisk
			pattern_pos++;

			// If asterisk is at the end, match everything
			if (pattern_pos >= pattern.length())
				return true;

			// Find the next non-wildcard character in pattern
			char next_char = pattern[pattern_pos];

			// Advance name_pos until we find next_char or reach end
			while (name_pos < test_name.length() && test_name[name_pos] != next_char)
				name_pos++;
		}
		else if (pattern[pattern_pos] == test_name[name_pos])
		{
			// Characters match, advance both
			pattern_pos++;
			name_pos++;
		}
		else
		{
			// Characters don't match
			return false;
		}
	}

	// Handle remaining asterisks at end of pattern
	while (pattern_pos < pattern.length() && pattern[pattern_pos] == '*')
		pattern_pos++;

	// Pattern matches if we've consumed both strings
	return pattern_pos >= pattern.length() && name_pos >= test_name.length();
}

#define TEST(name)                               \
	bool        test_##name();                   \
	static bool register_##name = []() {         \
		g_tests.push_back({#name, test_##name}); \
		return true;                             \
	}();                                         \
	bool test_##name()