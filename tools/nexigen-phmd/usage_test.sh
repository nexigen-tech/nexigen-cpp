#!/usr/bin/env bash
# Usage-error guard: unknown or valueless options exit with status 2 and never
# abort. Keeps the phmd option parser's negative paths covered.
set -euo pipefail

binary="${1:?usage: usage_test.sh <nexigen-phmd>}"

set +e
"${binary}" --bogus >/dev/null 2>&1
unknown_status=$?
"${binary}" --manifest >/dev/null 2>&1
missing_status=$?
set -e

if [ "${unknown_status}" -ne 2 ]; then
    echo "unknown option exited ${unknown_status}, expected 2" >&2
    exit 1
fi
if [ "${missing_status}" -ne 2 ]; then
    echo "missing option value exited ${missing_status}, expected 2" >&2
    exit 1
fi
