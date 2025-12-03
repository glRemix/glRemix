# helper target allowing IDE to discover includes and C++20 settings for better IntelliSense 

function(set_shim_target_properties ${target} ${platform})
    # prefix relative paths with the shim source directory
    set(_shim_full_sources "")
    foreach(f ${GLREMIX_SHIM_SOURCE_FILES_REL} ${GLREMIX_SHIM_HEADER_FILES_REL})
        list(APPEND _shim_full_sources "${GLREMIX_SHIM_SOURCE_DIR}/${f}")
    endforeach()

    foreach(f ${GLREMIX_SHARED_SOURCE_FILES_REL} ${GLREMIX_SHARED_HEADER_FILES_REL})
        list(APPEND _shim_full_sources "${GLREMIX_SHARED_DIR}/${f}")
    endforeach()

    # treat as C++20 and add all relevant include directories
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE
        "${GLREMIX_SHIM_SOURCE_DIR}"
        "${REPO_ROOT}/external/robin-map-1.4.0/include"
        "${CMAKE_BINARY_DIR}/external/shim-x64/generated"
        "${CMAKE_BINARY_DIR}/external/shim-win32/generated"
        "${REPO_ROOT}/build/external/shim-x64/generated"
        "${REPO_ROOT}/build/external/shim-win32/generated"
        "${REPO_ROOT}"
    )

    set_shim_compile_specifications(${target})

    # allow the renderer target to inherit its include paths
    if(TARGET glRemix_renderer)
        target_link_libraries(glRemix_renderer PRIVATE ${target})
    endif()

    # MSVC SETTINGS
    set_property(DIRECTORY ${REPO_ROOT} PROPERTY VS_STARTUP_PROJECT ${target}) # set as startup proj

    if(GLREMIX_SHIM_DEBUGGER_EXE_PATH) # set launch executable within visual studio
        cmake_path(GET GLREMIX_SHIM_DEBUGGER_EXE_PATH PARENT_PATH GLREMIX_SHIM_DEBUGGER_DIR)
        set_target_properties(${target} PROPERTIES
            VS_DEBUGGER_COMMAND "${GLREMIX_SHIM_DEBUGGER_EXE_PATH}"
            VS_DEBUGGER_COMMAND_ARGUMENTS ""
            VS_DEBUGGER_WORKING_DIRECTORY "${GLREMIX_SHIM_DEBUGGER_DIR}"
        )
    endif()
endfunction()

# reuseable for shim target and intellisense helper target

function(set_shim_compile_specifications target)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE
            NOMINMAX
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            GLREMIX_EXPORTS
        )

        if(MSVC)
            target_compile_options(${target} PRIVATE /MP)
        endif()

        target_link_libraries(${target} PRIVATE
            kernel32
            user32
            advapi32
            ole32
        )
    endif()

    if(GLREMIX_OVERRIDE_RENDERER_PATH)
        target_compile_definitions(${target} PRIVATE GLREMIX_CUSTOM_RENDERER_EXE_PATH="${GLREMIX_CUSTOM_RENDERER_EXE_PATH}")
    endif()

    if(GLREMIX_AUTO_LAUNCH_RENDERER)
        target_compile_definitions(${target} PRIVATE GLREMIX_AUTO_LAUNCH_RENDERER)
    endif()
endfunction()
