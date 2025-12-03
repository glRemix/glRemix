set(GLREMIX_COMMON_CMAKE_FILES
    ${REPO_ROOT}/CMakeLists.txt
    ${REPO_ROOT}/cmake/shared_files.cmake
    ${REPO_ROOT}/cmake/copy_if_exists.cmake
    ${REPO_ROOT}/cmake/cmake_source_group.cmake
)

function(create_renderer_cmake_source_group target)
    set(GLREMIX_RENDERER_CMAKE_FILES
        ${GLREMIX_COMMON_CMAKE_FILES}
        ${REPO_ROOT}/glRemixRenderer/CMakeLists.txt
    )

    target_sources(${target} PRIVATE ${GLREMIX_RENDERER_CMAKE_FILES})

    set_source_files_properties(${GLREMIX_RENDERER_CMAKE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

    source_group(TREE ${REPO_ROOT} PREFIX "_cmake" FILES ${GLREMIX_RENDERER_CMAKE_FILES})
endfunction()

function(create_shim_cmake_source_group target)
    set(GLREMIX_SHIM_CMAKE_FILES
        ${GLREMIX_COMMON_CMAKE_FILES}
        ${REPO_ROOT}/cmake/deploy_shim.cmake
        ${REPO_ROOT}/cmake/shim_helper.cmake
        ${REPO_ROOT}/glRemixShim/CMakeLists.txt
        ${REPO_ROOT}/glRemixShim/cmake/shim_sources.cmake
    )
    target_sources(${target} PRIVATE ${GLREMIX_SHIM_CMAKE_FILES})

    set_source_files_properties(${GLREMIX_SHIM_CMAKE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)

    source_group(TREE ${REPO_ROOT} PREFIX "_cmake" FILES ${GLREMIX_SHIM_CMAKE_FILES})
endfunction()

