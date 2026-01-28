# Engine static library configuration

set(ENGINE_HEADERS
    # Splat data structures
    ${MSPLAT_ROOT}/include/msplat/engine/splat/splat_soa.h
    ${MSPLAT_ROOT}/include/msplat/engine/splat/splat_loader.h
    ${MSPLAT_ROOT}/include/msplat/engine/splat/splat_mesh.h
    ${MSPLAT_ROOT}/include/msplat/engine/splat/splat_math.h
    # Sorting
    ${MSPLAT_ROOT}/include/msplat/engine/sorting/splat_sort_backend.h
    ${MSPLAT_ROOT}/include/msplat/engine/sorting/cpu_splat_sorter.h
    ${MSPLAT_ROOT}/include/msplat/engine/sorting/gpu_splat_sorter.h
    # Scene
    ${MSPLAT_ROOT}/include/msplat/engine/scene/scene.h
    # Rendering
    ${MSPLAT_ROOT}/include/msplat/engine/rendering/shader_factory.h
    ${MSPLAT_ROOT}/include/msplat/engine/rendering/mesh_generator.h
    # Shader IO (shared between C++ and HLSL)
    ${MSPLAT_ROOT}/shaders/shaderio.h
)

set(ENGINE_SOURCES
    # Splat data structures
    ${MSPLAT_ROOT}/src/engine/splat/splat_loader.cpp
    ${MSPLAT_ROOT}/src/engine/splat/splat_mesh.cpp
    # Sorting
    ${MSPLAT_ROOT}/src/engine/sorting/cpu_splat_sorter.cpp
    ${MSPLAT_ROOT}/src/engine/sorting/gpu_splat_sorter.cpp
    ${MSPLAT_ROOT}/src/engine/sorting/cpu_splat_sort_backend.cpp
    ${MSPLAT_ROOT}/src/engine/sorting/gpu_splat_sort_backend.cpp
    # Scene
    ${MSPLAT_ROOT}/src/engine/scene/scene.cpp
    # Rendering
    ${MSPLAT_ROOT}/src/engine/rendering/shader_factory.cpp
    ${MSPLAT_ROOT}/src/engine/rendering/mesh_generator.cpp
    # Third-party
    ${MSPLAT_ROOT}/third-party/miniply/miniply.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${MSPLAT_ROOT}
    ${MSPLAT_ROOT}/include
    ${MSPLAT_ROOT}/third-party/miniply
    ${MSPLAT_ROOT}/rhi/include
)

target_link_libraries(engine PUBLIC core RHI)

target_compile_features(engine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(engine PUBLIC /utf-8)
endif()

set_target_properties(engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
