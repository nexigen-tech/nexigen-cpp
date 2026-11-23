#!/usr/bin/env bash
# Heartbeat smoke: an idle virtual bus produces growing BusIdle durations.
set -euo pipefail
vbus_bin="$1"
rx_bin="$2"
mon_bin="$3"
root="$(mktemp -d /tmp/opencode/can-heartbeat.XXXXXX)"
pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT

"$vbus_bin" --app-id VBUS --channel can0=can0 --embedded >"$root/vbus.out" 2>&1 & pids+=($!)
sleep 0.5
"$rx_bin" --app-id CANRX --channel can0=can0 --backend virtual --embedded >"$root/rx.out" 2>&1 & pids+=($!)
sleep 0.5
"$mon_bin" --app-id CANMON --channel can0=can0 --embedded --json >"$root/mon.out" 2>&1 & pids+=($!)
sleep 7.0
for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
python3 - "$root/mon.out" <<'PY'
import json, sys
values = []
for line in open(sys.argv[1], encoding="utf-8", errors="ignore"):
    try:
        record = json.loads(line)
    except json.JSONDecodeError:
        continue
    if record.get("kind") == "BusIdle":
        values.append(int(record.get("idle_duration_ns", 0)))
if len(values) < 2:
    print(f"heartbeat smoke: expected >=2 BusIdle, got {len(values)}", file=sys.stderr)
    sys.exit(1)
if any(b <= a for a, b in zip(values, values[1:])):
    print(f"heartbeat smoke: idle durations not growing: {values}", file=sys.stderr)
    sys.exit(1)
print(f"heartbeat smoke: {len(values)} BusIdle with growing durations")
PY
