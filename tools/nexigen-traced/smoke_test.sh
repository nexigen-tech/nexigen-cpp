#!/usr/bin/env bash
# nexigen-traced lifecycle smoke test (change add-trace-daemon; spec
# tracing/trace-daemon, requirement "Lifecycle and steady-state I/O").
#
# A graceful SIGTERM must exit with code 0 and must NOT write a snapshot:
# shutdown is not an incident.
set -euo pipefail

BIN="${1:?usage: smoke_test.sh <nexigen-traced>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
mkdir -p "$DIR/dumps"

CFG="$DIR/config.json"
cat >"$CFG" <<EOF
{
  "service_name": "Nexigen/Test/TracedSmoke",
  "recovery_service_name": "Nexigen/Test/TracedSmokeRecovery",
  "recovery_event_name": "Nexigen/Test/TracedSmokeEvent",
  "drain_interval_ms": 10,
  "ring_capacity_records": 64,
  "dump_directory": "$DIR/dumps",
  "max_dump_files": 4,
  "app_name": "TRCD"
}
EOF

"$BIN" --config "$CFG" >"$DIR/out" 2>&1 &
PID=$!

# Let startup finish (node, subscribers, listener, waitset, readiness).
sleep 1
if ! kill -0 "$PID" 2>/dev/null; then
    echo "daemon exited early:" >&2
    cat "$DIR/out" >&2
    exit 1
fi

kill -TERM "$PID"
for _ in $(seq 1 50); do
    if ! kill -0 "$PID" 2>/dev/null; then
        break
    fi
    sleep 0.1
done
if kill -0 "$PID" 2>/dev/null; then
    echo "daemon did not stop on SIGTERM" >&2
    kill -9 "$PID"
    exit 1
fi

set +e
wait "$PID"
STATUS=$?
set -e
if [ "$STATUS" -ne 0 ]; then
    echo "daemon exited with $STATUS instead of 0 after SIGTERM:" >&2
    cat "$DIR/out" >&2
    exit 1
fi

if ! grep -q "nexigen-traced: listening on" "$DIR/out"; then
    echo "missing startup line:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if ! grep -q "nexigen-traced: stopped" "$DIR/out"; then
    echo "missing shutdown line:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if compgen -G "$DIR/dumps/incident_trace_*.nxtr" >/dev/null; then
    echo "graceful shutdown wrote a snapshot:" >&2
    ls "$DIR/dumps" >&2
    exit 1
fi

echo "nexigen-traced smoke: ok"
