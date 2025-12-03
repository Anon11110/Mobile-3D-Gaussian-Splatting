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

    # Parallel headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/parallel.h

    # Container headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/array.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/filesystem.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/functional.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/hash.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/memory.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/queue.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/string.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/unordered_map.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/unordered_set.h
    ${CMAKE_SOURCE_DIR}/include/msplat/core/containers/vector.h

    # Memory headers
    ${CMAKE_SOURCE_DIR}/include/msplat/core/memory/frame_arena.h
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

    # Parallel sources
    ${CMAKE_SOURCE_DIR}/src/core/parallel.cpp

    # Container sources
    ${CMAKE_SOURCE_DIR}/src/core/containers/filesystem.cpp

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

# ============================================================================
# Parallel CPU Sorting Configuration
# ============================================================================

option(MSPLAT_CPU_SORT_PARALLEL "Enable parallel CPU sorting" ON)

# Auto-detect std::execution support
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS}")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_REQUIRED_FLAGS "-std=c++20")
endif()

check_cxx_source_compiles("
    #include <execution>
    #include <algorithm>
    #include <vector>
    int main() {
        std::vector<int> v{3,1,2};
        std::sort(std::execution::par_unseq, v.begin(), v.end());
        return 0;
    }
" MSPLAT_HAS_STD_EXECUTION)

# Option to enable std::execution parallel policies (default based on detection)
option(MSPLAT_ENABLE_STD_PAR_SORT "Use std::execution parallel policies for sorting" ${MSPLAT_HAS_STD_EXECUTION})

if(MSPLAT_CPU_SORT_PARALLEL)
    target_compile_definitions(core PUBLIC MSPLAT_CPU_SORT_PARALLEL=1)
    message(STATUS "Parallel CPU sorting: ENABLED")
else()
    message(STATUS "Parallel CPU sorting: DISABLED")
endif()

if(MSPLAT_ENABLE_STD_PAR_SORT)
    target_compile_definitions(core PUBLIC MSPLAT_ENABLE_STD_PAR_SORT=1)
    message(STATUS "std::execution parallel policies: ENABLED")

    # Link TBB on Linux if available (required for parallel algorithms on some platforms)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(TBB QUIET)
        if(TBB_FOUND)
            target_link_libraries(core PUBLIC TBB::tbb)
            message(STATUS "TBB found and linked for parallel algorithms")
        else()
            message(STATUS "TBB not found - parallel algorithms may not work optimally")
        endif()
    endif()
else()
    message(STATUS "std::execution parallel policies: DISABLED")
endif()

# Link GLM (header-only, so just for interface)
target_link_libraries(core PUBLIC)

# Set target properties
set_target_properties(core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)