cmake_minimum_required(VERSION 3.20)
project(Mobile-3D-Gaussian-Splatting VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Core library options
option(MSPLAT_USE_SYSTEM_STL "Use system STL containers instead of custom implementations" OFF)

# Include core library configuration
include(cmake/core.cmake)

# Include engine library configuration
include(cmake/engine.cmake)

# Include app library configuration
include(cmake/app.cmake)

# Add subdirectories
add_subdirectory(rhi)
add_subdirectory(examples)

# Set startup project for Visual Studio
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT triangle)
endif()