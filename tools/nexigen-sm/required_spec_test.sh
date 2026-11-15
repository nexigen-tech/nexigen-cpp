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
      "states": ["Off", "Startup", "Running"],
      "allowed_transitions": [
        { "from": "Off", "to": "Startup" },
        { "from": "Startup", "to": "Running" }
      ],
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
# A matching digest passes the gate; a mismatch refuses startup before any
# thread is configured.
GOOD_SHA="$(sha256sum "$CFG" | cut -d' ' -f1)"
set +e
timeout 10 "$BIN" --config "$CFG" --config-sha256 "$GOOD_SHA" \
    --audit-log "$DIR/audit.log" >"$DIR/digest_ok" 2>&1
STATUS=$?
set -e
if ! grep -q "audit thread not started" "$DIR/digest_ok"; then
    echo "a matching config digest should pass the loader:" >&2
    cat "$DIR/digest_ok" >&2
    exit 1
fi
set +e
timeout 10 "$BIN" --config "$CFG" --config-sha256 "$(printf '0%.0s' $(seq 1 64))" \
    --audit-log "$DIR/audit.log" >"$DIR/digest_bad" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ] || ! grep -q "config digest verification failed" "$DIR/digest_bad"; then
    echo "expected a config digest mismatch to refuse startup:" >&2
    cat "$DIR/digest_bad" >&2
    exit 1
fi

echo "nexigen-sm required-spec: ok"
