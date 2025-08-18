#include "test_framework.h"

std::vector<Test> g_tests;

// Forward declarations for test functions
extern void register_platform_tests();
extern void register_allocator_tests();

int main()
{
	std::cout << "Running unit tests...\n\n";

	// Register all tests
	register_platform_tests();
	register_allocator_tests();

	int passed = 0;
	int failed = 0;

	for (const auto &test : g_tests)
	{
		std::cout << "Running test: " << test.name << "... ";

		try
		{
			if (test.test_func())
			{
				std::cout << "PASSED\n";
				passed++;
			}
			else
			{
				std::cout << "FAILED\n";
				failed++;
			}
		}
		catch (const std::exception &e)
		{
			std::cout << "FAILED (exception: " << e.what() << ")\n";
			failed++;
		}
		catch (...)
		{
			std::cout << "FAILED (unknown exception)\n";
			failed++;
		}
	}

	std::cout << "\nTest results: " << passed << " passed, " << failed << " failed\n";

	return failed == 0 ? 0 : 1;
}