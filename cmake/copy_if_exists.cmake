# copy_if_exists.cmake - Helper script to copy file only if it exists
#
# This script is invoked via cmake -P and copies SRC_FILE to DEST_FILE
# only if SRC_FILE exists. This allows graceful handling of optional assets.
#
# Usage:
#   cmake -DSRC_FILE=/path/to/source -DDEST_FILE=/path/to/dest -P copy_if_exists.cmake
#

if(EXISTS "${SRC_FILE}")
    # Get the directory of the destination file
    get_filename_component(DEST_DIR "${DEST_FILE}" DIRECTORY)

    # Create destination directory if needed
    if(NOT EXISTS "${DEST_DIR}")
        file(MAKE_DIRECTORY "${DEST_DIR}")
    endif()

    # Copy the file
    file(COPY_FILE "${SRC_FILE}" "${DEST_FILE}" ONLY_IF_DIFFERENT)
endif()
