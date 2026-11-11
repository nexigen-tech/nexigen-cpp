#!/usr/bin/env bash
# Hostile-config guard (change refactor-complexity-hotspots; spec
# core/serialization "Out-of-range unsigned value is rejected without
# terminating").
#
# Feeds a daemon one document that mixes wrong-typed, out-of-range and unknown
# fields. The shared field vocabulary must reject each value and keep the
# default, so the daemon either starts normally or rejects the configuration
# with a clean exit — it must never abort (SIGABRT / std::terminate).
set -euo pipefail

BIN="${1:?usage: hostile_config_test.sh <daemon-binary> [--config|--manifest]}"
FLAG="${2:---config}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT

CFG="$DIR/config.json"
cat >"$CFG" <<EOF
{"service_name":"Nexigen/Test/Hostile","bind_address":"127.0.0.1","port":70000,
 "endpoint":"/metrics","storage_directory":"$DIR/store",
 "max_storage_mb":999999999999999999999,"batch_flush_interval_ms":"not-a-number",
 "max_metrics":4294967296,"enable_http":false,
 "log_directory":"$DIR/logs","max_archive_files":4294967296,"max_file_size_mb":-1,
 "min_log_level_storage":42,"bg_cpu_affinity":[],"unknown_field":{"a":1},
 "processes":[],"function_groups":[]}
EOF

set +e
timeout -s KILL 3 "$BIN" "$FLAG" "$CFG" >"$DIR/out" 2>&1
status=$?
set -e

if grep -qE "terminate called|Aborted|what\(\):" "$DIR/out"; then
    echo "hostile config aborted the daemon:" >&2
    cat "$DIR/out" >&2
    exit 1
fi

case "$status" in
    0 | 1 | 2 | 124 | 143)
        echo "hostile config: ok (exit $status)"
        ;;
    *)
        echo "unexpected exit $status for a hostile config:" >&2
        cat "$DIR/out" >&2
        exit 1
        ;;
esac
