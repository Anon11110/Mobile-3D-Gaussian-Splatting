# Splat Loader Example

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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

# Download and setup assets
add_custom_command(TARGET splat-loader PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Downloading splat assets if needed"
)

# Copy assets to working directory
add_custom_command(TARGET splat-loader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/splats/flowers_1/flowers_1.ply
        ${CMAKE_CURRENT_BINARY_DIR}/flowers_1.ply
    COMMENT "Copying splat asset to working directory"
)