# Splat Loader Example

add_executable(splat-loader
    main.cpp
)

target_link_libraries(splat-loader PRIVATE
    engine
    core
)

target_include_directories(splat-loader PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(splat-loader PRIVATE cxx_std_20)

set_target_properties(splat-loader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Setup asset downloading and copying
include(${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake)
setup_splat_assets(splat-loader)