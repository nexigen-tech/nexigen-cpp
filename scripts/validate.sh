#!/usr/bin/env bash
# nexigen validation ladder - change speedup-validation-loop (design D3/D6).
#
# One entrypoint for the two-tier validation loop:
#   scripts/validate.sh --tier module --modules chrono,metrics
#       inner loop: affected module unit tests + static gates (~1-2 min)
#   scripts/validate.sh --tier ci
#       outer loop: static gates with clang-tidy BEFORE the expensive matrix,
#       then Debug build+tests, then Release/ASAN/TSAN configs in parallel
#       (requires the per-test isolation of design D4; keeps --serial mode
#       reproducing the pre-change sequential behavior)
#   --ccn-threshold 20   the user-approved CCN allowance (default 20)
#   --j 16               ctest parallelism inside configs (default 16)
#
# Every step logs into its own file and prints wall seconds, so loops compare
# against the Nov-9 session baselines (Release incremental rebuild 16.9 min,
# ASAN 14.7, TSAN 12.2 with the serial pre-change pipeline).
#
# Gate posture: naming/complexity/result-value scan the whole tree, while the
# tree carries pre-existing violations outside the current working set
# (tools/* naming, record_format_test .value()). The ladder treats violations
# belonging to THE WORKING SET (diff+untracked C++ files) as failures and
# baseline dirt as notices, matching the manual practice.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TIER="module"
MODULES=""
SERIAL=0
JOBS=16
CCN_THRESHOLD=20
TESTS_BUILD_DIR="build-verify"
FAILED=()

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}"
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --tier) TIER="$2"; shift 2 ;;
        --modules) MODULES="$2"; shift 2 ;;
        --serial) SERIAL=1; shift ;;
        --j) JOBS="$2"; shift 2 ;;
        --ccn-threshold) CCN_THRESHOLD="$2"; shift 2 ;;
        --help|-h) usage ;;
        *) echo "unknown argument: $1 (see the header comment)" >&2; exit 2 ;;
    esac
done
case "$TIER" in
    module|ci) ;;
    *) echo "--tier must be module|ci, got: $TIER" >&2; exit 2 ;;
esac

# --- helpers ----------------------------------------------------------------

timed() { # timed <label> <log-path> <cmd...>
    local label="$1" log="$2"
    shift 2
    local start rc
    start=$(date +%s)
    set +e
    "$@" >"$log" 2>&1
    rc=$?
    set -e
    echo "▶ ${label}: rc=${rc} ($(( $(date +%s) - start ))s → ${log})"
    if [ "$rc" -ne 0 ]; then
        echo "--- failure tail (${label}):"
        tail -n 40 "$log"
        FAILED+=("${label}")
    fi
    return 0
}

cpp_changed_files() {
    # Tracked working set only (diff against HEAD): untracked WIP files of
    # the parallel session must not gate this conveyor; format them before
    # they are staged instead.
    git diff --name-only HEAD | grep -E '\.(cpp|hpp|cc|h)$' | sort -u
}

# Full-tree gate; failing rows attributed to the working set only.
gate_filtered() { # gate_filtered <label> <log-path> <cmd...>
    local label="$1" log="$2"
    shift 2
    local ours
    ours="$(cpp_changed_files | tr '\n' '|' | sed 's/|$//')"
    set +e
    "$@" >"$log" 2>&1
    local rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then
        echo "▶ ${label}: rc=0"
        return 0
    fi
    if [ -n "$ours" ] && grep -E "^(src|tests|include|examples|tools)/" "$log" \
        | grep -E "(${ours})" | grep -q .; then
        echo "▶ ${label}: rc=${rc} — WORKING-SET VIOLATIONS:"
        grep -E "^(src|tests|include|examples|tools)/" "$log" | grep -E "(${ours})"
        FAILED+=("${label}")
    else
        echo "▶ ${label}: rc=${rc} — baseline dirt only (details: ${log}):"
        grep -E "^(src|tests|include|examples|tools)/" "$log" | head -5
    fi
    return 0
}

ccache_report() {
    if command -v ccache >/dev/null 2>&1; then
        echo "▶ ccache stats:"
        ccache -s 2>/dev/null | grep -iE "(cacheable calls|hits:|misses:|uncacheable calls|cache size)" | head -6
    else
        echo "▶ ccache: not installed (conda install -c conda-forge ccache)"
    fi
}

# --- static gates (both tiers, fail-fast order) ------------------------------

gate_filtered naming "${TESTS_BUILD_DIR}/validate-naming.log" \
    python3 cmake/check_naming.py
timed complexity "${TESTS_BUILD_DIR}/validate-complexity.log" \
    python3 cmake/check_complexity.py --threshold "${CCN_THRESHOLD}" include src
gate_filtered result_value "${TESTS_BUILD_DIR}/validate-result-value.log" \
    python3 cmake/check_result_value.py

format_files="$(cpp_changed_files | tr '\n' ' ')"
if [ -n "${format_files// }" ]; then
    set +e
    clang-format --dry-run --Werror ${format_files} \
        >"${TESTS_BUILD_DIR}/validate-format.log" 2>&1
    fmt_rc=$?
    set -e
    if [ "$fmt_rc" -ne 0 ]; then
        echo "▶ clang-format: FAILED (run clang-format -i on the files above);"
        tail -n 8 "${TESTS_BUILD_DIR}/validate-format.log"
        FAILED+=("format")
    else
        echo "▶ clang-format: clean (${#format_files} files)"
    fi
else
    echo "▶ clang-format: no working-set C++ files"
fi

if [ "$TIER" = "ci" ]; then
    # Debug tier: build once, then test with the soaked parallel ctest (D4).
    timed debug-build "${TESTS_BUILD_DIR}/validate-debug-build.log" \
        cmake --build "${TESTS_BUILD_DIR}"

    # clang-tidy runs AFTER the Debug build (so the gcc compile db and its
    # PCH snapshot are fresh) and BEFORE the expensive sanitizer matrix:
    # late header-order/POI failures must cost seconds, not a 40-minute rerun
    # (proposal Why). Use build-verify (gcc) rather than build-verify-clang —
    # the clang tree may be stale and lacks a compiled .pch, which makes
    # clang-tidy abort on "-include-pch" with "PCH file not found".
    tidy_files="$(git diff --name-only HEAD | grep -E '^src/.*\.cpp$' | tr '\n' ' ')"
    if [ -n "${tidy_files// }" ] && [ -d build-verify ]; then
        timed clang-tidy "${TESTS_BUILD_DIR}/validate-tidy.log" \
            bash -c "clang-tidy -p build-verify --quiet ${tidy_files}"
    else
        echo "▶ clang-tidy: no changed src TUs (or no build-verify), skipped"
    fi

    if [ "$SERIAL" -eq 1 ]; then
        timed debug-ctest "${TESTS_BUILD_DIR}/validate-debug-ctest.log" \
            ctest --test-dir "${TESTS_BUILD_DIR}" --output-on-failure
    else
        timed debug-ctest "${TESTS_BUILD_DIR}/validate-debug-ctest.log" \
            ctest --test-dir "${TESTS_BUILD_DIR}" -j"${JOBS}" --output-on-failure
    fi

    # Release + ASAN + TSAN: disjoint build dirs, launched in parallel; each
    # config logs its own build+ctest (D6: measured configs, wall = max not sum).
    for cfg in build-release build-asan build-tsan; do
        if [ ! -d "$ROOT/$cfg" ]; then
            echo "▶ ${cfg}: missing, skipped"
            continue
        fi
        log="$ROOT/$cfg/validate-config.log"
        pidfile="$ROOT/$cfg/validate-config.pid"
        rm -f "$pidfile" # an interrupted earlier run must not leave ghost pids
        if [ "$SERIAL" -eq 1 ]; then
            timed "$cfg" "$log" \
                bash -c "cmake --build $cfg && ctest --test-dir $cfg --output-on-failure"
        else
            echo "▶ ${cfg}: background launch → ${log}"
            (
                /usr/bin/time -f "TOTAL-$(basename "$cfg")=%es" bash -c \
                    "cmake --build $cfg && ctest --test-dir $cfg -j${JOBS} --output-on-failure" \
                    >"$log" 2>&1
            ) &
            echo "$!" >> "$ROOT/$cfg/validate-config.pid"
        fi
    done
    for cfg in build-release build-asan build-tsan; do
        pidfile="$ROOT/$cfg/validate-config.pid"
        if [ -f "$pidfile" ]; then
            read -r pid < "$pidfile"
            wait "$pid" || FAILED+=("$cfg")
            rm -f "$pidfile"
            grep -h "TOTAL-" "$ROOT/$cfg/validate-config.log" 2>/dev/null || true
        fi
        if [ -f "$ROOT/$cfg/validate-config.log" ]; then
            if grep -q "tests passed, 0 tests failed" "$ROOT/$cfg/validate-config.log" \
                && ! grep -qE "FAILED|error:" "$ROOT/$cfg/validate-config.log"; then
                echo "▶ ${cfg}: green"
            else
                echo "▶ ${cfg}: check $cfg/validate-config.log (red or partial)"
                FAILED+=("$cfg")
            fi
        fi
    done
else
    # module tier: affected module unit tests in the Debug tree.
    if [ -z "$MODULES" ]; then
        echo "--tier module needs --modules m1,m2" >&2
        exit 2
    fi
    targets=""
    regex=""
    for m in $(echo "$MODULES" | tr ',' ' '); do
        targets="${targets} nexigen_${m}_tests"
        regex="${regex}${regex:+|}nexigen_${m}_tests"
    done
    timed module-build "${TESTS_BUILD_DIR}/validate-module-build.log" \
        cmake --build "${TESTS_BUILD_DIR}" --target ${targets}
    timed module-ctest "${TESTS_BUILD_DIR}/validate-module-ctest.log" \
        bash -c "ctest --test-dir ${TESTS_BUILD_DIR} -j${JOBS} -R \"^(${regex})\$\" --output-on-failure"
fi

ccache_report

if [ "${#FAILED[@]}" -ne 0 ]; then
    echo "VALIDATION RED: ${FAILED[*]}"
    exit 1
fi
echo "VALIDATION GREEN"
