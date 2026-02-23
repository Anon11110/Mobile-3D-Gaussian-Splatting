# copy_assets.cmake - Centralized asset copying for splat examples
#
# This module provides functions to download and copy PLY assets
# to executable output directories in a unified manner.
#
# Usage in CMakeLists.txt:
#   include(${CMAKE_SOURCE_DIR}/cmake/copy_assets.cmake)
#   setup_splat_assets(target_name)
#

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Define asset mappings: source path relative to assets/splats/ -> destination filename
# These are the default assets that will be copied to all splat-rendering targets
set(SPLAT_ASSET_MAPPINGS
    "flowers_1/flowers_1.ply:flowers_1.ply"
    "train/point_cloud/iteration_30000/point_cloud.ply:train_30000.ply"
    "garden/garden-7k.splat:garden-7k.splat"
    "bicycle/point_cloud/iteration_7000/point_cloud.ply:bicycle_7000.ply"
    "kitchen/kitchen-7k.splat:kitchen-7k.splat"
)

# Function to add pre-build asset download step
# This runs the download_assets.py script before building
function(add_asset_download_step TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} PRE_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/assets/splats
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/download_assets.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Downloading splat assets if needed..."
    )
endfunction()

# Function to copy a single asset file to target's assets directory
# Only copies if the source file exists (graceful handling of missing assets)
function(copy_single_asset TARGET_NAME SRC_PATH DEST_NAME)
    set(FULL_SRC "${CMAKE_SOURCE_DIR}/assets/splats/${SRC_PATH}")
    set(DEST_DIR "$<TARGET_FILE_DIR:${TARGET_NAME}>/assets")

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DEST_DIR}
        COMMAND ${CMAKE_COMMAND}
            -DSRC_FILE=${FULL_SRC}
            -DDEST_FILE=${DEST_DIR}/${DEST_NAME}
            -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
        COMMENT "Copying ${DEST_NAME} if available"
    )
endfunction()

# Main function to set up all asset handling for a splat-rendering target
# This includes:
#   1. Pre-build asset download step
#   2. Post-build asset copying to target's assets/ directory
function(setup_splat_assets TARGET_NAME)
    # Add download step
    add_asset_download_step(${TARGET_NAME})

    # Create assets directory and copy each asset
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET_NAME}>/assets
        COMMENT "Creating assets directory"
    )

    # Copy each configured asset
    foreach(MAPPING ${SPLAT_ASSET_MAPPINGS})
        # Parse "source:dest" format
        string(REPLACE ":" ";" PARTS "${MAPPING}")
        list(GET PARTS 0 SRC_PATH)
        list(GET PARTS 1 DEST_NAME)

        set(FULL_SRC "${CMAKE_SOURCE_DIR}/assets/splats/${SRC_PATH}")

        # Use copy_if_different to avoid unnecessary copies
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -DSRC_FILE=${FULL_SRC}
                -DDEST_FILE=$<TARGET_FILE_DIR:${TARGET_NAME}>/assets/${DEST_NAME}
                -P ${CMAKE_SOURCE_DIR}/cmake/copy_if_exists.cmake
            COMMENT "Copying ${DEST_NAME}"
        )
    endforeach()
endfunction()
