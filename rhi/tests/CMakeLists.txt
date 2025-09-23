# RHI Tests CMakeLists.txt

# RHI Test executable
add_executable(rhi-tests
    main.cpp
    test_ref_count.cpp
)

# Include directories
target_include_directories(rhi-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Link with threading support for thread safety tests
find_package(Threads REQUIRED)
target_link_libraries(rhi-tests PRIVATE
    Threads::Threads
)

# Require C++20
target_compile_features(rhi-tests PRIVATE cxx_std_20)

# Set output directory
set_target_properties(rhi-tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)