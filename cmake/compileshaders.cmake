# Shader compilation function for GLSL to SPIR-V
function(compile_shaders SHADER_SRC_DIR SHADER_OUT_DIR)
    # Find glslc compiler
    find_program(GLSLC_EXECUTABLE glslc REQUIRED)
    
    if(NOT GLSLC_EXECUTABLE)
        message(FATAL_ERROR "glslc not found! Please install Vulkan SDK")
    endif()
    
    # Create output directory
    file(MAKE_DIRECTORY ${SHADER_OUT_DIR})
    
    # Find all shader files
    file(GLOB_RECURSE SHADER_FILES
        "${SHADER_SRC_DIR}/*.vert"
        "${SHADER_SRC_DIR}/*.frag" 
        "${SHADER_SRC_DIR}/*.comp"
        "${SHADER_SRC_DIR}/*.geom"
        "${SHADER_SRC_DIR}/*.tesc"
        "${SHADER_SRC_DIR}/*.tese"
    )
    
    # Compile each shader file
    foreach(SHADER_FILE ${SHADER_FILES})
        get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)
        set(SPIRV_FILE "${SHADER_OUT_DIR}/${SHADER_NAME}.spv")
        
        add_custom_command(
            OUTPUT ${SPIRV_FILE}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${SPIRV_FILE}
            DEPENDS ${SHADER_FILE}
            COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
            VERBATIM
        )
        
        list(APPEND SPIRV_FILES ${SPIRV_FILE})
    endforeach()
    
    # Create custom target for all shaders
    add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_FILES})
endfunction()