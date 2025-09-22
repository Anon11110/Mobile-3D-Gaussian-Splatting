#pragma once

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace rhi {
namespace test {

// Simple test framework for RHI
struct Test
{
    std::string name;
    std::function<bool()> test_func;

    Test(const std::string& n, std::function<bool()> f) :
        name(n), test_func(f)
    {}
};

extern std::vector<Test> g_tests;

// Test registration macro
#define RHI_TEST(name)                              \
    bool test_##name();                             \
    static bool register_##name = []() {            \
        rhi::test::g_tests.push_back({#name, test_##name}); \
        return true;                                \
    }();                                            \
    bool test_##name()

// Test assertion helpers
#define RHI_ASSERT(condition) \
    if (!(condition)) { \
        std::cerr << "Assertion failed: " #condition << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define RHI_ASSERT_EQ(expected, actual) \
    if ((expected) != (actual)) { \
        std::cerr << "Assertion failed: expected " << (expected) << " but got " << (actual) \
              << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define RHI_ASSERT_NE(expected, actual) \
    if ((expected) == (actual)) { \
        std::cerr << "Assertion failed: expected != " << (expected) \
              << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define RHI_ASSERT_NULL(ptr) \
    if ((ptr) != nullptr) { \
        std::cerr << "Assertion failed: expected nullptr at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

#define RHI_ASSERT_NOT_NULL(ptr) \
    if ((ptr) == nullptr) { \
        std::cerr << "Assertion failed: expected non-nullptr at " << __FILE__ << ":" << __LINE__ << std::endl; \
        return false; \
    }

} // namespace test
} // namespace rhi