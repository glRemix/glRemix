include_guard(GLOBAL)

if(NOT DEFINED GLREMIX_SHARED_DIR)
	if(DEFINED REPO_ROOT)
		set(GLREMIX_SHARED_DIR "${REPO_ROOT}/shared")
	else()
		message(FATAL_ERROR "Define GLREMIX_SHARED_DIR before including shared_files.cmake")
	endif()
endif()

set(containers "containers")

set(GLREMIX_SHARED_HEADER_NAMES
	"shared_memory.h"
	"gl_commands.h"
	"ipc_protocol.h"
    "ipc_protocol.inl"
    "gl_utils.h"
	"math_utils.h"
    "debug_utils.h"
	"${containers}/free_list_vector.h"
)

set(GLREMIX_SHARED_SOURCE_NAMES
    "shared_memory.cpp"
    "ipc_protocol.cpp"
)

set(GLREMIX_SHARED_HEADER_FILES)
foreach(_shared_name IN LISTS GLREMIX_SHARED_HEADER_NAMES)
	list(APPEND GLREMIX_SHARED_HEADER_FILES "${GLREMIX_SHARED_DIR}/${_shared_name}")
endforeach()

set(GLREMIX_SHARED_SOURCE_FILES)
foreach(_shared_src IN LISTS GLREMIX_SHARED_SOURCE_NAMES)
	list(APPEND GLREMIX_SHARED_SOURCE_FILES "${GLREMIX_SHARED_DIR}/${_shared_src}")
endforeach()

function(glremix_shared_headers_relative out_var base_dir)
	set(_result)
	foreach(_hdr IN LISTS GLREMIX_SHARED_HEADER_FILES)
		file(RELATIVE_PATH _rel "${base_dir}" "${_hdr}")
		list(APPEND _result "${_rel}")
	endforeach()
	set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

function(glremix_shared_sources_relative out_var base_dir)
	set(_result)
	foreach(_src IN LISTS GLREMIX_SHARED_SOURCE_FILES)
		file(RELATIVE_PATH _rel "${base_dir}" "${_src}")
		list(APPEND _result "${_rel}")
	endforeach()
	set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()
