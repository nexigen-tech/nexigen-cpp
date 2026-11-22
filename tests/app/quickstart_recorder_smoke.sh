#!/usr/bin/env bash
# End-to-end quickstart recording smoke:
#   app -> iceoryx2 -> nexigen-recd -> session -> nexigen-export list.
set -euo pipefail
app_bin="$1"
sender_bin="$2"
recd_bin="$3"
export_bin="$4"
root="$(mktemp -d /tmp/opencode/quickstart-recorder.XXXXXX)"
cleanup() {
    [[ "${app_pid:-0}" -gt 0 ]] && kill "${app_pid}" 2>/dev/null || true
    [[ "${rec_pid:-0}" -gt 0 ]] && kill "${rec_pid}" 2>/dev/null || true
    rm -rf "${root}"
}
trap cleanup EXIT
app_pid=0
rec_pid=0

"${app_bin}" --app-id QSTRT >"${root}/app.out" 2>&1 &
app_pid=$!
sleep 1
"${recd_bin}" --session-root "${root}/session" \
    --service-metrics nexigen/metrics/stream \
    --service-traces nexigen/trace/stream \
    --event-service Nexigen/Example/Quickstart/In \
    --event-service Nexigen/Example/Quickstart/Out >"${root}/recd.out" 2>&1 &
rec_pid=$!
sleep 1
"${sender_bin}"
sleep 2
kill -TERM "${app_pid}" 2>/dev/null || true
wait "${app_pid}" 2>/dev/null || true
app_pid=0
sleep 1
kill -TERM "${rec_pid}" 2>/dev/null || true
wait "${rec_pid}" 2>/dev/null || true
rec_pid=0

session="$(find "${root}/session" -name session.json -printf '%h\n' | head -1)"
if [[ -z "${session}" ]]; then
    echo "quickstart recorder: no session written" >&2
    exit 1
fi
listing="$("${export_bin}" list "${session}")"
for topic in log.QSTRT metric.QSTRT trace.QSTRT event.QSND event.QSTRT; do
    if ! grep -q "name=${topic}" <<<"${listing}"; then
        echo "quickstart recorder: missing ${topic}" >&2
        echo "${listing}" >&2
        exit 1
    fi
done
if grep -q 'records=0' <<<"${listing}"; then
    echo "quickstart recorder: a channel is empty" >&2
    echo "${listing}" >&2
    exit 1
fi
echo "quickstart recorder: log/metric/trace/event channels recorded under QSTRT/QSND"
