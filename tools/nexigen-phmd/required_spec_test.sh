#!/usr/bin/env bash
# nexigen-phmd required storage-thread spec failure (change
# apply-thread-specs-platform-wide). An impossible CPU with Required mode must
# exit non-zero instead of silently running without fault storage.
set -euo pipefail

BIN="${1:?usage: required_spec_test.sh <nexigen-phmd>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

CFG="$DIR/manifest.json"
cat >"$CFG" <<JSON
{
  "processes": [
    {
      "name": "dummy",
      "service_name": "Nexigen/Test/PhmdRequiredSpec",
      "alive_reference_cycle_ms": 100,
      "alive_min_margin_ms": 1,
      "alive_max_margin_ms": 50,
      "failed_supervision_cycles_tolerance": 1,
      "checkpoints": [1]
    }
  ],
  "fault_storage_thread": { "cpus": [999], "mode": "required" }
}
JSON

set +e
timeout 10 "$BIN" --manifest "$CFG" --fault-log "$DIR/faults.log" >"$DIR/out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ]; then
    echo "expected exit 1 for a required storage-thread failure, got $STATUS" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if ! grep -q "fault storage thread not started" "$DIR/out"; then
    echo "missing storage-thread failure diagnostic:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
echo "nexigen-phmd required-spec: ok"
