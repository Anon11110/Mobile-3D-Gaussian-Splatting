cmake_minimum_required(VERSION 3.20)
project(RHI VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
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

# Common sources
target_sources(RHI PRIVATE
    src/rhi.cpp
)

# Vulkan backend sources
target_sources(RHI PRIVATE
    src/vulkan/vulkan_device.cpp
    src/vulkan/vulkan_semaphore.cpp
    src/vulkan/vulkan_fence.cpp
    src/vulkan/vulkan_buffer.cpp
    src/vulkan/vulkan_shader.cpp
    src/vulkan/vulkan_texture.cpp
    src/vulkan/vulkan_pipeline.cpp
    src/vulkan/vulkan_swapchain.cpp
    src/vulkan/vulkan_commandlist.cpp
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
)

# Private include for Vulkan backend
target_include_directories(RHI PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vulkan
)

# Link libraries
target_link_libraries(RHI PUBLIC glfw)

# Shader compilation
include(../cmake/compileshaders.cmake)
compile_shaders(
    ${CMAKE_CURRENT_SOURCE_DIR}/../shaders
    ${CMAKE_CURRENT_BINARY_DIR}/shaders/compiled
)