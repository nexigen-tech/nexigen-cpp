#!/usr/bin/env bash
# nexigen-phmd safety sections (change close-phm-safety-loop).
#
# Proves the daemon accepts the documented example manifest, refuses an unsafe
# watchdog feed period, and refuses a fork region that names an unknown
# checkpoint. The watchdog stays disabled, so no hardware device is touched.
set -euo pipefail

BIN="${1:?usage: safety_sections_test.sh <nexigen-phmd>}"
SOURCE_DIR="${2:?usage: safety_sections_test.sh <nexigen-phmd> <source-dir>}"

DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

EXAMPLE_MANIFEST="$SOURCE_DIR/examples/phm/safety_manifest.json"
EXAMPLE_SHA="$(sha256sum "$EXAMPLE_MANIFEST" | cut -d' ' -f1)"

# 1. The shipped example manifest parses, validates, and starts supervision.
set +e
"$BIN" --manifest "$EXAMPLE_MANIFEST" --manifest-sha256 "$EXAMPLE_SHA" \
    --fault-log "$DIR/faults.log" >"$DIR/out" 2>&1 &
PID=$!
STARTED=0
for _ in $(seq 1 100); do
    if grep -q "supervising 3 process(es)" "$DIR/out" 2>/dev/null; then
        STARTED=1
        break
    fi
    if ! kill -0 "$PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if [ "$STARTED" -ne 1 ]; then
    wait "$PID"
    echo "the example manifest did not start supervision (status $?):" >&2
    cat "$DIR/out" >&2
    exit 1
fi
kill -TERM "$PID" 2>/dev/null
for _ in $(seq 1 50); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$PID" 2>/dev/null; then
    kill -KILL "$PID" 2>/dev/null
fi
wait "$PID"
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then
    echo "expected exit 0 for the example manifest, got $STATUS" >&2
    cat "$DIR/out" >&2
    exit 1
fi

# 2. A feed period above half the timeout is refused before any device opens.
BAD_WATCHDOG="$DIR/bad_watchdog.json"
cat >"$BAD_WATCHDOG" <<'JSON'
{
  "processes": [
    { "name": "dummy", "service_name": "Nexigen/Test/PhmdBadWatchdog", "checkpoints": [1] }
  ],
  "watchdog": { "enabled": true, "timeout_ms": 1000, "feed_period_ms": 700 }
}
JSON
set +e
timeout 10 "$BIN" --manifest "$BAD_WATCHDOG" --fault-log "$DIR/faults.log" >"$DIR/bad_out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ] || ! grep -q "invalid manifest: watchdog" "$DIR/bad_out"; then
    echo "expected the bad watchdog section to be refused:" >&2
    cat "$DIR/bad_out" >&2
    exit 1
fi

# 3. A fork that names an unknown checkpoint is refused.
BAD_FORK="$DIR/bad_fork.json"
cat >"$BAD_FORK" <<'JSON'
{
  "processes": [
    {
      "name": "dummy",
      "service_name": "Nexigen/Test/PhmdBadFork",
      "checkpoints": [1, 2],
      "logical_forks": [ { "fork_id": 1, "branch_entries": [9], "join_id": 2 } ]
    }
  ]
}
JSON
set +e
timeout 10 "$BIN" --manifest "$BAD_FORK" --fault-log "$DIR/faults.log" >"$DIR/fork_out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ] || ! grep -q "invalid manifest: process 'dummy'" "$DIR/fork_out"; then
    echo "expected the bad fork region to be refused:" >&2
    cat "$DIR/fork_out" >&2
    exit 1
fi

# 4. A mismatched manifest digest refuses to start.
set +e
timeout 10 "$BIN" --manifest "$EXAMPLE_MANIFEST" --manifest-sha256 \
    "$(printf '0%.0s' $(seq 1 64))" --fault-log "$DIR/faults.log" >"$DIR/digest_out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 1 ] || ! grep -q "manifest digest verification failed" "$DIR/digest_out"; then
    echo "expected a manifest digest mismatch to refuse startup:" >&2
    cat "$DIR/digest_out" >&2
    exit 1
fi

# 5. Fault-log records carry a checksum and verify after the fact.
FAST="$DIR/fast.json"
cat >"$FAST" <<'JSON'
{
  "processes": [
    {
      "name": "fast",
      "service_name": "Nexigen/Test/PhmdFastFault",
      "alive_reference_cycle_ms": 100,
      "alive_min_margin_ms": 0,
      "alive_max_margin_ms": 0,
      "failed_supervision_cycles_tolerance": 0,
      "checkpoints": [1]
    }
  ]
}
JSON
LOG="$DIR/fast-faults.log"
set +e
"$BIN" --manifest "$FAST" --fault-log "$LOG" >"$DIR/fast_out" 2>&1 &
PID=$!
for _ in $(seq 1 100); do
    if grep -q "supervising 3 process(es)" "$DIR/fast_out" 2>/dev/null; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done
sleep 0.8
kill -TERM "$PID" 2>/dev/null
for _ in $(seq 1 50); do
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done
kill -KILL "$PID" 2>/dev/null
wait "$PID"
set -e
if [ ! -s "$LOG" ]; then
    echo "the fast manifest wrote no fault records:" >&2
    cat "$DIR/fast_out" >&2
    exit 1
fi
set +e
"$BIN" --verify-fault-log "$LOG" >"$DIR/verify_out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ] || ! grep -q "corrupted=0" "$DIR/verify_out"; then
    echo "expected a clean fault log to verify:" >&2
    cat "$DIR/verify_out" >&2
    exit 1
fi
sed -i '1s/0/9/' "$LOG"
set +e
"$BIN" --verify-fault-log "$LOG" >"$DIR/verify_bad_out" 2>&1
STATUS=$?
set -e
if [ "$STATUS" -eq 0 ]; then
    echo "expected a corrupted fault log to fail verification:" >&2
    cat "$DIR/verify_bad_out" >&2
    exit 1
fi

echo "nexigen-phmd safety sections: ok"
