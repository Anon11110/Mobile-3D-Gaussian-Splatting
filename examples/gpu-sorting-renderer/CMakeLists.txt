# GPU Sorting Renderer Example Executable
add_executable(gpu-sorting-renderer
    main.cpp
    gpu_sorting_renderer_app.cpp
    gpu_sorting_renderer_app.h
)

# Link with RHI, core, engine, app, and imgui libraries
target_link_libraries(gpu-sorting-renderer PRIVATE RHI core engine app imgui)

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

# Setup asset downloading and copying
include(${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake)
setup_splat_assets(gpu-sorting-renderer)