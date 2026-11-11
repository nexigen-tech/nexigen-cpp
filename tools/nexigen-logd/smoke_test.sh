#!/usr/bin/env bash
# nexigen-logd lifecycle smoke test (change fix-logd-sigterm-shutdown; spec
# logging/log-daemon, requirement "Daemon writes to disk with rotation",
# scenario "Flush empties the buffer on shutdown").
#
# SIGTERM must run the daemon's graceful path: the final flush, the `stopped`
# line and exit code 0. Before the fix the process died with the default signal
# disposition (143) and never flushed.
set -euo pipefail

BIN="${1:?usage: smoke_test.sh <nexigen-logd>}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
mkdir -p "$DIR/logs"

CFG="$DIR/config.json"
cat >"$CFG" <<EOF
{"service_name":"Nexigen/Test/LogdSmoke","log_directory":"$DIR/logs","batch_flush_interval_ms":50}
EOF

"$BIN" --config "$CFG" >"$DIR/out" 2>&1 &
PID=$!

# Let startup finish (node, subscriber, listener, waitset, first file open).
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

if ! grep -q "nexigen-logd: listening on" "$DIR/out"; then
    echo "missing startup line:" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if ! grep -q "nexigen-logd: stopped" "$DIR/out"; then
    echo "graceful shutdown did not run (no 'stopped' line):" >&2
    cat "$DIR/out" >&2
    exit 1
fi
if [ -z "$(find "$DIR/logs" -mindepth 1 -maxdepth 1 -type d -print -quit)" ]; then
    echo "daemon did not open a log period directory under $DIR/logs" >&2
    exit 1
fi

echo "logd smoke: graceful SIGTERM shutdown, exit 0, log period opened"
