cmake_minimum_required(VERSION 3.18)
project(glRemix_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_GPU_BASED_VALIDATION "Enable GPU-based validation for debugging" OFF)

set(GLREMIX_SHARED_DIR "${REPO_ROOT}/shared")
include("${REPO_ROOT}/cmake/shared_files.cmake")

set(SHARED_HEADERS ${GLREMIX_SHARED_HEADER_FILES})
set(SHARED_SOURCES ${GLREMIX_SHARED_SOURCE_FILES})

set(dx_dir "${CMAKE_CURRENT_SOURCE_DIR}/dx")
set(gl_dir "${CMAKE_CURRENT_SOURCE_DIR}/gl")

set(RENDERER_SOURCES
    "main.cpp"
    "${dx_dir}/d3d12_context.cpp"
    "${dx_dir}/d3d12_descriptor_heap.cpp"
    "${dx_dir}/d3d12_pipeline_types.cpp"
    "${dx_dir}/d3d12_barrier.cpp"

    "${gl_dir}/gl_matrix_stack.cpp"

    "application.cpp"
    "rt_app.cpp"
    "debug_window.cpp"
)

set(RENDERER_HEADERS
    "${dx_dir}/d3d12_context.h"
    "${dx_dir}/d3d12_fence.h"
    "${dx_dir}/d3d12_queue.h"
    "${dx_dir}/d3d12_command_allocator.h"
    "${dx_dir}/d3d12_descriptor_heap.h"
    "${dx_dir}/d3d12_descriptor.h"
    "${dx_dir}/d3d12_as.h"
    "${dx_dir}/d3d12_pipeline_types.h"
    "${dx_dir}/d3d12_buffer.h"
    "${dx_dir}/d3d12_texture.h"
    "${dx_dir}/d3d12_barrier.h"

    "${gl_dir}/gl_matrix_stack.h"

    "structs.h"

    "application.h"
    "rt_app.h"
    "debug_window.h"
)

set(D3D12MA_SOURCES
    "${REPO_ROOT}/external/D3D12MemoryAllocator-3.0.1/src/D3D12MemAlloc.cpp"
)

set(SHADER_FILES
    "shaders/raster/triangle.hlsl"
    "shaders/rt/raytracing.hlsl"
)

set(IMGUI_DIR "${REPO_ROOT}/external/imgui")
file(GLOB IMGUI_SOURCES
    "${IMGUI_DIR}/imgui*.cpp"
)

add_executable(${PROJECT_NAME} 
    ${RENDERER_SOURCES} 
    ${RENDERER_HEADERS}
    ${SHARED_HEADERS}
    ${SHARED_SOURCES}
    ${D3D12MA_SOURCES}
    ${IMGUI_SOURCES}
    ${SHADER_FILES}
)

target_include_directories(${PROJECT_NAME} PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO_ROOT}/external/robin-map-1.4.0/include"
    "${REPO_ROOT}/external/D3D12MemoryAllocator-3.0.1/include"
    "${REPO_ROOT}/external/imgui"
    "${REPO_ROOT}"
)

set(_GLREMIX_COPY_IF_EXISTS_SCRIPT "${GLREMIX_COPY_IF_EXISTS_SCRIPT}")
if(NOT _GLREMIX_COPY_IF_EXISTS_SCRIPT)
    set(_GLREMIX_COPY_IF_EXISTS_SCRIPT "${REPO_ROOT}/cmake/copy_if_exists.cmake")
endif()

if(GLREMIX_DEPLOY_DIR)
    set(_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR "${GLREMIX_DEPLOY_DIR}/$<CONFIG>/renderer")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying renderer executable to ${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    )

    if(MSVC)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying renderer PDB to ${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}"
            COMMAND ${CMAKE_COMMAND}
                -Dsrc="$<TARGET_PDB_FILE:${PROJECT_NAME}>"
                -Ddst="${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
                -P "${_GLREMIX_COPY_IF_EXISTS_SCRIPT}"
        )
    endif()

endif()

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        NOMINMAX
        UNICODE 
        _UNICODE
        WIN32_LEAN_AND_MEAN
    )
    
    if(ENABLE_GPU_BASED_VALIDATION)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_GPU_BASED_VALIDATION)
    endif()
    
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /MP) # Enable multi-processor compilation
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE
        d3d12
        dxgi
        dxguid
        dxcompiler # No d3dcompiler since we only use DXC
        
        # Windows system libraries
        kernel32
        advapi32
    )

endif()

# Create the source groups for source tree with root at CMAKE_CURRENT_SOURCE_DIR
set(ALL_RENDERER_FILES ${RENDERER_SOURCES} ${RENDERER_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${ALL_RENDERER_FILES})
source_group("shared" FILES ${SHARED_HEADERS} ${SHARED_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/shaders" PREFIX "shaders" FILES ${SHADER_FILES})

# Exclude shader files from build
set_source_files_properties(${SHADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

set(_GLREMIX_SHIM_TARGET_PREFIX "${GLREMIX_SHIM_TARGET_PREFIX}")
if(NOT _GLREMIX_SHIM_TARGET_PREFIX)
    set(_GLREMIX_SHIM_TARGET_PREFIX "glRemix_shim_")
endif()

set(_GLREMIX_SHIM_TARGET_WIN32 "${_GLREMIX_SHIM_TARGET_PREFIX}win32")
set(_GLREMIX_SHIM_TARGET_X64 "${_GLREMIX_SHIM_TARGET_PREFIX}x64")

if(TARGET ${_GLREMIX_SHIM_TARGET_WIN32})
    add_dependencies(${PROJECT_NAME} ${_GLREMIX_SHIM_TARGET_WIN32})
endif()

if(TARGET ${_GLREMIX_SHIM_TARGET_X64})
    add_dependencies(${PROJECT_NAME} ${_GLREMIX_SHIM_TARGET_X64})
endif()

# Compile shaders using SXC.exe
add_custom_target(CompileShaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/shaders/SXC.exe -c ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shaders.config -sm 6_6 -out ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMENT "Compiling shaders with SXC.exe"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_dependencies(${PROJECT_NAME} CompileShaders)

if(GLREMIX_DEPLOY_DIR)
    add_custom_command(TARGET CompileShaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying shader files to ${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            "${_GLREMIX_RENDERER_DEPLOY_CONFIG_DIR}/shaders"
    )
endif()
