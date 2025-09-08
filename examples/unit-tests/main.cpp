#include "test_framework.h"
#include <msplat/core/log.h>

std::vector<Test> g_tests;

// Forward declarations for test functions
extern void register_platform_tests();
extern void register_pmr_allocator_tests();
extern void register_hash_tests();
extern void register_custom_vector_tests();
extern void register_string_tests();

void show_usage(const char *program_name)
{
	LOG_INFO("Usage: {} [pattern|--list|--help]\n", program_name);
	LOG_INFO("\nOptions:");
	LOG_INFO("  (none)     Run all tests (default behavior)");
	LOG_INFO("  pattern    Run tests matching the pattern (supports * wildcards)");
	LOG_INFO("  all        Run all tests");
	LOG_INFO("  '*'        Run all tests (quoted wildcard)");
	LOG_INFO("  --list     List all available tests without running them");
	LOG_INFO("  --help     Show this help message");
	LOG_INFO("\nExamples:");
	LOG_INFO("  {}                        # Run all tests (default)", program_name);
	LOG_INFO("  {} all                    # Run all tests", program_name);
	LOG_INFO("  {} 'vector_*'             # Run all vector tests (quote wildcards!)", program_name);
	LOG_INFO("  {} 'string_hash*'         # Run all string hash tests", program_name);
	LOG_INFO("  {} aligned_malloc_basic   # Run a specific test", program_name);
	LOG_INFO("  {} --list                 # List available tests", program_name);
	LOG_INFO("\nNote: Wildcard patterns (*) must be quoted to prevent shell expansion.");
}

int main(int argc, char *argv[])
{
	// Parse command line arguments - default to "all" if no arguments provided
	std::string arg = "all";
	if (argc >= 2)
	{
		arg = argv[1];
	}
	else
	{
		LOG_INFO("No arguments provided - running all tests by default.\n");
	}

	// Handle help option
	if (arg == "--help")
	{
		show_usage(argv[0]);
		return 0;
	}

	// Register all tests
	register_platform_tests();
	register_pmr_allocator_tests();
	register_hash_tests();
	register_custom_vector_tests();
	register_string_tests();

	// Handle list option
	if (arg == "--list")
	{
		LOG_INFO("Available tests ({} total):", g_tests.size());
		for (const auto &test : g_tests)
		{
			LOG_INFO("  {}", test.name);
		}
		return 0;
	}

	// Filter tests based on pattern
	std::vector<Test> filtered_tests;
	for (const auto &test : g_tests)
	{
		if (matches_pattern(test.name, arg))
		{
			filtered_tests.push_back(test);
		}
	}

	// Check if any tests match the pattern
	if (filtered_tests.empty())
	{
		LOG_INFO("ERROR: No tests match pattern '{}'\n", arg);
		LOG_INFO("Use '{}  --list' to see available tests", argv[0]);
		return 1;
	}

	// Show what we're running
	if (arg == "all" || arg == "*")
	{
		LOG_INFO("Running all {} tests...\n", filtered_tests.size());
	}
	else
	{
		LOG_INFO("Running {} tests matching pattern '{}'...\n", filtered_tests.size(), arg);
	}

	int passed = 0;
	int failed = 0;

	for (const auto &test : filtered_tests)
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