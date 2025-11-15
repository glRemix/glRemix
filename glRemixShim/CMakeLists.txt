cmake_minimum_required(VERSION 3.18)
project(glRemix_shim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY CXX_STANDARD 20)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GL_XML_REGISTRY "${CMAKE_CURRENT_SOURCE_DIR}/gl.xml")
set(GL_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GL_GENERATED_WRAPPERS "${GL_GENERATED_DIR}/gl_wrappers.inl")
set(GL_GENERATED_ALIASES "${GL_GENERATED_DIR}/gl_export_aliases.inl")
set(GL_GENERATED_REGISTER "${GL_GENERATED_DIR}/gl_register.inl")
set(WGL_GENERATED_WRAPPERS "${GL_GENERATED_DIR}/wgl_wrappers.inl")
set(WGL_GENERATED_REGISTER "${GL_GENERATED_DIR}/wgl_register.inl")

add_custom_command(
    OUTPUT "${GL_GENERATED_WRAPPERS}" "${GL_GENERATED_ALIASES}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_gl_wrappers.py"
        --xml "${GL_XML_REGISTRY}"
        --output "${GL_GENERATED_WRAPPERS}"
        --min-version 1.0
        --max-version 1.1
        --alias-output "${GL_GENERATED_ALIASES}"
    DEPENDS
        "${GL_XML_REGISTRY}"
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_gl_wrappers.py"
)

add_custom_command(
    OUTPUT "${WGL_GENERATED_WRAPPERS}"
    COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_wgl_wrappers.py"
        --output "${WGL_GENERATED_WRAPPERS}"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_wgl_wrappers.py"
)

add_custom_target(glremix_generate_gl_wrappers DEPENDS "${GL_GENERATED_WRAPPERS}" "${GL_GENERATED_ALIASES}")
add_custom_target(glremix_generate_wgl_wrappers DEPENDS "${WGL_GENERATED_WRAPPERS}")

add_custom_target(glRemixShim_GeneratedHeaders
    DEPENDS
        "${GL_GENERATED_WRAPPERS}"
        "${GL_GENERATED_ALIASES}"
        "${WGL_GENERATED_WRAPPERS}"
)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/shim_sources.cmake)

if(NOT DEFINED REPO_ROOT)
    set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

if(NOT DEFINED GLREMIX_DEPLOY_DIR)
    set(GLREMIX_DEPLOY_DIR "${REPO_ROOT}/deploy")
endif()

set(_GLREMIX_SHIM_DEPLOY_CONFIG_DIR "")
if(GLREMIX_DEPLOY_DIR)
    set(_GLREMIX_SHIM_DEPLOY_CONFIG_DIR "${GLREMIX_DEPLOY_DIR}/$<CONFIG>")
    if(DEFINED GLREMIX_SHIM_DEPLOY_SUBDIR AND NOT "${GLREMIX_SHIM_DEPLOY_SUBDIR}" STREQUAL "")
        set(_GLREMIX_SHIM_DEPLOY_CONFIG_DIR "${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}/${GLREMIX_SHIM_DEPLOY_SUBDIR}")
    endif()
endif()

if(NOT TARGET ${PROJECT_NAME})
    add_library(${PROJECT_NAME} SHARED
        ${GLREMIX_SHIM_SOURCE_FILES_REL}
        ${GLREMIX_SHIM_HEADER_FILES_REL}
        ${GLREMIX_SHARED_HEADER_FILES_REL}
        ${GLREMIX_SHARED_SOURCE_FILES_REL}
        ${GLREMIX_SHIM_SCRIPT_FILES_REL}
        "${GL_GENERATED_WRAPPERS}"
        "${GL_GENERATED_ALIASES}"
        "${WGL_GENERATED_WRAPPERS}"
    )

    set_source_files_properties("${GL_GENERATED_WRAPPERS}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set_source_files_properties("${GL_GENERATED_ALIASES}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set_source_files_properties("${WGL_GENERATED_WRAPPERS}" PROPERTIES HEADER_FILE_ONLY TRUE)
    set_source_files_properties(${GLREMIX_SHIM_SCRIPT_FILES_REL} PROPERTIES HEADER_FILE_ONLY TRUE)

    set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME "opengl32"
        SUFFIX ".dll"
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${GL_GENERATED_DIR}"
        "${REPO_ROOT}/external/robin-map-1.4.0/include"
        "${REPO_ROOT}"
    )
endif()

add_dependencies(${PROJECT_NAME} glremix_generate_gl_wrappers)
add_dependencies(${PROJECT_NAME} glremix_generate_wgl_wrappers)

set(_GLREMIX_COPY_IF_EXISTS_SCRIPT "${GLREMIX_COPY_IF_EXISTS_SCRIPT}")
if(NOT _GLREMIX_COPY_IF_EXISTS_SCRIPT)
    set(_GLREMIX_COPY_IF_EXISTS_SCRIPT "${REPO_ROOT}/cmake/copy_if_exists.cmake")
endif()

if(_GLREMIX_SHIM_DEPLOY_CONFIG_DIR)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copying opengl32 shim to ${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${PROJECT_NAME}>
            "${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    )

    if(MSVC)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copying shim PDB to ${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}"
            COMMAND ${CMAKE_COMMAND}
                -Dsrc="$<TARGET_PDB_FILE:${PROJECT_NAME}>"
                -Ddst="${_GLREMIX_SHIM_DEPLOY_CONFIG_DIR}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
                -P "${_GLREMIX_COPY_IF_EXISTS_SCRIPT}"
        )
    endif()
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/shim_compile_spec.cmake)
set_shim_compile_specifications(${PROJECT_NAME})