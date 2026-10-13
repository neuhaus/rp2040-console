# Native data generators must not accidentally use the ARM toolchain.
# HOST_CC may be a wrapper (e.g. an ILP32 compiler); RUNNER may contain
# qemu-i386 plus arguments. The caller owns the tool's data ABI and dependencies.
function(rp2040_console_host_tool)
    cmake_parse_arguments(HT "" "NAME;HOST_CC" "SOURCES;INCLUDE_DIRS;COMPILE_OPTIONS;LINK_OPTIONS;DEPENDS;RUNNER" ${ARGN})
    if(NOT HT_NAME OR NOT HT_SOURCES)
        message(FATAL_ERROR "rp2040_console_host_tool requires NAME and SOURCES")
    endif()
    if(NOT HT_HOST_CC)
        find_program(RP2040_CONSOLE_HOST_CC NAMES cc gcc clang)
        if(NOT RP2040_CONSOLE_HOST_CC)
            message(FATAL_ERROR "No native C compiler found; specify HOST_CC")
        endif()
        set(HT_HOST_CC "${RP2040_CONSOLE_HOST_CC}")
    endif()
    set(_sources)
    foreach(src IN LISTS HT_SOURCES)
        get_filename_component(src "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _sources "${src}")
    endforeach()
    set(_includes)
    foreach(dir IN LISTS HT_INCLUDE_DIRS)
        get_filename_component(dir "${dir}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _includes -I "${dir}")
    endforeach()
    set(_output "${CMAKE_CURRENT_BINARY_DIR}/host-tools/${HT_NAME}")
    add_custom_command(OUTPUT "${_output}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/host-tools"
            COMMAND "${HT_HOST_CC}" ${HT_COMPILE_OPTIONS} ${_includes} ${_sources}
                    ${HT_LINK_OPTIONS} -o "${_output}"
            DEPENDS ${_sources} ${HT_DEPENDS}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            VERBATIM)
    add_custom_target(${HT_NAME}-build DEPENDS "${_output}")
    add_custom_target(${HT_NAME} DEPENDS ${HT_NAME}-build)
    # Use NAME_COMMAND in COMMAND and NAME_DEPENDS in DEPENDS. Include the
    # binary file as well as the target, so changed generators rerun consumers.
    # A custom target avoids
    # confusing the imported executable's location with a generated output.
    set(${HT_NAME}_COMMAND ${HT_RUNNER} "${_output}" PARENT_SCOPE)
    set(${HT_NAME}_DEPENDS ${HT_NAME} "${_output}" PARENT_SCOPE)
    set(${HT_NAME}_OUTPUT "${_output}" PARENT_SCOPE)
endfunction()
