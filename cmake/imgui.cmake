# ImGui library configuration

# ImGui core sources
set(IMGUI_SOURCES
    ${MSPLAT_ROOT}/third-party/imgui/imgui.cpp
    ${MSPLAT_ROOT}/third-party/imgui/imgui_demo.cpp
    ${MSPLAT_ROOT}/third-party/imgui/imgui_draw.cpp
    ${MSPLAT_ROOT}/third-party/imgui/imgui_tables.cpp
    ${MSPLAT_ROOT}/third-party/imgui/imgui_widgets.cpp
)

# ImGui backend sources
if(ANDROID)
    # Android backend + Vulkan
    set(IMGUI_BACKEND_SOURCES
        ${MSPLAT_ROOT}/third-party/imgui/backends/imgui_impl_android.cpp
        ${MSPLAT_ROOT}/third-party/imgui/backends/imgui_impl_vulkan.cpp
    )
else()
    # GLFW + Vulkan for desktop
    set(IMGUI_BACKEND_SOURCES
        ${MSPLAT_ROOT}/third-party/imgui/backends/imgui_impl_glfw.cpp
        ${MSPLAT_ROOT}/third-party/imgui/backends/imgui_impl_vulkan.cpp
    )
endif()

# Create imgui static library
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKEND_SOURCES})

# Public include directories
target_include_directories(imgui PUBLIC
    ${MSPLAT_ROOT}/third-party/imgui
    ${MSPLAT_ROOT}/third-party/imgui/backends
)

# Require C++20
target_compile_features(imgui PUBLIC cxx_std_20)

# Add MSVC-specific flags
if(MSVC)
    target_compile_options(imgui PUBLIC /utf-8)
endif()

# Link with required libraries
if(ANDROID)
    # Android: link with android and log libraries, Vulkan
    find_library(android-lib android)
    find_library(log-lib log)
    find_package(Vulkan REQUIRED)
    target_link_libraries(imgui PUBLIC
        ${android-lib}
        ${log-lib}
        Vulkan::Vulkan
    )
else()
    # Desktop: link with GLFW and Vulkan
    find_package(Vulkan REQUIRED)
    target_link_libraries(imgui PUBLIC
        glfw
        Vulkan::Vulkan
    )
endif()

# Set target properties
set_target_properties(imgui PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
