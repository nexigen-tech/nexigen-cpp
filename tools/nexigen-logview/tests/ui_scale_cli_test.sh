#!/usr/bin/env bash
# CLI contract for --ui-scale: help text and strict invalid-value rejection.
set -euo pipefail
binary="$1"
"${binary}" --help | grep -q -- '--ui-scale'
for value in 0.4 5 nan abc; do
    if "${binary}" --ui-scale "${value}" >/dev/null 2>&1; then
        echo "UI_SCALE accepted invalid value: ${value}" >&2
        exit 1
    fi
done
if "${binary}" --ui-scale >/dev/null 2>&1; then
    echo "UI_SCALE accepted a missing value" >&2
    exit 1
fi
