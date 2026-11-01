#!/usr/bin/env bash
# nexigen-hub CLI smoke test.
set -euo pipefail
BIN="${1:?usage: hub_cli_test.sh <nexigen-hub>}"
BIN="$(realpath "$BIN")"
TEST_TMP="${NEXIGEN_TEST_TMPDIR:-/tmp/opencode}"
mkdir -p "$TEST_TMP"
ROOT="$(mktemp -d "$TEST_TMP/nexigen_hub_cli_XXXXXX")"
OUT="$(mktemp "$TEST_TMP/nexigen_hub_cli_out_XXXXXX")"
cleanup() {
    if [[ -n "${HUB_PID:-}" ]]; then kill "$HUB_PID" 2>/dev/null || true; fi
    rm -rf "$ROOT" "$OUT"
}
trap cleanup EXIT

"$BIN" --help | grep -q -- "--run-dir"
if "$BIN" >/dev/null 2>&1; then
    echo "missing --run-dir unexpectedly succeeded" >&2
    exit 1
fi

"$BIN" --run-dir "$ROOT" --listen 0.0.0.0:0 --ring-records 16 \
    >"$OUT" 2>&1 &
HUB_PID=$!
for _ in {1..50}; do
    grep -q "listening on" "$OUT" && break
    sleep 0.1
done
grep -q "warning: listening on non-loopback" "$OUT"
grep -q "listening on http://0.0.0.0:" "$OUT"
kill "$HUB_PID"
wait "$HUB_PID" 2>/dev/null || true
HUB_PID=""
echo "hub cli checks passed"
