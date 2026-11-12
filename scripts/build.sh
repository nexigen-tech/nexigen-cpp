#!/usr/bin/env bash
# nexigen - build trees with a machine-appropriate -j (default 64).
#
# Usage:
#   scripts/build.sh build-asan-com build-release-com
#   scripts/build.sh --all
#   scripts/build.sh -j 32 --all
#   scripts/build.sh --list
#   scripts/build.sh --dry-run build-verify-lto
#
# Each positional argument is a build directory (relative to the repo root
# or absolute) holding a CMakeCache.txt; the generator is not assumed
# (cmake --build handles Ninja and Makefiles alike). --all discovers every
# build*/ directory with a cache.
#
# Options: -j/--jobs N (default 64, or $JOBS), --all, --list, --dry-run,
#          --fail-fast, -h/--help.
# Each build streams to the console and is logged to <dir>/build-script.log.
#
# Exit: 0 all green; 1 any build failed; 2 usage/argument error.
set -u -o pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

JOBS="${JOBS:-64}"
ALL=0
LIST=0
DRY=0
FAIL_FAST=0
DIRS=()
FAILED=()
SKIPPED=()

usage() {
    sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        -j|--jobs) JOBS="${2:?--jobs needs a value}"; shift 2 ;;
        --jobs=*) JOBS="${1#*=}"; shift ;;
        --all) ALL=1; shift ;;
        --list) LIST=1; shift ;;
        --dry-run) DRY=1; shift ;;
        --fail-fast) FAIL_FAST=1; shift ;;
        -h|--help) usage ;;
        -*) echo "unknown argument: $1 (see --help)" >&2; exit 2 ;;
        *) DIRS+=("$1"); shift ;;
    esac
done

case "$JOBS" in
    ''|*[!0-9]*) echo "-j must be a positive integer, got: $JOBS" >&2; exit 2 ;;
esac
if [ "$JOBS" -lt 1 ]; then
    echo "-j must be >= 1, got: $JOBS" >&2
    exit 2
fi

discover_all() {
    local d
    for d in "$ROOT"/build*/; do
        d="${d%/}"
        [ -f "$d/CMakeCache.txt" ] && printf '%s\n' "$d"
    done
}

cache_value() { # cache_value <cache-file> <key>
    sed -n "s/^$2:[^=]*=//p" "$1" | head -1
}

list_dirs() {
    printf '%-24s %-12s %-16s %s\n' "dir" "build-type" "generator" "ccache"
    local d bt gen cc
    for d in "$@"; do
        if [ ! -f "$d/CMakeCache.txt" ]; then
            printf '%-24s %s\n' "$(basename "$d")" "(no CMakeCache.txt)"
            continue
        fi
        bt=$(cache_value "$d/CMakeCache.txt" CMAKE_BUILD_TYPE)
        gen=$(cache_value "$d/CMakeCache.txt" CMAKE_GENERATOR)
        if grep -q "LAUNCHER = .*ccache" "$d/build.ninja" 2>/dev/null \
            || grep -q "CMAKE_CXX_COMPILER_LAUNCHER.*ccache" "$d/CMakeCache.txt" 2>/dev/null; then
            cc=yes
        else
            cc=no
        fi
        printf '%-24s %-12s %-16s %s\n' \
            "$(basename "$d")" "${bt:-<empty>}" "${gen:-<unknown>}" "$cc"
    done
}

if [ "$ALL" -eq 1 ]; then
    while IFS= read -r d; do DIRS+=("$d"); done < <(discover_all)
fi

if [ "$LIST" -eq 1 ]; then
    if [ "${#DIRS[@]}" -eq 0 ]; then
        while IFS= read -r d; do DIRS+=("$d"); done < <(discover_all)
    fi
    list_dirs "${DIRS[@]}"
    exit 0
fi

if [ "${#DIRS[@]}" -eq 0 ]; then
    echo "no build directory given; pass one or more paths, or --all" >&2
    echo "known trees:" >&2
    while IFS= read -r d; do echo "  $(basename "$d")" >&2; done < <(discover_all)
    exit 2
fi

build_one() {
    local dir="$1" name log start rc
    name="$(basename "$dir")"
    if [ ! -f "$dir/CMakeCache.txt" ]; then
        echo "▶ ${name}: skipped (no CMakeCache.txt)"
        SKIPPED+=("$name")
        return 0
    fi
    echo "▶ ${name}: cmake --build ${dir} -j ${JOBS}"
    if [ "$DRY" -eq 1 ]; then
        return 0
    fi
    log="$dir/build-script.log"
    start=$(date +%s)
    cmake --build "$dir" -j "$JOBS" 2>&1 | tee "$log"
    rc=${PIPESTATUS[0]}
    if [ "$rc" -ne 0 ]; then
        echo "✖ ${name}: rc=${rc} after $(( $(date +%s) - start ))s (tail: ${log})"
        FAILED+=("$name")
        return "$rc"
    fi
    echo "✔ ${name}: green in $(( $(date +%s) - start ))s"
    return 0
}

for dir in "${DIRS[@]}"; do
    if ! build_one "$dir"; then
        [ "$FAIL_FAST" -eq 1 ] && break
    fi
done

if command -v ccache >/dev/null 2>&1; then
    echo "▶ ccache: $(ccache -s 2>/dev/null \
        | grep -iE 'cacheable calls|hits:|misses:' \
        | tr -s ' ' | tr '\n' ' ')"
fi

if [ "${#FAILED[@]}" -ne 0 ]; then
    echo "BUILD RED: ${FAILED[*]}"
    exit 1
fi
if [ "${#SKIPPED[@]}" -ne 0 ]; then
    echo "BUILD GREEN (skipped: ${SKIPPED[*]})"
else
    echo "BUILD GREEN"
fi
