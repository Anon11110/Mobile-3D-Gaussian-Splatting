# Scene Test Example

find_package(Python3 REQUIRED COMPONENTS Interpreter)

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

# Download and setup assets
add_custom_command(TARGET scene-test PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Downloading splat assets if needed"
)

# Copy assets to executable directory
add_custom_command(TARGET scene-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/assets/splats/flowers_1/flowers_1.ply
        $<TARGET_FILE_DIR:scene-test>/flowers_1.ply
    COMMENT "Copying splat asset to executable directory"
)