#!/usr/bin/env bash
# Install nexigen into a scratch prefix and build the standalone consumer
# against the installed CMake package (no in-tree targets, no add_subdirectory).
set -euo pipefail

BUILD_DIR="${1:?usage: smoke_test.sh <build-dir> [prefix]}"
PREFIX="${2:-}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ -z "$PREFIX" ]]; then
    TEST_TMP="${NEXIGEN_TEST_TMPDIR:-/tmp/opencode}"
    mkdir -p "$TEST_TMP"
    PREFIX="$(mktemp -d "$TEST_TMP/nexigen_install_XXXXXX")"
    CLEANUP=1
else
    CLEANUP=0
fi

cleanup() {
    if [[ "$CLEANUP" == "1" ]]; then
        rm -rf "$PREFIX"
    fi
}
trap cleanup EXIT

cmake --install "$BUILD_DIR" --prefix "$PREFIX"
cmake -S "$SCRIPT_DIR/consumer" -B "$PREFIX/consumer-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$PREFIX"
cmake --build "$PREFIX/consumer-build"
"$PREFIX/consumer-build/nexigen_consumer"
