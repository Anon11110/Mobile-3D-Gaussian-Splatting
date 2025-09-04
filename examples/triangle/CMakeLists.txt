# Triangle Example Executable
add_executable(triangle main.cpp)

# Link with RHI and core libraries
target_link_libraries(triangle PRIVATE RHI core)

# Set C++ standard
target_compile_features(triangle PRIVATE cxx_std_20)

# Platform-specific settings
if(WIN32)
    set_target_properties(triangle PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
endif()

# Set output directories
set_target_properties(triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# For multi-config generators (e.g. Visual Studio), ensure per-config subdirs
if(MSVC)
    set_target_properties(triangle PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
    )
endif()

# Ensure VS runs from the target's output directory so relative paths work
if(MSVC)
    set_property(TARGET triangle PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:triangle>)
endif()

# Use the improved shader compilation system
include(${CMAKE_SOURCE_DIR}/cmake/compileshaders.cmake)
add_shader_directory(triangle ${CMAKE_CURRENT_SOURCE_DIR}/shaders)