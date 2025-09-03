# Engine static library configuration

set(ENGINE_HEADERS
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_soa.h
    ${CMAKE_SOURCE_DIR}/include/msplat/engine/splat_loader.h
)

set(ENGINE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/engine/splat_loader.cpp
    ${CMAKE_SOURCE_DIR}/third-party/miniply/miniply.cpp
)

add_library(engine STATIC ${ENGINE_SOURCES} ${ENGINE_HEADERS})

target_include_directories(engine PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third-party/miniply
)

target_link_libraries(engine PUBLIC core)

target_compile_features(engine PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(engine PUBLIC /utf-8)
endif()

set_target_properties(engine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)