# Naive Splat CPU Sort Example

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_executable(naive-splat-cpu
    main.cpp
    naive_splat_cpu_app.cpp
    naive_splat_cpu_app.h
)

target_link_libraries(naive-splat-cpu PRIVATE
    engine
    core
    app
    RHI
)

target_include_directories(naive-splat-cpu PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rhi/include
)

target_compile_features(naive-splat-cpu PRIVATE cxx_std_20)

# Apply Windows macro fix to this target
apply_windows_macro_fix(naive-splat-cpu)

# Platform-specific settings
if(WIN32)
    set_target_properties(naive-splat-cpu PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
endif()

set_target_properties(naive-splat-cpu PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    set_target_properties(naive-splat-cpu PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
    )
    set_property(TARGET naive-splat-cpu PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:naive-splat-cpu>)
endif()

# Download and setup assets
add_custom_command(TARGET naive-splat-cpu PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Downloading splat assets if needed"
)

# Copy assets to executable directory
add_custom_command(TARGET naive-splat-cpu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/splats/flowers_1/flowers_1.ply
        $<TARGET_FILE_DIR:naive-splat-cpu>/flowers_1.ply
    COMMENT "Copying splat asset to executable directory"
)

# Compile shaders
include(${CMAKE_SOURCE_DIR}/cmake/compileshaders.cmake)
add_shader_directory(naive-splat-cpu ${CMAKE_CURRENT_SOURCE_DIR}/shaders)