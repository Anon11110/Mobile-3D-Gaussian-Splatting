add_executable(particles main.cpp)

target_link_libraries(particles PRIVATE RHI core)

target_compile_features(particles PRIVATE cxx_std_20)

if(WIN32)
    set_target_properties(particles PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
endif()

set_target_properties(particles PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(MSVC)
    set_target_properties(particles PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin/Debug
        RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin/Release
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}/bin/RelWithDebInfo
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}/bin/MinSizeRel
    )
endif()

# Ensure VS runs from the target's output directory so relative paths work
if(MSVC)
    set_property(TARGET particles PROPERTY VS_DEBUGGER_WORKING_DIRECTORY $<TARGET_FILE_DIR:particles>)
endif()

include(${CMAKE_SOURCE_DIR}/cmake/compileshaders.cmake)
add_shader_directory(particles ${CMAKE_CURRENT_SOURCE_DIR}/shaders)