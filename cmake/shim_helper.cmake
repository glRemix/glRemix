# helper target allowing IDE to discover includes and C++20 settings for better IntelliSense 

# prefix relative paths with the shim source directory
set(_shim_full_sources "")
foreach(f ${GLREMIX_SHIM_SOURCE_FILES_REL} ${GLREMIX_SHIM_HEADER_FILES_REL})
    list(APPEND _shim_full_sources "${GLREMIX_SHIM_SOURCE_DIR}/${f}")
endforeach()

foreach(f ${GLREMIX_SHARED_SOURCE_FILES_REL} ${GLREMIX_SHARED_HEADER_FILES_REL})
    list(APPEND _shim_full_sources "${GLREMIX_SHARED_DIR}/${f}")
endforeach()

add_library(_glRemix_shim_intellisense STATIC EXCLUDE_FROM_ALL ${_shim_full_sources})

# treat as C++20 and add all relevant include directories
target_compile_features(_glRemix_shim_intellisense PRIVATE cxx_std_20)
target_include_directories(_glRemix_shim_intellisense PRIVATE
    "${GLREMIX_SHIM_SOURCE_DIR}"
    "${REPO_ROOT}/external/robin-map-1.4.0/include"
    "${CMAKE_BINARY_DIR}/external/shim-x64/generated"
    "${CMAKE_BINARY_DIR}/external/shim-win32/generated"
    "${REPO_ROOT}/build/external/shim-x64/generated"
    "${REPO_ROOT}/build/external/shim-win32/generated"
    "${REPO_ROOT}"
)

set_target_properties(_glRemix_shim_intellisense PROPERTIES
    OUTPUT_NAME "glremix_intellisense"
)

# compile intellisense helper target with same defs as real target
include("${GLREMIX_SHIM_SOURCE_DIR}/cmake/shim_compile_spec.cmake")
set_shim_compile_specifications(_glRemix_shim_intellisense)

# allow the renderer target to inherit its include paths
if(TARGET glRemix_renderer)
    target_link_libraries(glRemix_renderer PRIVATE _glRemix_shim_intellisense)
endif()