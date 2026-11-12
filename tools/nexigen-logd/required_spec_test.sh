#!/usr/bin/env bash
# nexigen-logd required main-thread spec failure (change
# apply-thread-specs-platform-wide; spec logging/log-daemon "Required failure
# stops startup"). An impossible CPU with Required mode must exit non-zero
# before the subscriber is created.
set -euo pipefail

BIN="${1:?usage: required_spec_test.sh <nexigen-logd>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
mkdir -p "$DIR/logs"

CFG="$DIR/config.json"
cat >"$CFG" <<JSON
{
  "service_name": "Nexigen/Test/LogdRequired",
  "log_directory": "$DIR/logs",
  "main_thread": { "cpus": [999], "mode": "required" },
  "batch_flush_interval_ms": 50
}
JSON

set +e
timeout 5 "$BIN" --config "$CFG" >"$DIR/out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ]; then
    echo "expected exit 1 for a required main-thread failure, got $STATUS" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if ! grep -q "main thread spec failed" "$DIR/out"; then
    echo "missing main-thread failure diagnostic:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if compgen -G "$DIR/logs/*.jsonl" >/dev/null; then
    echo "subscriber/log file was created before the spec failure" >&2
    ls "$DIR/logs" >&2
    exit 1
fi
echo "nexigen-logd required-spec: ok"
