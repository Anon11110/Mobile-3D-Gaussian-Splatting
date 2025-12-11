# Engine static library configuration

set(ENGINE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_soa.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_loader.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_sort_backend.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/cpu_splat_sorter.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/shader_factory.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/mesh_generator.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_mesh.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_math.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/scene.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/gpu_splat_sorter.h
    ${CMAKE_SOURCE_DIR}/shaders/shaderio.h
)

set(ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/engine/splat_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/cpu_splat_sorter.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/gpu_splat_sort_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/cpu_splat_sort_backend.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/shader_factory.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/mesh_generator.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/splat_mesh.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/scene.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/gpu_splat_sorter.cpp
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