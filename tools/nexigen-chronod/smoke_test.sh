#!/usr/bin/env bash
# nexigen-chronod smoke test: one publication must produce a lossless history
# line, the history must append across daemon lifetimes, and a real daemon must
# publish a Ready base with UTC that an application-side client can consume.
set -euo pipefail

binary="$1"
probe="${2:-}"
exporter="${3:-}"
directory="$(mktemp -d)"
live_pid=""
cleanup() {
    if [ -n "${live_pid}" ] && kill -0 "${live_pid}" 2>/dev/null; then
        kill -KILL "${live_pid}" 2>/dev/null || true
    fi
    rm -rf "${directory}"
}
trap cleanup EXIT

"${binary}" --once \
    --segment "/nexigen-chronod-smoke-$$" \
    --history "${directory}/chrono.jsonl"

test -s "${directory}/chrono.jsonl"
grep -q '"schema":2' "${directory}/chrono.jsonl"
grep -q '"boot_uuid"' "${directory}/chrono.jsonl"
grep -q '"generation"' "${directory}/chrono.jsonl"

# A simulated register-pair domain binds, and the optional recorder session
# captures the published table (TBP2 payload) without hardware.
cat >"${directory}/settings.json" <<EOF
{
  "segment_name": "/nexigen-chronod-smoke-sim-$$",
  "history_path": "${directory}/sim.jsonl",
  "record_session_path": "${directory}/session",
  "domains": [
    {"name": "simreg", "uuid": "sim", "access": "sim",
     "mode": "register_pair", "simulate": true}
  ]
}
EOF
"${binary}" --once --settings "${directory}/settings.json"
test -s "${directory}/sim.jsonl"
if ! grep -aq 'TBP2' "${directory}"/session/chronod/*.nxrec; then
    echo "recorder session did not contain a time-base payload" >&2
    exit 1
fi

# The daemon-produced session is readable by the on-demand converter: one
# TimeBase generation plus its 1 Hz snapshot, verified end to end.
if [ -n "${exporter}" ]; then
    if ! "${exporter}" verify "${directory}/session/chronod" |
            grep -q 'clean=true'; then
        echo "exporter did not verify the chronod session" >&2
        exit 1
    fi
    if ! "${exporter}" inspect "${directory}/session/chronod" |
            grep -q 'records=2'; then
        echo "chronod session does not contain the time-base table and snapshot" >&2
        exit 1
    fi
fi

# A second daemon lifetime appends: the first history line must survive.
first_line="$(head -n 1 "${directory}/chrono.jsonl")"
first_count="$(wc -l < "${directory}/chrono.jsonl")"
"${binary}" --once \
    --segment "/nexigen-chronod-smoke-2-$$" \
    --history "${directory}/chrono.jsonl"
grep -Fqx "${first_line}" "${directory}/chrono.jsonl"
second_count="$(wc -l < "${directory}/chrono.jsonl")"
test "${second_count}" -gt "${first_count}"

# Live daemon: a separate process publishes until SIGTERM, and the probe
# attaches through the same client applications use.
if [ -n "${probe}" ]; then
    live_segment="/nexigen-chronod-live-$$"
    live_history="${directory}/live.jsonl"
    "${binary}" --segment "${live_segment}" --history "${live_history}" \
        >"${directory}/live.out" 2>&1 &
    live_pid=$!

    ready=0
    for _ in $(seq 1 50); do
        if "${probe}" "${live_segment}" 1000; then
            ready=1
            break
        fi
        sleep 0.1
    done
    if [ "${ready}" -ne 1 ]; then
        echo "live chronod did not publish a Ready base with UTC" >&2
        cat "${directory}/live.out" >&2
        exit 1
    fi

    kill -TERM "${live_pid}"
    wait "${live_pid}"
    live_pid=""
    test -s "${live_history}"
    grep -q '"boot_uuid"' "${live_history}"
fi
