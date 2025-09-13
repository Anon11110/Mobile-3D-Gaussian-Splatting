#include "perf_framework.h"
#include <algorithm>
#include <iostream>
#include <msplat/core/log.h>
#include <msplat/core/timer.h>
#include <string>

#ifdef _WIN32
#	include <fcntl.h>
#	include <io.h>
#	include <windows.h>
#endif

using namespace msplat::timer;

// Forward declarations of test functions
int vector_performance_main();
int hash_performance_main();

namespace
{

struct TestSuite
{
	const char *name;
	int (*test_function)();
};

TestSuite test_suites[] = {
    {"Vector Performance", vector_performance_main},
    {"Hash Performance", hash_performance_main}};

constexpr size_t num_test_suites = sizeof(test_suites) / sizeof(test_suites[0]);

void print_usage(const char *program_name)
{
	LOG_INFO("Usage: {} [test_name]", program_name);
	LOG_INFO("Available tests:");
	for (size_t i = 0; i < num_test_suites; ++i)
	{
		LOG_INFO("  {}: {}", i + 1, test_suites[i].name);
	}
	LOG_INFO("  all: Run all tests");
	LOG_INFO("");
	LOG_INFO("If no test is specified, all tests will be run.");
}

int run_test_suite(const TestSuite &suite)
{
	// Output handled by individual test suites now

	Timer timer;
	timer.start();

	int result = suite.test_function();

	timer.stop();
	double elapsed = timer.elapsedMilliseconds();

	// Summary output is handled by test suites themselves

	return result;
}

int run_all_tests()
{
	LOG_INFO("Running all performance test suites...");

	int   total_failed = 0;
	Timer total_timer;
	total_timer.start();

	for (size_t i = 0; i < num_test_suites; ++i)
	{
		int result = run_test_suite(test_suites[i]);
		if (result != 0)
		{
			total_failed++;
		}
	}

	total_timer.stop();
	double total_elapsed = total_timer.elapsedMilliseconds();

	perf::log_main_footer(num_test_suites, num_test_suites - total_failed, total_elapsed);

	return total_failed;
}

}        // anonymous namespace

int main(int argc, char *argv[])
{
#ifdef _WIN32
	// Set Windows console to UTF-8 to properly display box-drawing characters
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif

	perf::log_main_header();

	if (argc == 1)
	{
		// No arguments - run all tests
		return run_all_tests();
	}

	if (argc != 2)
	{
		print_usage(argv[0]);
		return 1;
	}

	std::string test_name = argv[1];

	if (test_name == "help" || test_name == "--help" || test_name == "-h")
	{
		print_usage(argv[0]);
		return 0;
	}

	if (test_name == "all")
	{
		return run_all_tests();
	}

	// Try to parse as test number
	try
	{
		int test_number = std::stoi(test_name);
		if (test_number >= 1 && test_number <= static_cast<int>(num_test_suites))
		{
			return run_test_suite(test_suites[test_number - 1]);
		}
	}
	catch (const std::exception &)
	{
		// Not a number, try name matching
	}

	// Try to match by name (case-insensitive partial match)
	for (size_t i = 0; i < num_test_suites; ++i)
	{
		std::string suite_name = test_suites[i].name;

		// Convert both to lowercase for comparison
		std::transform(suite_name.begin(), suite_name.end(), suite_name.begin(), ::tolower);
		std::transform(test_name.begin(), test_name.end(), test_name.begin(), ::tolower);

		if (suite_name.find(test_name) != std::string::npos)
		{
			return run_test_suite(test_suites[i]);
		}
	}

	LOG_ERROR("Unknown test: '{}'", argv[1]);
	print_usage(argv[0]);
	return 1;
}