# Scene Test Example

add_executable(scene-test
    main.cpp
)

target_link_libraries(scene-test PRIVATE
    engine
    core
    RHI
)

target_include_directories(scene-test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/rhi/include
)

target_compile_features(scene-test PRIVATE cxx_std_20)

set_target_properties(scene-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(WIN32)
    apply_windows_macro_fix(scene-test)
endif()

# Setup asset downloading and copying
include(${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake)
setup_splat_assets(scene-test)