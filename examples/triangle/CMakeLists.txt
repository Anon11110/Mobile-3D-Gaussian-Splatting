# Triangle Example Executable
add_executable(triangle main.cpp)

# Link with RHI library
target_link_libraries(triangle PRIVATE RHI)

# Set C++ standard
target_compile_features(triangle PRIVATE cxx_std_17)

# Platform-specific settings
if(WIN32)
    set_target_properties(triangle PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
endif()

# Set output directory
set_target_properties(triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Copy shaders to output directory (if they exist)
if(EXISTS ${CMAKE_SOURCE_DIR}/shaders/compiled)
    add_custom_command(TARGET triangle POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/shaders/compiled 
        ${CMAKE_BINARY_DIR}/bin/shaders/compiled
        COMMENT "Copying compiled shaders to output directory"
    )
endif()