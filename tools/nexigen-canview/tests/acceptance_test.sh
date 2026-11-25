#!/usr/bin/env bash
set -u
bin="$1"
config_dir="$2"
fixture="$3"
work="$4"
mkdir -p "$work"
config="$work/config.json"
session="$work/session.jsonl"

"$bin" --help >/dev/null 2>&1 || exit 1
"$bin" --nope >/dev/null 2>&1; [ $? -eq 2 ] || exit 1

# Eight enabled channels are accepted by the CLI; a ninth is rejected by validation.
eight=()
for i in 1 2 3 4 5 6 7 8; do eight+=(--channel "ch$i:$i"); done
"$bin" --config "$config" --headless --mock 1 "${eight[@]}" >/dev/null 2>&1 || exit 1
"$bin" --config "$config" --headless --mock 1 "${eight[@]}" --channel ch9:9 >/dev/null 2>&1
[ $? -eq 2 ] || exit 1

# JSONL export -> import round-trip through the headless path.
"$bin" --config "$config" --headless --mock 20 --json > "$session" 2>/dev/null || exit 1
"$bin" --config "$config" --headless --offline "$session" --max-records 3 >/dev/null 2>&1 || exit 1
"$bin" --config "$config" --headless --offline "$fixture" --json --max-records 1 >/dev/null 2>&1 || exit 1

# 1M / 2M capacity plans are covered by the unit suite.
"$bin" --config "$config" --headless --mock 1 --memory-mb 256 >/dev/null 2>&1 || exit 1
"$bin" --config "$config" --headless --mock 1 --memory-mb 384 >/dev/null 2>&1 || exit 1
exit 0
