cmake_minimum_required(VERSION 3.20)
project(RHI VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Backend selection
set(RHI_BACKEND "VULKAN" CACHE STRING "RHI backend (VULKAN or METAL3)")
set_property(CACHE RHI_BACKEND PROPERTY STRINGS VULKAN METAL3)
string(TOUPPER "${RHI_BACKEND}" RHI_BACKEND)

if(NOT RHI_BACKEND STREQUAL "VULKAN" AND NOT RHI_BACKEND STREQUAL "METAL3")
    message(FATAL_ERROR "Unsupported RHI_BACKEND='${RHI_BACKEND}'. Supported values: VULKAN, METAL3")
endif()

if(RHI_BACKEND STREQUAL "METAL3" AND NOT APPLE)
    message(FATAL_ERROR "RHI_BACKEND=METAL3 is only supported on Apple platforms")
endif()

message(STATUS "RHI backend: ${RHI_BACKEND}")

# Use bundled GLFW only on desktop platforms (not Android)
if(NOT ANDROID)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third-party/glfw ${CMAKE_CURRENT_BINARY_DIR}/glfw)
endif()

# RHI Library
add_library(RHI STATIC)

if(RHI_BACKEND STREQUAL "VULKAN")
    # Find Vulkan package - platform specific
    if(ANDROID)
        # Android NDK includes Vulkan
        find_library(VULKAN_LIB vulkan)
        if(NOT VULKAN_LIB)
            message(FATAL_ERROR "Vulkan library not found in Android NDK")
        endif()
        message(STATUS "Found Android Vulkan: ${VULKAN_LIB}")
    else()
        find_package(Vulkan REQUIRED)
    endif()

    target_sources(RHI PRIVATE
        src/backends/vulkan/vulkan_device.cpp
        src/backends/vulkan/vulkan_semaphore.cpp
        src/backends/vulkan/vulkan_fence.cpp
        src/backends/vulkan/vulkan_buffer.cpp
        src/backends/vulkan/vulkan_shader.cpp
        src/backends/vulkan/vulkan_texture.cpp
        src/backends/vulkan/vulkan_textureview.cpp
        src/backends/vulkan/vulkan_pipeline.cpp
        src/backends/vulkan/vulkan_swapchain.cpp
        src/backends/vulkan/vulkan_commandlist.cpp
        src/backends/vulkan/vulkan_descriptorset.cpp
        src/backends/vulkan/vulkan_conversions.cpp
        src/backends/vulkan/vulkan_query.cpp
        src/backends/vulkan/vma_implementation.cpp
    )

    target_compile_definitions(RHI PUBLIC RHI_VULKAN=1)

    # Platform-specific Vulkan configuration and linking
    if(ANDROID)
        target_compile_definitions(RHI PUBLIC VK_USE_PLATFORM_ANDROID_KHR=1)
        # VMA needs dynamic function loading on Android since the NDK only exports Vulkan 1.0 functions
        # Vulkan 1.1+ functions must be loaded dynamically via vkGetInstanceProcAddr/vkGetDeviceProcAddr
        target_compile_definitions(RHI PRIVATE VMA_DYNAMIC_VULKAN_FUNCTIONS=1 VMA_STATIC_VULKAN_FUNCTIONS=0)
        target_link_libraries(RHI PUBLIC ${VULKAN_LIB} android log)
    elseif(APPLE)
        target_compile_definitions(RHI PUBLIC VK_USE_PLATFORM_MACOS_MVK=1)
        target_link_libraries(RHI PUBLIC Vulkan::Vulkan)
    else()
        # Windows and Linux
        target_link_libraries(RHI PUBLIC Vulkan::Vulkan)
    endif()

    target_include_directories(RHI PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/VMA/include
    )

    target_include_directories(RHI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan
    )

    # Suppress nullability warnings from VMA third-party library
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        target_compile_options(RHI PRIVATE -Wno-nullability-completeness)
    endif()
elseif(RHI_BACKEND STREQUAL "METAL3")
    target_sources(RHI PRIVATE
        src/backends/metal3/metal_backend.cpp
        src/backends/metal3/metal_device.cpp
        src/backends/metal3/metal_buffer.cpp
        src/backends/metal3/metal_texture.cpp
        src/backends/metal3/metal_textureview.cpp
        src/backends/metal3/metal_shader.cpp
        src/backends/metal3/metal_pipeline.cpp
        src/backends/metal3/metal_commandlist.cpp
        src/backends/metal3/metal_swapchain.cpp
        src/backends/metal3/metal_descriptorset.cpp
        src/backends/metal3/metal_query.cpp
        src/backends/metal3/metal_conversions.cpp
        src/backends/metal3/metal_fence.cpp
        src/backends/metal3/metal_semaphore.cpp
    )

    target_compile_definitions(RHI PUBLIC RHI_METAL3=1)

    set_source_files_properties(
        src/backends/metal3/metal_backend.cpp
        PROPERTIES COMPILE_DEFINITIONS "MTL_PRIVATE_IMPLEMENTATION;NS_PRIVATE_IMPLEMENTATION;CA_PRIVATE_IMPLEMENTATION"
    )

    target_include_directories(RHI PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/metal-cpp
    )

    target_include_directories(RHI PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/metal3
    )

    target_link_libraries(RHI PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation"
        "-framework AppKit"
    )
endif()

# Common include directories
target_include_directories(RHI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# GLFW include only on desktop platforms
if(NOT ANDROID)
    target_include_directories(RHI PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/glfw/include
    )
endif()

# Link GLFW only on desktop platforms
if(NOT ANDROID)
    target_link_libraries(RHI PUBLIC glfw)
endif()

# Add /utf-8 flag for MSVC to fix Unicode support
if(MSVC)
    target_compile_options(RHI PUBLIC /utf-8)
endif()

# Option to build RHI tests
option(RHI_BUILD_TESTS "Build RHI tests" OFF)

# Add tests subdirectory if enabled (desktop only for now)
if(RHI_BUILD_TESTS AND NOT ANDROID)
    add_subdirectory(tests)
endif()
