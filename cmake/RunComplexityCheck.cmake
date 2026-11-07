# RunComplexityCheck.cmake — invoked by the `complexity-check` custom target.
#
# Required variables set by the caller (from Complexity.cmake):
#   NEXIGEN_LIZARD_EXE       — path to lizard executable (may be empty)
#   NEXIGEN_CHECK_SCRIPT     — path to check_complexity.py
#   NEXIGEN_SOURCE_DIR       — repository root
#   NEXIGEN_COMPLEXITY_THRESHOLD — integer threshold (default 15)

# CMake's generated Makefile quoting can wrap values in literal double-quotes
# (see Makefile's `\"` escaping). Strip any surrounding quotes.
string(REGEX REPLACE "^\"(.*)\"$" "\\1" NEXIGEN_CHECK_SCRIPT "${NEXIGEN_CHECK_SCRIPT}")
string(REGEX REPLACE "^\"(.*)\"$" "\\1" NEXIGEN_SOURCE_DIR "${NEXIGEN_SOURCE_DIR}")

if(NOT NEXIGEN_CHECK_SCRIPT)
    message(FATAL_ERROR "NEXIGEN_CHECK_SCRIPT not set")
endif()

if(NOT NEXIGEN_SOURCE_DIR)
    message(FATAL_ERROR "NEXIGEN_SOURCE_DIR not set")
endif()

# Locate a suitable Python interpreter (required for the complexity check).
find_program(NEXIGEN_PYTHON python3 python)
if(NOT NEXIGEN_PYTHON)
    message(WARNING "Complexity gate SKIPPED — python3 not found")
    return()
endif()

if(NOT EXISTS "${NEXIGEN_CHECK_SCRIPT}")
    message(FATAL_ERROR "check_complexity.py not found at ${NEXIGEN_CHECK_SCRIPT}")
endif()

# If lizard is explicitly known (from -D or find_program), prepend its
# directory to PATH so the Python script can find it.
if(NEXIGEN_LIZARD_EXE)
    get_filename_component(NEXIGEN_LIZARD_DIR "${NEXIGEN_LIZARD_EXE}" DIRECTORY)
    if(NEXIGEN_LIZARD_DIR)
        set(ENV{PATH} "${NEXIGEN_LIZARD_DIR}:$ENV{PATH}")
    endif()
endif()

execute_process(
    COMMAND "${NEXIGEN_PYTHON}" "${NEXIGEN_CHECK_SCRIPT}"
        --threshold "${NEXIGEN_COMPLEXITY_THRESHOLD}"
        "${NEXIGEN_SOURCE_DIR}/include"
        "${NEXIGEN_SOURCE_DIR}/src"
        "${NEXIGEN_SOURCE_DIR}/tests"
    WORKING_DIRECTORY "${NEXIGEN_SOURCE_DIR}"
    RESULT_VARIABLE result
    ECHO_ERROR_VARIABLE
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Complexity gate FAILED.")
endif()