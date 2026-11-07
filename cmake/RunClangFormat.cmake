# Runs clang-format over the project's C++ sources.
#
# Invoked by the `format` (apply) and `format-check` (verify) targets through
# `cmake -P`; it does not need a configured build tree, so CI can reuse it.
#
# Required variables:
#   CLANG_FORMAT - clang-format executable
#   SOURCE_DIR   - repository root
#   FORMAT_MODE  - "apply" or "check"

if(NOT DEFINED CLANG_FORMAT OR NOT DEFINED SOURCE_DIR OR NOT DEFINED FORMAT_MODE)
    message(FATAL_ERROR "RunClangFormat.cmake requires CLANG_FORMAT, SOURCE_DIR and FORMAT_MODE")
endif()

if(FORMAT_MODE STREQUAL "apply")
    set(NEXIGEN_FORMAT_ARGS -i)
elseif(FORMAT_MODE STREQUAL "check")
    set(NEXIGEN_FORMAT_ARGS --dry-run --Werror)
else()
    message(FATAL_ERROR "Unknown FORMAT_MODE '${FORMAT_MODE}' (expected apply or check)")
endif()

# Keep this list in sync with the directories documented in README.md. The
# OpenSpec tree is documentation, not C++, so it is deliberately absent.
set(NEXIGEN_FORMAT_STYLE "file:${SOURCE_DIR}/.clang-format")
file(GLOB_RECURSE NEXIGEN_FORMAT_SOURCES
    "${SOURCE_DIR}/include/*.hpp"
    "${SOURCE_DIR}/include/*.h"
    "${SOURCE_DIR}/src/*.cpp"
    "${SOURCE_DIR}/src/*.hpp"
    "${SOURCE_DIR}/tests/*.cpp"
    "${SOURCE_DIR}/tests/*.hpp"
    "${SOURCE_DIR}/examples/*.cpp"
    "${SOURCE_DIR}/examples/*.hpp"
    "${SOURCE_DIR}/tools/*.cpp"
    "${SOURCE_DIR}/tools/*.hpp")
list(SORT NEXIGEN_FORMAT_SOURCES)

if(NOT NEXIGEN_FORMAT_SOURCES)
    message(FATAL_ERROR "No C++ sources found under ${SOURCE_DIR}")
endif()

list(LENGTH NEXIGEN_FORMAT_SOURCES NEXIGEN_FORMAT_COUNT)
message(STATUS "clang-format ${FORMAT_MODE}: ${NEXIGEN_FORMAT_COUNT} files")

set(NEXIGEN_FORMAT_FAILURES 0)
set(NEXIGEN_FORMAT_FAILED_FILES "")
foreach(NEXIGEN_FORMAT_SOURCE IN LISTS NEXIGEN_FORMAT_SOURCES)
    execute_process(
        COMMAND "${CLANG_FORMAT}" --style=${NEXIGEN_FORMAT_STYLE} ${NEXIGEN_FORMAT_ARGS} "${NEXIGEN_FORMAT_SOURCE}"
        WORKING_DIRECTORY "${SOURCE_DIR}"
        RESULT_VARIABLE NEXIGEN_FORMAT_RESULT
        OUTPUT_VARIABLE NEXIGEN_FORMAT_STDOUT
        ERROR_VARIABLE NEXIGEN_FORMAT_STDERR)
    if(NOT NEXIGEN_FORMAT_RESULT EQUAL 0)
        math(EXPR NEXIGEN_FORMAT_FAILURES "${NEXIGEN_FORMAT_FAILURES} + 1")
        file(RELATIVE_PATH NEXIGEN_FORMAT_RELATIVE "${SOURCE_DIR}" "${NEXIGEN_FORMAT_SOURCE}")
        if(FORMAT_MODE STREQUAL "check")
            # The full --Werror diff is enormous for a first run; list the files
            # instead and let `format` show the actual changes via git diff.
            list(APPEND NEXIGEN_FORMAT_FAILED_FILES "  ${NEXIGEN_FORMAT_RELATIVE}")
        else()
            message(SEND_ERROR
                "clang-format ${FORMAT_MODE} failed for ${NEXIGEN_FORMAT_RELATIVE}\n"
                "${NEXIGEN_FORMAT_STDOUT}${NEXIGEN_FORMAT_STDERR}")
        endif()
    endif()
endforeach()

if(NEXIGEN_FORMAT_FAILURES GREATER 0)
    if(FORMAT_MODE STREQUAL "check")
        list(JOIN NEXIGEN_FORMAT_FAILED_FILES "\n" NEXIGEN_FORMAT_FILE_LIST)
        message(FATAL_ERROR
            "${NEXIGEN_FORMAT_FAILURES} file(s) are not clang-format clean:\n"
            "${NEXIGEN_FORMAT_FILE_LIST}\n"
            "Run `cmake --build <build-dir> --target format` and commit the result")
    endif()
    message(FATAL_ERROR "clang-format failed for ${NEXIGEN_FORMAT_FAILURES} file(s)")
endif()

message(STATUS "clang-format ${FORMAT_MODE}: clean")
