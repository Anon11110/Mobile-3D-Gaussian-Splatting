# Shader compilation function for GLSL to SPIR-V with automatic copying
function(compile_shaders SHADER_SRC_DIR TARGET_NAME)
    # Find glslc compiler
    find_program(GLSLC_EXECUTABLE glslc)

    if(NOT GLSLC_EXECUTABLE)
        message(FATAL_ERROR "glslc not found! Please install Vulkan SDK and ensure it's in PATH")
    endif()

    # Set up shader directories
    set(SHADER_COMPILED_DIR "${SHADER_SRC_DIR}/compiled")
    # Use generator expression to get the correct output directory based on configuration
    set(SHADER_BIN_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/shaders/compiled")

    # Create compiled directory
    file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

    # Find all shader files
    file(GLOB SHADER_FILES
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
        "${SHADER_SRC_DIR}/*.geom"
        "${SHADER_SRC_DIR}/*.tesc"
        "${SHADER_SRC_DIR}/*.tese"
    )

    # Find shader header files for dependency tracking
    file(GLOB SHADER_HEADERS
        "${SHADER_SRC_DIR}/*.h"
        "${CMAKE_SOURCE_DIR}/shaders/*.h"
    )

    # Create a custom target for shader compilation
    # Note: Uses TARGET-based approach for Xcode compatibility
    set(SHADER_TARGET_NAME "${TARGET_NAME}_shaders")
    add_custom_target(${SHADER_TARGET_NAME} ALL)

    # Keep track of all SPIRV files
    set(ALL_SPIRV_FILES "")

    # Add compilation commands with dependency tracking
    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        set(SPIRV_FILE "${SHADER_COMPILED_DIR}/${SHADER_NAME}.spv")

        # Add compilation command with header dependency tracking
        add_custom_command(
            TARGET ${SHADER_TARGET_NAME}
            COMMAND ${GLSLC_EXECUTABLE}
                    -I${SHADER_SRC_DIR}
                    -I${CMAKE_SOURCE_DIR}/shaders
                    ${SHADER_FILE} -o ${SPIRV_FILE} --target-spv=spv1.3
            DEPENDS ${SHADER_FILE} ${SHADER_HEADERS}
            COMMENT "Compiling shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
            VERBATIM
        )

        # Add to list of SPIRV files for copying
        list(APPEND ALL_SPIRV_FILES ${SPIRV_FILE})
    endforeach()

    # Copy all compiled shaders to the appropriate build configuration directory
    # This uses a generator expression to resolve at build time
    add_custom_command(
        TARGET ${SHADER_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_COMPILED_DIR} ${SHADER_BIN_DIR}
        COMMENT "Copying compiled shaders to $<CONFIG> output directory"
        VERBATIM
    )

    # Make the main target depend on shader compilation
    add_dependencies(${TARGET_NAME} ${SHADER_TARGET_NAME})

    # Report configuration
    message(STATUS "Shader compilation configured for ${TARGET_NAME}:")
    message(STATUS "  Source directory: ${SHADER_SRC_DIR}")
    message(STATUS "  Compiled directory: ${SHADER_COMPILED_DIR}")
    message(STATUS "  Output will be copied to: build/bin/<CONFIG>/shaders/compiled")
    message(STATUS "  Include paths: ${SHADER_SRC_DIR}, ${CMAKE_SOURCE_DIR}/shaders")

    # Count shaders and headers
    list(LENGTH SHADER_FILES SHADER_COUNT)
    list(LENGTH SHADER_HEADERS HEADER_COUNT)
    message(STATUS "  Found ${SHADER_COUNT} shader(s) to compile")
    message(STATUS "  Found ${HEADER_COUNT} shader header(s) for dependency tracking")
endfunction()

# Helper function to add a shader directory to a target
function(add_shader_directory TARGET_NAME SHADER_DIR)
    compile_shaders(${SHADER_DIR} ${TARGET_NAME})
endfunction()