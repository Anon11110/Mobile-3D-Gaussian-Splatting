# Core static library configuration

# Core library headers
set(CORE_HEADERS
    # Math headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/math.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/basics.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/vector.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/matrix.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/quaternion.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/affine.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/aabb.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/sphere.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/frustum.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/math/color.h

    # Logging headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/log.h

    # Timer headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/timer.h

    # VFS headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/vfs.h

    # Platform headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/platform.h

    # Memory/Containers headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/memory.h
)

set(CORE_SOURCES
    # Math sources (header-only for now)
    # Logging sources
    ${CMAKE_SOURCE_DIR}/src/core/log.cpp

    # Timer sources
    ${CMAKE_SOURCE_DIR}/src/core/timer.cpp

    # VFS sources
    ${CMAKE_SOURCE_DIR}/src/core/vfs.cpp

    # Platform sources
    ${CMAKE_SOURCE_DIR}/src/core/platform.cpp

    # Third-party sources
    ${CMAKE_SOURCE_DIR}/third-party/mimalloc/src/static.c

    # Add a dummy source file to create the library
    ${CMAKE_SOURCE_DIR}/src/core/core.cpp
)

# Create core static library
add_library(core STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Public include directories
target_include_directories(core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/msplat
    ${CMAKE_SOURCE_DIR}  # For third-party includes
    ${CMAKE_SOURCE_DIR}/third-party/glm
    ${CMAKE_SOURCE_DIR}/third-party/spdlog/include
    ${CMAKE_SOURCE_DIR}/third-party/mimalloc/include
)

# Require C++20 for C++ files and C11 for C files
target_compile_features(core PUBLIC cxx_std_20)
set_property(TARGET core PROPERTY C_STANDARD 11)
set_property(TARGET core PROPERTY C_STANDARD_REQUIRED ON)

# Add MSVC-specific flags
if(MSVC)
    target_compile_options(core PUBLIC /utf-8)
endif()

# Configure mimalloc for static linking
target_compile_definitions(core PRIVATE
    MI_STATIC_LIB
    MI_BUILD_SHARED=OFF
)

# Container selection: use system STL if option is enabled
if(MSPLAT_USE_SYSTEM_STL)
    target_compile_definitions(core PUBLIC MSPLAT_USE_SYSTEM_STL)
endif()

# Link GLM (header-only, so just for interface)
target_link_libraries(core PUBLIC)

# Set target properties
set_target_properties(core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)