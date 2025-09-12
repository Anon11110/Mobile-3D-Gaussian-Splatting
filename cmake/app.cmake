# App static library configuration

# App library headers
set(APP_HEADERS
    ${CMAKE_SOURCE_DIR}/include/msplat/app/application.h
    ${CMAKE_SOURCE_DIR}/include/msplat/app/device_manager.h
)

set(APP_SOURCES
    ${CMAKE_SOURCE_DIR}/src/app/device_manager.cpp
)

# Create app static library
add_library(app STATIC ${APP_SOURCES} ${APP_HEADERS})

# Public include directories
target_include_directories(app PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/msplat
)

# Require C++20
target_compile_features(app PUBLIC cxx_std_20)

# Add MSVC-specific flags
if(MSVC)
    target_compile_options(app PUBLIC /utf-8)
endif()

# Link with required libraries
target_link_libraries(app PUBLIC
    core
    RHI
    glfw
)

# Set target properties
set_target_properties(app PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)