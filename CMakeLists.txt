cmake_minimum_required(VERSION 3.18)
project(glRemix-Workspace)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT WIN32)
    message(FATAL_ERROR "glRemix is Windows only")
endif()

# Set repo root for all subprojects
set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(GLREMIX_DEPLOY_DIR "${REPO_ROOT}/deploy" CACHE PATH "Directory where output is copied after build")

include(ExternalProject)

option(GLREMIX_BUILD_SHIM_WIN32 "Build 32-bit OpenGL shim" ON)
option(GLREMIX_BUILD_SHIM_X64 "Build 64-bit OpenGL shim" OFF)
option(GLREMIX_BUILD_RENDERER "Build renderer" ON)
option(GLREMIX_OVERRIDE_RENDERER_PATH "Override path to glRemix renderer. If 'GLREMIX_CUSTOM_RENDERER_EXE_PATH' is not set, default path to renderer executable within deploy directory is used." ON)
option(GLREMIX_AUTO_LAUNCH_RENDERER "Automatically launch renderer process (disable when using graphics debuggers like PIX)" ON)

set(GLREMIX_COPY_IF_EXISTS_SCRIPT "${REPO_ROOT}/cmake/copy_if_exists.cmake")

set(GLREMIX_SHIM_TARGET_PREFIX "glRemix_shim_")
set(GLREMIX_SHIM_SOURCE_DIR "${REPO_ROOT}/glRemixShim")
set(GLREMIX_SHARED_DIR "${REPO_ROOT}/shared") # Do not delete this

include("${REPO_ROOT}/cmake/shared_files.cmake")
include("${REPO_ROOT}/cmake/shim_helper.cmake")
include(${REPO_ROOT}/cmake/cmake_source_group.cmake)

set(GLREMIX_SHIM_EXTERNAL_PREFIX "UTILITY_")
set(GLREMIX_SHIM_EXTERNAL_SUFFIX "_Builder")

if(GLREMIX_OVERRIDE_RENDERER_PATH AND NOT DEFINED GLREMIX_CUSTOM_RENDERER_EXE_PATH)
    set(GLREMIX_CUSTOM_RENDERER_EXE_PATH "${GLREMIX_DEPLOY_DIR}/$<CONFIG>/renderer/glRemix_renderer.exe")

    message(AUTHOR_WARNING "'GLREMIX_OVERRIDE_RENDERER_PATH' CMake option is enabled but 'GLREMIX_CUSTOM_RENDERER_EXE_PATH' CMake variable was not specified. Setting to default path for now.")
endif()

# Add external project builders for the shim config, this is to allow building both 32-bit and 64-bit with the 64-bit renderer
function(glremix_add_shim_builder platform description)
    set(target "${GLREMIX_SHIM_TARGET_PREFIX}${platform}")
    set(builder "${GLREMIX_SHIM_EXTERNAL_PREFIX}${target}${GLREMIX_SHIM_EXTERNAL_SUFFIX}")
    set(GLREMIX_EXTERNAL_BINARY_DIR "${CMAKE_BINARY_DIR}/external/shim-${platform}")

    ExternalProject_Add(${builder}
        SOURCE_DIR "${GLREMIX_SHIM_SOURCE_DIR}"
        BINARY_DIR "${GLREMIX_EXTERNAL_BINARY_DIR}"
        CMAKE_GENERATOR "${CMAKE_GENERATOR}"
        CMAKE_GENERATOR_PLATFORM "${platform}"
        CMAKE_ARGS
        -DCMAKE_CXX_STANDARD=20
        -DCMAKE_CXX_STANDARD_REQUIRED=ON
        -DREPO_ROOT=${REPO_ROOT}
        -DGLREMIX_EXTERNAL_BINARY_DIR=${GLREMIX_EXTERNAL_BINARY_DIR}
        -DGLREMIX_DEPLOY_DIR=${GLREMIX_DEPLOY_DIR}
        -DGLREMIX_SHIM_DEPLOY_SUBDIR=shim/${platform}
        -DGLREMIX_OVERRIDE_RENDERER_PATH=${GLREMIX_OVERRIDE_RENDERER_PATH}
        -DGLREMIX_CUSTOM_RENDERER_EXE_PATH=${GLREMIX_CUSTOM_RENDERER_EXE_PATH}
        -DGLREMIX_AUTO_LAUNCH_RENDERER=${GLREMIX_AUTO_LAUNCH_RENDERER}
        BUILD_COMMAND ${CMAKE_COMMAND} --build . --config $<CONFIG>
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS
        "${GLREMIX_DEPLOY_DIR}/$<CONFIG>/shim/${platform}/opengl32.dll"
        "${GLREMIX_DEPLOY_DIR}/$<CONFIG>/shim/${platform}/opengl32.pdb"
    )
    create_shim_cmake_source_group(${builder})

    # Add a step to force rebuild check when source files change
    ExternalProject_Add_Step(${builder} force_build_check
        COMMAND ${CMAKE_COMMAND} -E echo "Checking if ${target} needs rebuild"
        DEPENDS ${GLREMIX_SHIM_ALL_FILES}
        DEPENDERS build
        ALWAYS FALSE
    )

    include("${GLREMIX_SHIM_SOURCE_DIR}/cmake/shim_sources.cmake")

    if(GLREMIX_DEPLOY_DIR)
        ExternalProject_Add_Step(${builder} deploy
            COMMAND ${CMAKE_COMMAND}
            -DGLREMIX_EXTERNAL_BINARY_DIR=${GLREMIX_EXTERNAL_BINARY_DIR}
            -Dconfig=$<CONFIG>
            -Doutput_dir=${GLREMIX_DEPLOY_DIR}/$<CONFIG>/shim/${platform}
            -P ${REPO_ROOT}/cmake/deploy_shim.cmake
            DEPENDEES build
            ALWAYS 1 # Can't figure out how to make this conditional on the build step running
        )
    endif()

    add_library(${target} STATIC EXCLUDE_FROM_ALL ${GLREMIX_SHIM_ALL_FILES})
    add_dependencies(${target} ${builder})

    set_shim_target_properties(${target} ${platform})
    create_shim_cmake_source_group(${target})
endfunction()

if((GLREMIX_BUILD_SHIM_WIN32 AND GLREMIX_BUILD_SHIM_X64) OR (NOT GLREMIX_BUILD_SHIM_WIN32 AND NOT GLREMIX_BUILD_SHIM_X64 AND NOT GLREMIX_BUILD_RENDERER))
    message(FATAL_ERROR "Enable exactly one shim architecture unless performing a renderer-only build (set GLREMIX_BUILD_RENDERER=ON)")
endif()

if(GLREMIX_BUILD_SHIM_WIN32)
    glRemix_add_shim_builder("Win32" "Win32" "Build 32-bit OpenGL shim")
endif()

if(GLREMIX_BUILD_SHIM_X64)
    glRemix_add_shim_builder("x64" "x64" "Build 64-bit OpenGL shim")
endif()

if(GLREMIX_BUILD_RENDERER)
    add_subdirectory(glRemixRenderer)
endif()
