# Building Visual Studio file lists requires the top level CMakeLists.txt to know every shim source, so declare them in a shared helper here.

set(GL_XML_REGISTRY "${CMAKE_CURRENT_SOURCE_DIR}/gl.xml")
set(GLREMIX_GL_GENERATED_DIR "${GLREMIX_EXTERNAL_BINARY_DIR}/includes/generated")
set(GL_GENERATED_WRAPPERS "${GLREMIX_GL_GENERATED_DIR}/gl_wrappers.inl")
set(GL_GENERATED_ALIASES "${GLREMIX_GL_GENERATED_DIR}/gl_export_aliases.inl")
set(GL_GENERATED_REGISTER "${GLREMIX_GL_GENERATED_DIR}/gl_register.inl")
set(WGL_GENERATED_WRAPPERS "${GLREMIX_GL_GENERATED_DIR}/wgl_wrappers.inl")

set(GLEXT_GENERATED_ENUMS "${GLREMIX_GL_GENERATED_DIR}/glext_enums.inl")

set(GLREMIX_SHIM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(GLREMIX_SHARED_DIR "${GLREMIX_SHIM_SOURCE_DIR}/../shared")

include("${GLREMIX_SHIM_SOURCE_DIR}/../cmake/shared_files.cmake")

set(GLREMIX_SHIM_SOURCE_FILES
    "${GLREMIX_SHIM_SOURCE_DIR}/dllmain.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_exports.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_loader.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_hooks.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/wgl_exports.cpp"

    # override_modules
    "${GLREMIX_SHIM_SOURCE_DIR}/override_modules/state_query.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/override_modules/multitexture.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/override_modules/wgl.cpp"
    "${GLREMIX_SHIM_SOURCE_DIR}/override_modules/client_state.cpp"
)

set(GLREMIX_SHIM_HEADER_FILES
    "${GLREMIX_SHIM_SOURCE_DIR}/framework.h"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_hooks.h"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_loader.h"
    "${GLREMIX_SHIM_SOURCE_DIR}/wgl_export_aliases.inl"
    "${GLREMIX_SHIM_SOURCE_DIR}/export_macros.h"
    "${GLREMIX_SHIM_SOURCE_DIR}/undef_export_macros.h"
    "${GLREMIX_SHIM_SOURCE_DIR}/gl_extensions.inl"
    "${GLREMIX_SHIM_SOURCE_DIR}/override_modules/common_includes.h"
)

set(GLREMIX_SHIM_RESOURCE_FILES
    "${GLREMIX_SHIM_SOURCE_DIR}/gl.xml"
    "${GLREMIX_SHIM_SOURCE_DIR}/scripts/generate_gl_wrappers.py"
    "${GLREMIX_SHIM_SOURCE_DIR}/scripts/generate_wgl_wrappers.py"
    "${GLREMIX_SHIM_SOURCE_DIR}/scripts/generate_glext_enums.py"
)

set(GLREMIX_SHIM_ALL_FILES
    ${GLREMIX_SHIM_SOURCE_FILES}
    ${GLREMIX_SHIM_HEADER_FILES}
    ${GLREMIX_SHARED_HEADER_FILES}
    ${GLREMIX_SHARED_SOURCE_FILES}
    ${GLREMIX_SHIM_RESOURCE_FILES}
)

set_source_files_properties(${GLREMIX_SHARED_HEADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${GLREMIX_SHIM_HEADER_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties(${GLREMIX_SHIM_RESOURCE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

set(GLREMIX_SHIM_SOURCE_FILES_REL)
foreach(_shim_src IN LISTS GLREMIX_SHIM_SOURCE_FILES)
    file(RELATIVE_PATH _shim_rel "${GLREMIX_SHIM_SOURCE_DIR}" "${_shim_src}")
    list(APPEND GLREMIX_SHIM_SOURCE_FILES_REL "${_shim_rel}")
endforeach()

set(GLREMIX_SHIM_HEADER_FILES_REL)
foreach(_shim_hdr IN LISTS GLREMIX_SHIM_HEADER_FILES)
    file(RELATIVE_PATH _shim_rel "${GLREMIX_SHIM_SOURCE_DIR}" "${_shim_hdr}")
    list(APPEND GLREMIX_SHIM_HEADER_FILES_REL "${_shim_rel}")
endforeach()

set(GLREMIX_SHIM_RESOURCE_FILES_REL)
foreach(_shim_resource IN LISTS GLREMIX_SHIM_RESOURCE_FILES)
    file(RELATIVE_PATH _shim_rel "${GLREMIX_SHIM_SOURCE_DIR}" "${_shim_resource}")
    list(APPEND GLREMIX_SHIM_RESOURCE_FILES_REL "${_shim_rel}")
endforeach()

glremix_shared_headers_relative(GLREMIX_SHARED_HEADER_FILES_REL "${GLREMIX_SHIM_SOURCE_DIR}")
glremix_shared_sources_relative(GLREMIX_SHARED_SOURCE_FILES_REL "${GLREMIX_SHIM_SOURCE_DIR}")

if(NOT DEFINED GLREMIX_SHIM_SUPPRESS_SOURCE_GROUPS)
    source_group(TREE "${GLREMIX_SHIM_SOURCE_DIR}" FILES ${GLREMIX_SHIM_SOURCE_FILES} ${GLREMIX_SHIM_HEADER_FILES})
    source_group(TREE "${GLREMIX_SHARED_DIR}" PREFIX "shared" FILES ${GLREMIX_SHARED_HEADER_FILES} ${GLREMIX_SHARED_SOURCE_FILES})
    source_group(TREE "${GLREMIX_SHIM_SOURCE_DIR}" PREFIX "_resource" FILES ${GLREMIX_SHIM_RESOURCE_FILES})
endif()
