#!/usr/bin/env bash
# nexigen-recd end-to-end smoke: record one log/metric/trace frame, close, verify.
set -u -o pipefail
recd="$1"
producer="$2"
exporter="$3"
root="$(mktemp -d)"
cleanup() { rm -rf "$root" "${root2:-}" "${root3:-}" "${root4:-}" "${root5:-}" "${root6:-}"; }
trap cleanup EXIT

"$recd" --session-root "$root" >/dev/null 2>&1 &
pid=$!
sleep 0.5
"$producer" || exit 1
sleep 0.5
kill -TERM "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true

session="$(find "$root" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session" ] || { echo "no session created" >&2; exit 1; }
grep -Eq '"boot_token": "[0-9a-f]{16}"' "$session/session.json" || exit 1
"$exporter" verify "$session" | grep -q "clean=true" || exit 1
"$exporter" list "$session" | grep -q "kind=Telemetry" || exit 1
exit 0
