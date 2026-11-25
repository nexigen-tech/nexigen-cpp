#!/usr/bin/env bash
set -u
bin="$1"
config="$2"
fixture="$3"
"$bin" --help >/dev/null 2>&1 || exit 1
"$bin" --nope >/dev/null 2>&1
[ $? -eq 2 ] || exit 1
"$bin" --config "$config" --headless >/dev/null 2>&1
[ $? -eq 2 ] || exit 1
"$bin" --config "$config" --headless --offline /nonexistent/session.jsonl >/dev/null 2>&1
[ $? -eq 1 ] || exit 1
"$bin" --config "$config" --headless --offline "$fixture" --max-records 2 >/dev/null 2>&1
[ $? -eq 0 ] || exit 1
exit 0
