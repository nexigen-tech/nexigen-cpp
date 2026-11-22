#!/usr/bin/env bash
# Checks the two quickstart invariants that are not covered by the run smoke:
# a missing --app-id exits non-zero with the identity diagnostic, and the
# `main` body stays within the 30-line budget.
set -euo pipefail
app_bin="$1"
source_file="$2"

if output="$("${app_bin}" 2>&1)"; then
    echo "quickstart contract: run without --app-id unexpectedly succeeded" >&2
    exit 1
fi
if ! grep -q -- "--app-id" <<<"${output}"; then
    echo "quickstart contract: missing --app-id diagnostic" >&2
    echo "${output}" >&2
    exit 1
fi

lines="$(awk '
    /int main\(/ { in_main = 1 }
    in_main {
        line = $0
        sub(/^[[:space:]]+/, "", line)
        if (line == "" || line ~ /^\/\//) next
        count++
    }
    END { print count + 0 }
' "${source_file}")"

if (( lines > 30 )); then
    echo "quickstart contract: main body is ${lines} lines (limit 30)" >&2
    exit 1
fi

echo "quickstart contract: missing id rejected; main body is ${lines} lines"
