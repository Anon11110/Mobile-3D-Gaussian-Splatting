#include "test_framework.h"
#include <chrono>
#include <iomanip>

namespace rhi {
namespace test {

// Global test registry
std::vector<Test> g_tests;

int run_tests(const std::string& filter = "all") {
    int passed = 0;
    int failed = 0;

    std::cout << "Running RHI Tests" << std::endl;
    std::cout << "=================" << std::endl;

    for (const auto& test : g_tests) {
        // Simple filter: run all tests if filter is "all" or test name contains filter
        if (filter != "all" && test.name.find(filter) == std::string::npos) {
            continue;
        }

        std::cout << "Running: " << std::setw(50) << std::left << test.name << " ... ";
        std::cout.flush();

        auto start = std::chrono::high_resolution_clock::now();
        bool result = false;

        try {
            result = test.test_func();
        } catch (const std::exception& e) {
            std::cout << "FAILED (exception: " << e.what() << ")" << std::endl;
            failed++;
            continue;
        } catch (...) {
            std::cout << "FAILED (unknown exception)" << std::endl;
            failed++;
            continue;
        }

        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

        if (result) {
            std::cout << "PASSED (" << duration.count() << " μs)" << std::endl;
            passed++;
        } else {
            std::cout << "FAILED" << std::endl;
            failed++;
        }
    }

    std::cout << std::endl;
    std::cout << "=================" << std::endl;
    std::cout << "Test Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return failed > 0 ? 1 : 0;
}

} // namespace test
} // namespace rhi

int main(int argc, char* argv[]) {
    std::string filter = "all";

    // Simple command line parsing
    if (argc > 1) {
        filter = argv[1];
    }

    if (filter == "--help" || filter == "-h") {
        std::cout << "Usage: " << argv[0] << " [test_filter]" << std::endl;
        std::cout << "  test_filter: Run only tests containing this string (default: all)" << std::endl;
        return 0;
    }

    return rhi::test::run_tests(filter);
}