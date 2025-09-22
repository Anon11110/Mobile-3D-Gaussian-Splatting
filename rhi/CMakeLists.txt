cmake_minimum_required(VERSION 3.20)
project(RHI VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always use Vulkan backend
set(RHI_BACKEND "VULKAN")

# Find packages
find_package(Vulkan REQUIRED)

# Use bundled GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../third-party/glfw ${CMAKE_CURRENT_BINARY_DIR}/glfw)

# RHI Library
add_library(RHI STATIC)


# Vulkan backend sources
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
    src/backends/vulkan/vma_implementation.cpp
)
target_compile_definitions(RHI PUBLIC RHI_VULKAN=1)
target_link_libraries(RHI PUBLIC Vulkan::Vulkan)

# Platform-specific settings for MoltenVK on macOS
if(APPLE)
    target_compile_definitions(RHI PUBLIC VK_USE_PLATFORM_MACOS_MVK=1)
endif()

# Include directories
target_include_directories(RHI PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/Vulkan-Headers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../third-party/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third-party/VMA/include
)

# Private include for Vulkan backend
target_include_directories(RHI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/backends/vulkan
)

# Link libraries
target_link_libraries(RHI PUBLIC glfw)

# Add /utf-8 flag for MSVC to fix Unicode support
if(MSVC)
    target_compile_options(RHI PUBLIC /utf-8)
endif()

# Option to build RHI tests
option(RHI_BUILD_TESTS "Build RHI tests" OFF)

# Add tests subdirectory if enabled
if(RHI_BUILD_TESTS)
    add_subdirectory(tests)
endif()