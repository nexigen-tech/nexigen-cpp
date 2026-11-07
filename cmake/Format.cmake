# clang-format integration.
#
# Adds two custom targets:
#   format       - rewrite the C++ sources in place with .clang-format
#   format-check - fail if any source differs (--dry-run --Werror)
#
# clang-format 22.1.8 is the pinned version: CI installs it through the Python
# package so the gate and a local run produce identical output. A different
# major version still works but can reformat a handful of declarations (for
# example the spacing of a reference to an array), so configure warns.

find_program(NEXIGEN_CLANG_FORMAT
    NAMES clang-format-22 clang-format
    DOC "clang-format executable used by the format and format-check targets")

if(NOT NEXIGEN_CLANG_FORMAT)
    message(STATUS "clang-format not found: `format` and `format-check` targets disabled")
    return()
endif()

execute_process(
    COMMAND "${NEXIGEN_CLANG_FORMAT}" --version
    OUTPUT_VARIABLE NEXIGEN_CLANG_FORMAT_VERSION_OUTPUT
    ERROR_VARIABLE NEXIGEN_CLANG_FORMAT_VERSION_OUTPUT
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NEXIGEN_CLANG_FORMAT_VERSION_OUTPUT MATCHES "([0-9]+)\\.[0-9]+\\.[0-9]+")
    set(NEXIGEN_CLANG_FORMAT_VERSION "${CMAKE_MATCH_1}")
    string(REGEX MATCH "^[0-9]+" NEXIGEN_CLANG_FORMAT_MAJOR "${NEXIGEN_CLANG_FORMAT_VERSION}")
    if(NOT NEXIGEN_CLANG_FORMAT_MAJOR STREQUAL "22")
        message(WARNING
            "clang-format ${NEXIGEN_CLANG_FORMAT_VERSION} found; the project pins "
            "22.1.8. The check may report differences that CI does not see.")
    endif()
endif()

add_custom_target(format
    COMMAND "${CMAKE_COMMAND}"
        -DCLANG_FORMAT=${NEXIGEN_CLANG_FORMAT}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DFORMAT_MODE=apply
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Formatting C++ sources with clang-format"
    USES_TERMINAL
    VERBATIM)

add_custom_target(format-check
    COMMAND "${CMAKE_COMMAND}"
        -DCLANG_FORMAT=${NEXIGEN_CLANG_FORMAT}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DFORMAT_MODE=check
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunClangFormat.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking C++ formatting with clang-format"
    USES_TERMINAL
    VERBATIM)
