# Shader compilation functions for GLSL and HLSL to SPIR-V

# GLSL to SPIR-V compilation using glslc
function(compile_glsl_shaders SHADER_SRC_DIR TARGET_NAME)
    find_program(GLSLC_EXECUTABLE glslc)

    if(NOT GLSLC_EXECUTABLE)
        message(FATAL_ERROR "glslc not found! Please install Vulkan SDK and ensure it's in PATH")
    endif()

    set(SHADER_COMPILED_DIR "${SHADER_SRC_DIR}/compiled")
    set(SHADER_BIN_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/shaders/compiled")

    file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

    file(GLOB SHADER_FILES
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag"
        "${SHADER_SRC_DIR}/*.comp"
        "${SHADER_SRC_DIR}/*.geom"
        "${SHADER_SRC_DIR}/*.tesc"
        "${SHADER_SRC_DIR}/*.tese"
    )

    file(GLOB SHADER_HEADERS
        "${SHADER_SRC_DIR}/*.h"
        "${CMAKE_SOURCE_DIR}/shaders/*.h"
    )

    set(SHADER_TARGET_NAME "${TARGET_NAME}_shaders")
    add_custom_target(${SHADER_TARGET_NAME} ALL)

    set(ALL_SPIRV_FILES "")

    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        set(SPIRV_FILE "${SHADER_COMPILED_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            TARGET ${SHADER_TARGET_NAME}
            COMMAND ${GLSLC_EXECUTABLE}
                    -I${SHADER_SRC_DIR}
                    -I${CMAKE_SOURCE_DIR}/shaders
                    ${SHADER_FILE} -o ${SPIRV_FILE} --target-spv=spv1.3
            DEPENDS ${SHADER_FILE} ${SHADER_HEADERS}
            COMMENT "Compiling GLSL shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
            VERBATIM
        )

        list(APPEND ALL_SPIRV_FILES ${SPIRV_FILE})
    endforeach()

    add_custom_command(
        TARGET ${SHADER_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_COMPILED_DIR} ${SHADER_BIN_DIR}
        COMMENT "Copying compiled GLSL shaders to $<CONFIG> output directory"
        VERBATIM
    )

    add_dependencies(${TARGET_NAME} ${SHADER_TARGET_NAME})

    message(STATUS "GLSL shader compilation configured for ${TARGET_NAME}:")
    message(STATUS "  Source directory: ${SHADER_SRC_DIR}")
    message(STATUS "  Compiled directory: ${SHADER_COMPILED_DIR}")

    list(LENGTH SHADER_FILES SHADER_COUNT)
    list(LENGTH SHADER_HEADERS HEADER_COUNT)
    message(STATUS "  Found ${SHADER_COUNT} GLSL shader(s) to compile")
    message(STATUS "  Found ${HEADER_COUNT} shader header(s) for dependency tracking")
endfunction()

# HLSL to SPIR-V compilation using DXC
function(compile_hlsl_shaders SHADER_SRC_DIR TARGET_NAME)
    # Find DXC compiler (DirectX Shader Compiler)
    find_program(DXC_EXECUTABLE dxc
        HINTS
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/bin"
            "C:/VulkanSDK/*/Bin"
    )

    if(NOT DXC_EXECUTABLE)
        message(FATAL_ERROR "dxc not found! Please install Vulkan SDK (includes DXC) and ensure it's in PATH")
    endif()

    set(SHADER_COMPILED_DIR "${SHADER_SRC_DIR}/compiled")
    set(SHADER_BIN_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/shaders/compiled")

    file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

    # Find all HLSL shader files
    file(GLOB SHADER_FILES
        "${SHADER_SRC_DIR}/*.vert.hlsl"
        "${SHADER_SRC_DIR}/*.frag.hlsl"
        "${SHADER_SRC_DIR}/*.comp.hlsl"
        "${SHADER_SRC_DIR}/*.geom.hlsl"
        "${SHADER_SRC_DIR}/*.tesc.hlsl"
        "${SHADER_SRC_DIR}/*.tese.hlsl"
    )

    # Find shader header files for dependency tracking
    file(GLOB SHADER_HEADERS
        "${SHADER_SRC_DIR}/*.hlsl"
        "${CMAKE_SOURCE_DIR}/shaders/*.hlsl"
    )
    # Remove actual shader files from headers list
    list(FILTER SHADER_HEADERS EXCLUDE REGEX "\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$")

    set(SHADER_TARGET_NAME "${TARGET_NAME}_hlsl_shaders")
    add_custom_target(${SHADER_TARGET_NAME} ALL)

    set(ALL_SPIRV_FILES "")

    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

        # Determine shader profile based on extension
        # Extract the stage from filename (e.g., "splat_raster.vert.hlsl" -> "vert")
        string(REGEX MATCH "\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$" SHADER_EXT ${SHADER_NAME})
        string(REGEX REPLACE "\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$" "" SHADER_BASE_NAME ${SHADER_NAME})

        if(SHADER_EXT MATCHES "\\.vert\\.hlsl$")
            set(SHADER_PROFILE "vs_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.vert.spv")
        elseif(SHADER_EXT MATCHES "\\.frag\\.hlsl$")
            set(SHADER_PROFILE "ps_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.frag.spv")
        elseif(SHADER_EXT MATCHES "\\.comp\\.hlsl$")
            set(SHADER_PROFILE "cs_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.comp.spv")
        elseif(SHADER_EXT MATCHES "\\.geom\\.hlsl$")
            set(SHADER_PROFILE "gs_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.geom.spv")
        elseif(SHADER_EXT MATCHES "\\.tesc\\.hlsl$")
            set(SHADER_PROFILE "hs_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.tesc.spv")
        elseif(SHADER_EXT MATCHES "\\.tese\\.hlsl$")
            set(SHADER_PROFILE "ds_6_0")
            set(OUTPUT_NAME "${SHADER_BASE_NAME}.tese.spv")
        else()
            message(WARNING "Unknown shader type for ${SHADER_NAME}, skipping")
            continue()
        endif()

        set(SPIRV_FILE "${SHADER_COMPILED_DIR}/${OUTPUT_NAME}")

        # DXC command for SPIR-V output:
        # -spirv: Generate SPIR-V
        # -T: Shader profile (vs_6_0, ps_6_0, cs_6_0, etc.)
        # -E: Entry point (main)
        # -fspv-target-env=vulkan1.1: Target Vulkan 1.1
        # -I: Include directories
        add_custom_command(
            TARGET ${SHADER_TARGET_NAME}
            COMMAND ${DXC_EXECUTABLE}
                    -spirv
                    -T ${SHADER_PROFILE}
                    -E main
                    -fspv-target-env=vulkan1.1
                    -I ${SHADER_SRC_DIR}
                    -I ${CMAKE_SOURCE_DIR}/shaders
                    -Fo ${SPIRV_FILE}
                    ${SHADER_FILE}
            DEPENDS ${SHADER_FILE} ${SHADER_HEADERS}
            COMMENT "Compiling HLSL shader: ${SHADER_NAME} -> ${OUTPUT_NAME}"
            VERBATIM
        )

        list(APPEND ALL_SPIRV_FILES ${SPIRV_FILE})
    endforeach()

    add_custom_command(
        TARGET ${SHADER_TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${SHADER_COMPILED_DIR} ${SHADER_BIN_DIR}
        COMMENT "Copying compiled HLSL shaders to $<CONFIG> output directory"
        VERBATIM
    )

    add_dependencies(${TARGET_NAME} ${SHADER_TARGET_NAME})

    message(STATUS "HLSL shader compilation configured for ${TARGET_NAME}:")
    message(STATUS "  Source directory: ${SHADER_SRC_DIR}")
    message(STATUS "  Compiled directory: ${SHADER_COMPILED_DIR}")
    message(STATUS "  DXC compiler: ${DXC_EXECUTABLE}")

    list(LENGTH SHADER_FILES SHADER_COUNT)
    list(LENGTH SHADER_HEADERS HEADER_COUNT)
    message(STATUS "  Found ${SHADER_COUNT} HLSL shader(s) to compile")
    message(STATUS "  Found ${HEADER_COUNT} HLSL header(s) for dependency tracking")
endfunction()

# Legacy function for backward compatibility (defaults to GLSL)
function(compile_shaders SHADER_SRC_DIR TARGET_NAME)
    compile_glsl_shaders(${SHADER_SRC_DIR} ${TARGET_NAME})
endfunction()

# Helper function to add a GLSL shader directory to a target
function(add_shader_directory TARGET_NAME SHADER_DIR)
    compile_glsl_shaders(${SHADER_DIR} ${TARGET_NAME})
endfunction()

# Helper function to add an HLSL shader directory to a target
function(add_hlsl_shader_directory TARGET_NAME SHADER_DIR)
    compile_hlsl_shaders(${SHADER_DIR} ${TARGET_NAME})
endfunction()
