# GPU Sorting Renderer Example Executable
add_executable(gpu-sorting-renderer
    main.cpp
    gpu_sorting_renderer_app.cpp
    gpu_sorting_renderer_app.h
)

# Link with RHI, core, engine, and app libraries
target_link_libraries(gpu-sorting-renderer PRIVATE RHI core engine app)

# Set C++ standard
target_compile_features(gpu-sorting-renderer PRIVATE cxx_std_20)

# Apply Windows macro fix to this target
apply_windows_macro_fix(gpu-sorting-renderer)

# Platform-specific settings
if(WIN32)
    set_target_properties(gpu-sorting-renderer PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
endif()

# Set output directories
set_target_properties(gpu-sorting-renderer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# For multi-config generators (e.g. Visual Studio), ensure per-config subdirs
if(MSVC)
    set_target_properties(gpu-sorting-renderer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
    )
endif()

# Ensure VS runs from the target's output directory so relative paths work
if(MSVC)
    set_property(TARGET gpu-sorting-renderer PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:gpu-sorting-renderer>)
endif()

# Use the improved shader compilation system
include(${CMAKE_SOURCE_DIR}/cmake/compileshaders.cmake)
# Compile shaders from the main shaders directory
add_shader_directory(gpu-sorting-renderer ${CMAKE_SOURCE_DIR}/shaders)

# Find Python for asset downloading
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET gpu-sorting-renderer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Downloading splat assets if needed"
)

# Create assets directory and copy splat file
add_custom_command(TARGET gpu-sorting-renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:gpu-sorting-renderer>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/splats/flowers_1/flowers_1.ply
        $<TARGET_FILE_DIR:gpu-sorting-renderer>/assets/test.splat
    COMMENT "Copying splat asset to executable directory"
)