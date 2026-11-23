#!/usr/bin/env bash
# Determinism smoke: the same seed produces the same injected fault sequence.
set -euo pipefail
vbus="$1"; rx="$2"; tx="$3"; mon="$4"; sender="$5"
root="$(mktemp -d /tmp/opencode/can-seed.XXXXXX)"
trap 'rm -rf "$root"' EXIT

run_once() {
    local out="$1"
    local pids=()
    "$vbus" --app-id VBUS --channel can0=can0 --channel can1=can1 --embedded \
        --seed 42 --inject-errors --inject-overload --inject-warning \
        --inject-passive --inject-bus-off >"$out.vbus" 2>&1 & pids+=($!)
    sleep 0.5
    "$rx" --app-id CANRX --channel can0=can0 --channel can1=can1 --backend virtual \
        --embedded >"$out.rx" 2>&1 & pids+=($!)
    sleep 0.5
    "$tx" --app-id CANTX --channel can0=can0 --channel can1=can1 --backend virtual \
        --embedded >"$out.tx" 2>&1 & pids+=($!)
    "$mon" --app-id CANMON --channel can0=can0 --channel can1=can1 --embedded --json \
        >"$out.mon" 2>&1 & pids+=($!)
    sleep 1.5
    "$sender" --app-id CANSND --channel can0=can0 --channel can1=can1 --embedded \
        >"$out.sender" 2>&1 || true
    sleep 2
    for pid in "${pids[@]}"; do kill -TERM "$pid" 2>/dev/null || true; done
    for pid in "${pids[@]}"; do wait "$pid" 2>/dev/null || true; done
    python3 - "$out.mon" "$out.sequence" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8", errors="ignore") as src, open(sys.argv[2], "w") as dst:
    for line in src:
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if record.get("kind") in ("Error", "Overload"):
            dst.write(f"{record.get('kind')}|{record.get('error_flags')}|{record.get('channel_id')}\n")
PY
}

run_once "$root/a"
run_once "$root/b"
if ! diff -u "$root/a.sequence" "$root/b.sequence"; then
    echo "fault seed smoke: sequences differ" >&2
    exit 1
fi
lines=$(wc -l < "$root/a.sequence")
if [[ "$lines" -lt 4 ]]; then
    echo "fault seed smoke: too few injected faults ($lines)" >&2
    exit 1
fi
echo "fault seed smoke: identical $lines fault records for seed 42"
