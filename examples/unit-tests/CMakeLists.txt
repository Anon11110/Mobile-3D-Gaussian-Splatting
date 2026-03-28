# Unit tests for core library

# Find required packages
find_package(Threads REQUIRED)

# Unit test executable
add_executable(unit-tests
    main.cpp
    test_platform.cpp
    test_pmr_allocators.cpp
    test_custom_vector.cpp
    test_hash.cpp
    test_string.cpp
    test_async_buffer_upload.cpp
    test_scene.cpp
    test_splat_loader.cpp
)

# Link with core library, engine, and RHI
target_link_libraries(unit-tests PRIVATE
    core
    engine
    RHI
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

# Setup asset downloading and copying for scene tests
include(${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake)
setup_splat_assets(unit-tests)