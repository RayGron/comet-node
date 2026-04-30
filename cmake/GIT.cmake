#
# Copyright (C) 2021-2026 QuasarApp (MIT).
# Copyright (C) 2026-2026 RayGron (lGPLv3).
#

# This module provides The git utilty functions.
#**********************
# Availabel functions:
# *********************
# The updateGitVars macross - This macross update all GIT variables releative current cmake file.
#**********************
# Availabel VARIABLE:
# *********************
# The GIT_COMMIT_COUNT variable - This variable contains commits count
# The GIT_COMMIT_HASH variable - This variable contains short version of the buildet commit hash.
# Note:
# For update actualy value of git variables use the updateGitVars macros


if(DEFINED PROJECT_GIT_UTILS_SUPPORT)
  return()
else()
  set(PROJECT_GIT_UTILS_SUPPORT 1)
endif()

# This macros create or update next variables:
# GIT_COMMIT_COUNT - This variable contains count of the commits.
# GIT_COMMIT_HASH - This variable contains short hash of the current cummit.
macro(updateGitVars)

    execute_process(
        COMMAND git rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        )
    string(STRIP "${GIT_COMMIT_COUNT}" GIT_COMMIT_COUNT)

    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        )
    string(STRIP "${GIT_COMMIT_HASH}" GIT_COMMIT_HASH)

endmacro()

# This macros create or update next variables:
# GIT_COMMIT_COUNT - This variable contains count of the commits.
# GIT_COMMIT_COUNT_FROM - This variable contains count of the commits from the hash.
# GIT_COMMIT_HASH - This variable contains short hash of the current cummit.
macro(updateGitVarsWithHash hash)

    execute_process(
        COMMAND git rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_COUNT
        )
    string(STRIP "${GIT_COMMIT_COUNT}" GIT_COMMIT_COUNT)

    execute_process(
        COMMAND git rev-list ${hash}..HEAD --count
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_COUNT_FROM
        )
    string(STRIP "${GIT_COMMIT_COUNT_FROM}" GIT_COMMIT_COUNT_FROM)

    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        )
    string(STRIP "${GIT_COMMIT_HASH}" GIT_COMMIT_HASH)

endmacro()


# This function do some as cmake function configure_file but add files into target for convenient access from editor
# Arguments :
#  name - it is name of the target for that will be configuret selected file.
#  file - it is file that will be configured
function(configure_file_in name file)

    if (TARGET ${name}Templates)
        target_sources(${name}Templates PRIVATE "${file}.in")
    else()
        add_custom_target(${name}Templates ALL
            SOURCES "${file}.in"
        )
    endif()

    configure_file("${file}.in" ${file} @ONLY)

endfunction()