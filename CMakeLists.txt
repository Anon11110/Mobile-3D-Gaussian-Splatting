cmake_minimum_required(VERSION 3.20)
project(Mobile-3D-Gaussian-Splatting VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set MSPLAT_ROOT for cmake includes (can be overridden for Android builds)
if(NOT DEFINED MSPLAT_ROOT)
    set(MSPLAT_ROOT "${CMAKE_SOURCE_DIR}")
endif()

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Enable compile_commands.json for IDE integration (Debug and RelWithDebInfo)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# Core library options
option(MSPLAT_USE_STD_CONTAINERS "Use system STL containers instead of custom implementations" OFF)

# Windows-specific: Function to apply Windows macro fixes to a target
function(apply_windows_macro_fix target)
    if(WIN32)
        set(WINDOWS_SANITIZED_HEADER "${CMAKE_SOURCE_DIR}/include/msplat/core/windows_sanitized.h")
        if(MSVC)
            target_compile_options(${target} PRIVATE /FI"${WINDOWS_SANITIZED_HEADER}")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE -include "${WINDOWS_SANITIZED_HEADER}")
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -include "${WINDOWS_SANITIZED_HEADER}")
        endif()
    endif()
endfunction()

# Include core library configuration
include(cmake/core.cmake)

# Include engine library configuration
include(cmake/engine.cmake)

# Include imgui library configuration
include(cmake/imgui.cmake)

# Include app library configuration
include(cmake/app.cmake)

# Add subdirectories
add_subdirectory(rhi)
add_subdirectory(examples)

# Set startup project for Visual Studio
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT triangle)
endif()