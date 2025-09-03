# Performance tests for PMR allocators and containers

# Main performance test suite
add_executable(perf-tests 
    main.cpp
    vector_performance.cpp
    hash_performance.cpp
)

# Link with core library
target_link_libraries(perf-tests PRIVATE core)

# Require C++20
target_compile_features(perf-tests PRIVATE cxx_std_20)

# Set output directory
set_target_properties(perf-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
)

# Add platform-specific libraries for threading
if(WIN32)
    # Windows threading support is built-in
elseif(APPLE)
    # macOS threading support is built-in
else()
    # Linux - link pthread
    target_link_libraries(perf-tests PRIVATE pthread)
endif()