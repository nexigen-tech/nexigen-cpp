#!/usr/bin/env bash
set -u
bin="$1"
config="$2"
fixture="$3"
xvfb-run -a bash -c '"$1" --config "$2" --offline "$3" >/dev/null 2>&1 & pid=$!; sleep 2; kill -TERM "$pid"; wait "$pid"' _ "$bin" "$config" "$fixture"
