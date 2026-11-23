#!/usr/bin/env bash
# Virtual-hardware end-to-end: control sender -> cantx -> canvbus -> canrx -> canmon.
set -euo pipefail
vbus_bin="$1"
rx_bin="$2"
tx_bin="$3"
mon_bin="$4"
sender_bin="$5"
root="$(mktemp -d /tmp/opencode/can-e2e.XXXXXX)"
pids=()
cleanup() {
    for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
    rm -rf "$root"
}
trap cleanup EXIT

args=(--app-id VBUS --channel can0=can0 --channel can1=can1 --embedded --seed 42 --inject-errors --inject-overload --inject-warning --inject-passive --inject-bus-off --inject-duplicate --inject-reorder --inject-burst --external)
"$vbus_bin" "${args[@]}" >"$root/vbus.out" 2>&1 & pids+=($!)
sleep 0.5
"$rx_bin" --app-id CANRX --channel can0=can0 --channel can1=can1 --backend virtual --embedded >"$root/rx.out" 2>&1 & pids+=($!)
sleep 0.5
"$tx_bin" --app-id CANTX --channel can0=can0 --channel can1=can1 --backend virtual --embedded >"$root/tx.out" 2>&1 & pids+=($!)
"$mon_bin" --app-id CANMON --channel can0=can0 --channel can1=can1 --embedded --json >"$root/mon.out" 2>&1 & pids+=($!)
sleep 1.5
"$sender_bin" --app-id CANSND --channel can0=can0 --channel can1=can1 --embedded >"$root/sender.out" 2>&1
sleep 3
for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
if ! grep -q '"kind":"ClassicData"' "$root/mon.out"; then
    echo "can e2e: monitor did not receive a data frame" >&2
    cat "$root/mon.out" >&2
    exit 1
fi
if ! grep -q '"kind":"BusIdle"' "$root/mon.out"; then
    echo "can e2e: monitor did not receive a heartbeat" >&2
    cat "$root/mon.out" >&2
    exit 1
fi
echo "can e2e: virtual data frame and heartbeat reached the monitor"
