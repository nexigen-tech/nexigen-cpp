#!/usr/bin/env bash
# nexigen-sm required audit-thread spec failure (change
# apply-thread-specs-platform-wide). An impossible CPU with Required mode must
# exit non-zero instead of silently running without the audit log.
set -euo pipefail

BIN="${1:?usage: required_spec_test.sh <nexigen-sm>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

CFG="$DIR/manifest.json"
cat >"$CFG" <<JSON
{
  "function_groups": [
    {
      "name": "FG",
      "states": ["Startup", "Running"],
      "allowed_transitions": [{ "from": "Startup", "to": "Running" }],
      "transition_max_time_ms": 500,
      "preparation_timeout_ms": 100
    }
  ],
  "audit_thread": { "cpus": [999], "mode": "required" }
}
JSON

set +e
timeout 10 "$BIN" --config "$CFG" --audit-log "$DIR/audit.log" >"$DIR/out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ]; then
    echo "expected exit 1 for a required audit-thread failure, got $STATUS" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if ! grep -q "audit thread not started" "$DIR/out"; then
    echo "missing audit-thread failure diagnostic:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
echo "nexigen-sm required-spec: ok"
