#!/usr/bin/env bash
# nexigen-recd end-to-end smoke: frames, gaps, events, flags, rotation, retention.
set -u -o pipefail
recd="$1"
producer="$2"
exporter="$3"
root=""
root2=""
root3=""
root4=""
root5=""
root6=""
cleanup() { rm -rf "$root" "$root2" "$root3" "$root4" "$root5" "$root6"; }
trap cleanup EXIT

# 1. Frames + gap + two application codes.
root="$(mktemp -d)"
"$recd" --session-root "$root" >/dev/null 2>&1 &
pid=$!
sleep 0.5
"$producer" || exit 1
sleep 0.5
kill -TERM "$pid" 2>/dev/null || true
wait "$pid" 2>/dev/null || true
session="$(find "$root" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session" ] || exit 1
grep -Eq '"boot_token": "[0-9a-f]{16}"' "$session/session.json" || exit 1
"$exporter" verify "$session" | grep -q "clean=true" || exit 1
"$exporter" list "$session" | grep -q "log.APP2" || exit 1
"$exporter" list "$session" | grep -q "gaps=[1-9]" || exit 1

# 2. --no-traces omits the trace dataset.
root2="$(mktemp -d)"
"$recd" --session-root "$root2" --no-traces >/dev/null 2>&1 &
pid2=$!
sleep 0.5
"$producer" || exit 1
sleep 0.5
kill -TERM "$pid2" 2>/dev/null || true
wait "$pid2" 2>/dev/null || true
session2="$(find "$root2" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session2" ] || exit 1
"$exporter" list "$session2" | grep -q "schema=nexigen.log.LogFrame" || exit 1
if "$exporter" list "$session2" | grep -q "schema=nexigen.tracing.SpanFrame"; then
    exit 1
fi

# 3. Event stream recorded with a service-named channel.
root3="$(mktemp -d)"
"$producer" --hold >/dev/null 2>&1 &
holder=$!
sleep 0.4
"$recd" --session-root "$root3" --event-service Nexigen/Test/RecdEvent \
    --no-logs --no-metrics --no-traces >/dev/null 2>&1 &
pid3=$!
wait "$holder" || exit 1
sleep 0.3
kill -TERM "$pid3" 2>/dev/null || true
wait "$pid3" 2>/dev/null || true
session3="$(find "$root3" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session3" ] || exit 1
"$exporter" list "$session3" | grep -q "event.APP3" || exit 1

# 4. Missing event service keeps the collector alive.
root4="$(mktemp -d)"
"$recd" --session-root "$root4" --event-service Nexigen/Test/NoSuchEvent \
    --no-logs --no-metrics --no-traces >/dev/null 2>&1 &
pid4=$!
sleep 0.4
kill -TERM "$pid4" 2>/dev/null || true
wait "$pid4" 2>/dev/null || true
session4="$(find "$root4" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session4" ] || exit 1
"$exporter" verify "$session4" | grep -q "clean=true" || exit 1

# 5. Forced size rotation produces multiple segments.
root5="$(mktemp -d)"
"$recd" --session-root "$root5" --max-bytes 1 >/dev/null 2>&1 &
pid5=$!
sleep 0.4
"$producer" || exit 1
sleep 0.5
kill -TERM "$pid5" 2>/dev/null || true
wait "$pid5" 2>/dev/null || true
session5="$(find "$root5" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session5" ] || exit 1
"$exporter" list "$session5" | grep -Eq "segments=[2-9]" || exit 1

# 6. Retention deletes closed segments and still closes cleanly.
root6="$(mktemp -d)"
"$recd" --session-root "$root6" --max-bytes 1 --retention-bytes 1 >/dev/null 2>&1 &
pid6=$!
sleep 0.4
"$producer" || exit 1
sleep 0.5
kill -TERM "$pid6" 2>/dev/null || true
wait "$pid6" 2>/dev/null || true
session6="$(find "$root6" -mindepth 1 -maxdepth 1 -type d | head -1)"
[ -n "$session6" ] || exit 1
"$exporter" verify "$session6" | grep -q "clean=true" || exit 1
[ "$(find "$session6" -maxdepth 1 -name '*.nxrec' | wc -l)" -eq 1 ] || exit 1
exit 0
