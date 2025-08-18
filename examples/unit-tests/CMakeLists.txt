# Unit tests for core library

# Find required packages
find_package(Threads REQUIRED)

# Unit test executable
add_executable(unit-tests
    main.cpp
    test_platform.cpp
    test_allocators.cpp
    test_custom_vector.cpp
)

# Link with core library
target_link_libraries(unit-tests PRIVATE
    core
    Threads::Threads
)

# Include directories
target_include_directories(unit-tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Require C++20
target_compile_features(unit-tests PRIVATE cxx_std_20)

# Set output directory
set_target_properties(unit-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)