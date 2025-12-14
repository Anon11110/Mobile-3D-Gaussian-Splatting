# App static library configuration

# App library headers
set(APP_HEADERS
    ${MSPLAT_ROOT}/include/msplat/app/application.h
    ${MSPLAT_ROOT}/include/msplat/app/camera.h
    ${MSPLAT_ROOT}/include/msplat/app/device_manager.h
    ${MSPLAT_ROOT}/include/msplat/app/platform_adapter.h
)

# Common sources
set(APP_SOURCES
    ${MSPLAT_ROOT}/src/app/camera.cpp
    ${MSPLAT_ROOT}/src/app/device_manager.cpp
)

# Platform-specific sources
if(ANDROID)
    list(APPEND APP_SOURCES
        ${MSPLAT_ROOT}/src/app/android_adapter.cpp
    )
else()
    # Desktop platforms (Windows, Linux, macOS)
    list(APPEND APP_SOURCES
        ${MSPLAT_ROOT}/src/app/desktop_adapter.cpp
    )
endif()

# Create app static library
add_library(app STATIC ${APP_SOURCES} ${APP_HEADERS})

# Public include directories
target_include_directories(app PUBLIC
    ${MSPLAT_ROOT}/include
    ${MSPLAT_ROOT}/include/msplat
)

# Require C++20
target_compile_features(app PUBLIC cxx_std_20)

# Add MSVC-specific flags
if(MSVC)
    target_compile_options(app PUBLIC /utf-8)
endif()

# Platform-specific linking
if(ANDROID)
    # Find Android libraries
    find_library(android-lib android)
    find_library(log-lib log)

    target_link_libraries(app PUBLIC
        core
        RHI
        ${android-lib}
        ${log-lib}
    )

    # Include android_native_app_glue headers
    target_include_directories(app PUBLIC
        ${ANDROID_NDK}/sources/android/native_app_glue
    )
else()
    # Desktop platforms
    target_link_libraries(app PUBLIC
        core
        RHI
        glfw
    )
endif()

# Set target properties
set_target_properties(app PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
