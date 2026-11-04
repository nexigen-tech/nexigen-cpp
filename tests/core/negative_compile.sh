#!/usr/bin/env bash
# Negative-compile probe (dedup-sink-plumbing; spec core/sink-plumbing
# "Duplicate or absent member types fail at compile time").
#
# Usage: neg_compile.sh <compiler> <source.cpp> <include-dir> [extra-flags...]
#
# The probe TU is expected to FAIL to compile with the pack's
# static_assert diagnostics (type absent / duplicated, index beyond the
# pack). The script exits 0 only when the compile fails AND the error text
# is the documented static-assert class; any other outcome fails the test.
set -u
COMPILER="$1"; SRC="$2"; INCLUDE="$3"; shift 3
EXTRA_FLAGS=("$@")

TMP_OUT="$(mktemp)"; TMP_ERR="$(mktemp)"
trap 'rm -f "$TMP_OUT" "$TMP_ERR"' EXIT

# shellcheck disable=SC2086
if "$COMPILER" -std=c++23 -fno-exceptions -fno-rtti -D_GNU_SOURCE \
      -I"$INCLUDE" "${EXTRA_FLAGS[@]}" \
      -c "$SRC" -o "$TMP_OUT" 2>"$TMP_ERR"; then
    echo "neg-compile: probe compiled — it must fail (spec violated)" >&2
    exit 1
fi
if ! grep -q "static assertion failed" "$TMP_ERR"; then
    echo "neg-compile: missing 'static assertion failed' diagnostic" >&2
    cat "$TMP_ERR" >&2
    exit 1
fi
echo "neg-compile: probe failed as required ($(grep -c 'static assertion failed' "$TMP_ERR") static_assert hits)"
exit 0
