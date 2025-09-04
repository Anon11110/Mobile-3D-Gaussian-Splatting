#include "test_framework.h"
#include <msplat/core/log.h>

std::vector<Test> g_tests;

// Forward declarations for test functions
extern void register_platform_tests();
extern void register_pmr_allocator_tests();
extern void register_hash_tests();

int main()
{
	LOG_INFO("Running unit tests...\n");

	// Register all tests
	register_platform_tests();
	register_pmr_allocator_tests();
	register_hash_tests();

	int passed = 0;
	int failed = 0;

	for (const auto &test : g_tests)
	{
		LOG_INFO("Running test: {}... ", test.name);

		try
		{
			if (test.test_func())
			{
				LOG_INFO("PASSED");
				passed++;
			}
			else
			{
				LOG_INFO("FAILED");
				failed++;
			}
		}
		catch (const std::exception &e)
		{
			LOG_INFO("FAILED (exception: {})", e.what());
			failed++;
		}
		catch (...)
		{
			LOG_INFO("FAILED (unknown exception)");
			failed++;
		}
	}

	LOG_INFO("\nTest results: {} passed, {} failed", passed, failed);

	return failed == 0 ? 0 : 1;
}