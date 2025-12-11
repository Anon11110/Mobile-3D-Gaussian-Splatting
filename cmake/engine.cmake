# Engine static library configuration

set(ENGINE_HEADERS
    # Splat data structures
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat/splat_soa.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat/splat_loader.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat/splat_mesh.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat/splat_math.h
    # Sorting
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/sorting/splat_sort_backend.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/sorting/cpu_splat_sorter.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/sorting/gpu_splat_sorter.h
    # Scene
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/scene/scene.h
    # Rendering
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/rendering/shader_factory.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/rendering/mesh_generator.h
    # Shader IO
    ${CMAKE_SOURCE_DIR}/shaders/shaderio.h
)

set(ENGINE_SOURCES
    # Splat data structures
    ${CMAKE_SOURCE_DIR}/src/engine/splat/splat_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/splat/splat_mesh.cpp
    # Sorting
    ${CMAKE_SOURCE_DIR}/src/engine/sorting/cpu_splat_sorter.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/sorting/gpu_splat_sorter.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/sorting/cpu_splat_sort_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/sorting/gpu_splat_sort_backend.cpp
    # Scene
    ${CMAKE_SOURCE_DIR}/src/engine/scene/scene.cpp
    # Rendering
    ${CMAKE_SOURCE_DIR}/src/engine/rendering/shader_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/rendering/mesh_generator.cpp
    # Third-party
    ${CMAKE_SOURCE_DIR}/third-party/miniply/miniply.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third-party/miniply
    ${CMAKE_SOURCE_DIR}/rhi/include
    ${CMAKE_SOURCE_DIR}/shaders
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
