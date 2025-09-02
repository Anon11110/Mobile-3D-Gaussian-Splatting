# Splat Loader Example

find_package(Threads REQUIRED)

add_executable(splat-loader-example
    main.cpp
)

target_link_libraries(splat-loader-example PRIVATE
    engine
    core
    Threads::Threads
)

target_include_directories(splat-loader-example PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(splat-loader-example PRIVATE cxx_std_20)

set_target_properties(splat-loader-example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Download and setup assets
add_custom_command(TARGET splat-loader-example PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
    COMMAND python ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Downloading splat assets if needed"
)

# Copy assets to working directory
add_custom_command(TARGET splat-loader-example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/splats/flowers_1/flowers_1.ply
        ${CMAKE_CURRENT_BINARY_DIR}/flowers_1.ply
    COMMENT "Copying splat asset to working directory"
)