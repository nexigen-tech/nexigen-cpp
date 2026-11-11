#!/usr/bin/env bash
# nexigen-metricsd lifecycle smoke test (change add-metrics-daemon):
# starts the daemon, verifies it creates its history file, then checks that a
# SIGTERM stops it cleanly (exit 0).
set -euo pipefail

BIN="${1:?usage: smoke_test.sh <nexigen-metricsd>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

CFG="$DIR/config.json"
cat > "$CFG" <<EOF
{"service_name":"Nexigen/Test/MetricsdSmoke","bind_address":"127.0.0.1","port":0,
 "endpoint":"/metrics","storage_directory":"$DIR/store","max_storage_mb":1,
 "batch_flush_interval_ms":100,"max_metrics":16,"enable_http":false}
EOF

"$BIN" --config "$CFG" >"$DIR/log" 2>&1 &
PID=$!

# Let startup finish (bind/subscriber/history open).
sleep 1
if ! kill -0 "$PID" 2>/dev/null; then
    echo "daemon exited early:" >&2
    cat "$DIR/log" >&2
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

wait "$PID"
test -f "$DIR/store/metrics_history.jsonl"
echo "metricsd smoke: ok"
