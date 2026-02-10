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
            TARGET ${SHADER_TARGET_NAME} POST_BUILD
            COMMAND ${GLSLC_EXECUTABLE}
                    -I${SHADER_SRC_DIR}
                    -I${CMAKE_SOURCE_DIR}/shaders
                    ${SHADER_FILE} -o ${SPIRV_FILE} --target-spv=spv1.3
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

    set(ENABLE_METAL_CONVERSION OFF)
    if(APPLE AND DEFINED RHI_BACKEND AND RHI_BACKEND STREQUAL "METAL3")
        set(ENABLE_METAL_CONVERSION ON)
        find_program(METAL_SHADER_CONVERTER_EXECUTABLE metal-shaderconverter
            HINTS
                "/opt/metal-shaderconverter/bin"
                "/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin"
        )
        if(NOT METAL_SHADER_CONVERTER_EXECUTABLE)
            message(FATAL_ERROR "metal-shaderconverter not found! Install Metal Shader Converter and ensure it is in PATH")
        endif()
    endif()

    set(SHADER_COMPILED_DIR "${SHADER_SRC_DIR}/compiled")
    set(SHADER_BIN_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/shaders/compiled")

    file(MAKE_DIRECTORY ${SHADER_COMPILED_DIR})

    # Find all HLSL shader files (supports both new and legacy naming conventions)
    file(GLOB SHADER_FILES
        # New convention: *_vs.hlsl, *_fs.hlsl, etc.
        "${SHADER_SRC_DIR}/*_vs.hlsl"
        "${SHADER_SRC_DIR}/*_fs.hlsl"
        "${SHADER_SRC_DIR}/*_cs.hlsl"
        "${SHADER_SRC_DIR}/*_gs.hlsl"
        "${SHADER_SRC_DIR}/*_ts.hlsl"
        "${SHADER_SRC_DIR}/*_es.hlsl"
        # Legacy convention: *.vert.hlsl, *.frag.hlsl, etc.
        "${SHADER_SRC_DIR}/*.vert.hlsl"
        "${SHADER_SRC_DIR}/*.frag.hlsl"
        "${SHADER_SRC_DIR}/*.comp.hlsl"
        "${SHADER_SRC_DIR}/*.geom.hlsl"
        "${SHADER_SRC_DIR}/*.tesc.hlsl"
        "${SHADER_SRC_DIR}/*.tese.hlsl"
    )

    # Find shader header files for dependency tracking
    file(GLOB SHADER_HEADERS
        "${SHADER_SRC_DIR}/*.h"
        "${SHADER_SRC_DIR}/*_common.hlsl"
        "${CMAKE_SOURCE_DIR}/shaders/*.h"
        "${CMAKE_SOURCE_DIR}/shaders/*_common.hlsl"
    )

    set(SHADER_TARGET_NAME "${TARGET_NAME}_hlsl_shaders")
    add_custom_target(${SHADER_TARGET_NAME} ALL)

    set(ALL_SPIRV_FILES "")

    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

        # Extract stage from either naming convention and normalize
        # Two conventions supported:
        #   1. New: triangle_vs.hlsl, triangle_fs.hlsl, etc.
        #   2. Legacy: triangle.vert.hlsl, triangle.frag.hlsl, etc.
        set(STAGE "")
        set(SHADER_BASE_NAME "")

        # Try new naming convention first: *_vs.hlsl, *_fs.hlsl, etc.
        if(SHADER_NAME MATCHES "_(vs|fs|cs|gs|ts|es)\\.hlsl$")
            string(REGEX REPLACE ".*_(vs|fs|cs|gs|ts|es)\\.hlsl$" "\\1" STAGE ${SHADER_NAME})
            string(REGEX REPLACE "_(vs|fs|cs|gs|ts|es)\\.hlsl$" "" SHADER_BASE_NAME ${SHADER_NAME})
        # Try legacy naming convention: *.vert.hlsl, *.frag.hlsl, etc.
        elseif(SHADER_NAME MATCHES "\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$")
            string(REGEX REPLACE ".*\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$" "\\1" STAGE ${SHADER_NAME})
            string(REGEX REPLACE "\\.(vert|frag|comp|geom|tesc|tese)\\.hlsl$" "" SHADER_BASE_NAME ${SHADER_NAME})
            # Normalize legacy names to new convention
            if(STAGE STREQUAL "vert")
                set(STAGE "vs")
            elseif(STAGE STREQUAL "frag")
                set(STAGE "fs")
            elseif(STAGE STREQUAL "comp")
                set(STAGE "cs")
            elseif(STAGE STREQUAL "geom")
                set(STAGE "gs")
            elseif(STAGE STREQUAL "tesc")
                set(STAGE "ts")
            elseif(STAGE STREQUAL "tese")
                set(STAGE "es")
            endif()
        else()
            message(WARNING "Unknown shader naming convention for ${SHADER_NAME}, skipping")
            continue()
        endif()

        # Map normalized stage to DXC profile (single source of truth)
        if(STAGE STREQUAL "vs")
            set(SHADER_PROFILE "vs_6_0")
        elseif(STAGE STREQUAL "fs")
            set(SHADER_PROFILE "ps_6_0")
        elseif(STAGE STREQUAL "cs")
            set(SHADER_PROFILE "cs_6_0")
        elseif(STAGE STREQUAL "gs")
            set(SHADER_PROFILE "gs_6_0")
        elseif(STAGE STREQUAL "ts")
            set(SHADER_PROFILE "hs_6_0")
        elseif(STAGE STREQUAL "es")
            set(SHADER_PROFILE "ds_6_0")
        else()
            message(WARNING "Unknown stage ${STAGE} for ${SHADER_NAME}, skipping")
            continue()
        endif()

        set(SHADER_STAGE_SUFFIX ${STAGE})

        # Optional shader entrypoint override via source comment:
        #   // ENTRYPOINT: my_entry
        # Defaults to "main" when not provided.
        set(SHADER_ENTRYPOINT "main")
        file(READ ${SHADER_FILE} SHADER_SOURCE_TEXT)
        string(REGEX MATCH "ENTRYPOINT:[ \t]*[A-Za-z_][A-Za-z0-9_]*" SHADER_ENTRYPOINT_TAG "${SHADER_SOURCE_TEXT}")
        if(SHADER_ENTRYPOINT_TAG)
            string(REGEX REPLACE "ENTRYPOINT:[ \t]*" "" SHADER_ENTRYPOINT "${SHADER_ENTRYPOINT_TAG}")
        endif()

        set(OUTPUT_BASENAME "${SHADER_BASE_NAME}_${SHADER_STAGE_SUFFIX}")
        set(OUTPUT_NAME "${OUTPUT_BASENAME}.spv")
        set(SPIRV_FILE "${SHADER_COMPILED_DIR}/${OUTPUT_NAME}")

        # DXC command for SPIR-V output:
        # -spirv: Generate SPIR-V
        # -T: Shader profile (vs_6_0, ps_6_0, cs_6_0, etc.)
        # -E: Entry point (default: main, override via ENTRYPOINT tag)
        # -fspv-target-env=vulkan1.1: Target Vulkan 1.1
        # -I: Include directories
        # -D: Preprocessor defines (VULKAN or METAL3 based on backend)

        # Set backend-specific defines
        set(DXC_DEFINES "")
        if(ENABLE_METAL_CONVERSION)
            set(DXC_DEFINES "-D" "METAL3")
        else()
            set(DXC_DEFINES "-D" "VULKAN")
        endif()

        add_custom_command(
            TARGET ${SHADER_TARGET_NAME} POST_BUILD
            COMMAND ${DXC_EXECUTABLE}
                    -spirv
                    -T ${SHADER_PROFILE}
                    -E ${SHADER_ENTRYPOINT}
                    -fspv-target-env=vulkan1.1
                    -fvk-use-dx-layout
                    ${DXC_DEFINES}
                    -I ${SHADER_SRC_DIR}
                    -I ${CMAKE_SOURCE_DIR}/shaders
                    -Fo ${SPIRV_FILE}
                    ${SHADER_FILE}
            COMMENT "Compiling HLSL shader: ${SHADER_NAME} (entry: ${SHADER_ENTRYPOINT}) -> ${OUTPUT_NAME}"
            VERBATIM
        )

        list(APPEND ALL_SPIRV_FILES ${SPIRV_FILE})

        if(ENABLE_METAL_CONVERSION)
            set(DXIL_FILE "${SHADER_COMPILED_DIR}/${OUTPUT_BASENAME}.dxil")
            set(METALLIB_FILE "${SHADER_COMPILED_DIR}/${OUTPUT_BASENAME}.metallib")
            # Embed source-level debug data in DXIL so Metal GPU captures can resolve HLSL source.
            set(METAL_DXC_DEBUG_FLAGS
                -Zi
                -Qembed_debug
                -Qsource_in_debug_module
            )

            add_custom_command(
                TARGET ${SHADER_TARGET_NAME} POST_BUILD
                COMMAND ${DXC_EXECUTABLE}
                        -T ${SHADER_PROFILE}
                        -E ${SHADER_ENTRYPOINT}
                        ${METAL_DXC_DEBUG_FLAGS}
                        -D METAL3
                        -I ${SHADER_SRC_DIR}
                        -I ${CMAKE_SOURCE_DIR}/shaders
                        -Fo ${DXIL_FILE}
                        ${SHADER_FILE}
                COMMAND ${METAL_SHADER_CONVERTER_EXECUTABLE}
                        ${DXIL_FILE}
                        -o ${METALLIB_FILE}
                COMMENT "Compiling HLSL shader for Metal: ${SHADER_NAME} (entry: ${SHADER_ENTRYPOINT}) -> ${OUTPUT_BASENAME}.metallib"
                VERBATIM
            )
        endif()
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
    if(ENABLE_METAL_CONVERSION)
        message(STATUS "  Metal Shader Converter: ${METAL_SHADER_CONVERTER_EXECUTABLE}")
    endif()

    list(LENGTH SHADER_FILES SHADER_COUNT)
    list(LENGTH SHADER_HEADERS HEADER_COUNT)
    message(STATUS "  Found ${SHADER_COUNT} HLSL shader(s) to compile")
    message(STATUS "  Found ${HEADER_COUNT} HLSL header(s) for dependency tracking")
endfunction()

# Helper function to add a GLSL shader directory to a target
function(add_shader_directory TARGET_NAME SHADER_DIR)
    compile_glsl_shaders(${SHADER_DIR} ${TARGET_NAME})
endfunction()

# Helper function to add an HLSL shader directory to a target
function(add_hlsl_shader_directory TARGET_NAME SHADER_DIR)
    compile_hlsl_shaders(${SHADER_DIR} ${TARGET_NAME})
endfunction()
