# Cyclomatic complexity gate — lizard integration.
#
# Adds the custom target:
#   complexity-check — run lizard on include/ src/ tests/ and fail if any
#                      C++ function exceeds the project threshold (CCN 15)
#                      after filtering out alignas false positives.
#
# Requirements: lizard (pip package), installable at configure or CI time.
# On first configure without lizard the target is a no-op that prints a hint.

# Attempt to locate lizard
find_program(NEXIGEN_LIZARD
    NAMES lizard lizard.py
    DOC "lizard (pip: lizard) — cyclomatic complexity analyser")

# Locate the companion Python script (sibling of this file)
get_filename_component(NEXIGEN_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ABSOLUTE)
set(NEXIGEN_CHECK_COMPLEXITY "${NEXIGEN_CMAKE_DIR}/check_complexity.py")

add_custom_target(complexity-check
    COMMAND "${CMAKE_COMMAND}"
        -D NEXIGEN_LIZARD_EXE="${NEXIGEN_LIZARD}"
        -D NEXIGEN_CHECK_SCRIPT="${NEXIGEN_CHECK_COMPLEXITY}"
        -D NEXIGEN_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        -D NEXIGEN_COMPLEXITY_THRESHOLD=15
        -P "${NEXIGEN_CMAKE_DIR}/RunComplexityCheck.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Checking cyclomatic complexity (CCN ≤ 15, exclude examples/, filter alignas)" VERBATIM)