# Android CMakeLists.txt - Entry point for Gradle CMake integration
cmake_minimum_required(VERSION 3.22.1)
project(MSplatAndroid VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific configuration
if(NOT ANDROID)
    message(FATAL_ERROR "This CMakeLists.txt is for Android builds only")
endif()

message(STATUS "Building MSplat for Android")
message(STATUS "  Android ABI: ${ANDROID_ABI}")
message(STATUS "  Android Platform: ${ANDROID_PLATFORM}")
message(STATUS "  NDK: ${ANDROID_NDK}")

# Set the project root directory (used by cmake/*.cmake includes)
set(MSPLAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

option(ENABLE_SORT_VERIFICATION "Enable sort verification features" OFF)

# Include core library configuration
include(${MSPLAT_ROOT}/cmake/core.cmake)

# Include engine library configuration
include(${MSPLAT_ROOT}/cmake/engine.cmake)

# Add RHI subdirectory
add_subdirectory(${MSPLAT_ROOT}/rhi ${CMAKE_CURRENT_BINARY_DIR}/rhi)

# Include app library configuration (will use android-specific sources)
include(${MSPLAT_ROOT}/cmake/app.cmake)

# Include imgui library configuration
include(${MSPLAT_ROOT}/cmake/imgui.cmake)

# Find required Android libraries
find_library(log-lib log)
find_library(android-lib android)

# Build the main Android shared library
add_library(msplat_android SHARED
    app/src/main/cpp/android_main.cpp
    ${MSPLAT_ROOT}/examples/hybrid-splat-renderer/hybrid_splat_renderer_app.cpp
)

# Include android_native_app_glue
set(NATIVE_APP_GLUE_DIR "${ANDROID_NDK}/sources/android/native_app_glue")
add_library(native_app_glue STATIC
    ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c
)
# Set linker language explicitly since it's C-only
set_target_properties(native_app_glue PROPERTIES LINKER_LANGUAGE C)
target_include_directories(native_app_glue PUBLIC
    ${NATIVE_APP_GLUE_DIR}
)

target_include_directories(msplat_android PRIVATE
    ${MSPLAT_ROOT}/include
    ${MSPLAT_ROOT}/examples/hybrid-splat-renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/app/src/main/cpp
    ${NATIVE_APP_GLUE_DIR}
    ${MSPLAT_ROOT}/shaders  # For shaderio.h
)

target_link_libraries(msplat_android PRIVATE
    core
    engine
    app
    RHI
    imgui
    native_app_glue
    ${android-lib}
    ${log-lib}
)

if(ENABLE_SORT_VERIFICATION)
    target_compile_definitions(msplat_android PRIVATE ENABLE_SORT_VERIFICATION)
endif()

# Force export of ANativeActivity_onCreate
set_target_properties(msplat_android PROPERTIES
    LINK_FLAGS "-u ANativeActivity_onCreate"
)

# Set output properties
set_target_properties(msplat_android PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
