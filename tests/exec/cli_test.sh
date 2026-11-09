#!/usr/bin/env bash
# nexigen-exec CLI smoke test: suite start, status (text + JSON), stop.
set -euo pipefail

BIN="${1:?usage: cli_test.sh <nexigen-exec>}"
BIN="$(realpath "$BIN")"
TEST_TMP="${NEXIGEN_TEST_TMPDIR:-/tmp/opencode}"
mkdir -p "$TEST_TMP"
ROOT="$(mktemp -d "$TEST_TMP/nexigen_cli_XXXXXX")"
ART="$(mktemp -d "$TEST_TMP/nexigen_cli_art_XXXXXX")"
MANIFEST="$ROOT/manifest.json"

cleanup() {
    "$BIN" stop --config "$MANIFEST" --grace 1 >/dev/null 2>&1 || true
    rm -rf "$ROOT" "$ART"
}
trap cleanup EXIT

cat > "$MANIFEST" <<EOF
{
  "version": 1,
  "suite_id": "cli",
  "run_id": "run-cli",
  "run_dir": "$ROOT",
  "artifacts_path": "$ART",
  "log_level": "info",
  "suite_restart_attempts": 0,
  "suite_restart_delay_s": 0,
  "apps": [
    {
      "app": "cli_app",
      "instance": "1",
      "command": "/bin/sh",
      "args": ["-c", "echo cli-out; exec sleep 60"],
      "impact_on_app_suite_operability": "not_impact",
      "restart_attempts": 0,
      "restart_delay_s": 0
    }
  ]
}
EOF

"$BIN" start --config "$MANIFEST" | grep -q "started:"
EXECUTION_MANAGER_LOG="$ART/cli/execution_manager.jsonl"
for _ in {1..50}; do
    [ -f "$EXECUTION_MANAGER_LOG" ] && grep -q "event=suite_started" "$EXECUTION_MANAGER_LOG" && break
    sleep 0.1
done
grep -q "event=command" "$EXECUTION_MANAGER_LOG"
grep -q "event=suite_started" "$EXECUTION_MANAGER_LOG"
"$BIN" --help | grep -q -- "--config"
"$BIN" status --config "$MANIFEST" | grep -q "suite: running"
"$BIN" status --config "$MANIFEST" --json | grep -q '"state": "running"'
CONFIG_STATUS="$("$BIN" status --config "$MANIFEST")"
DIR_STATUS="$("$BIN" status --suite-dir "$ROOT/.suite/cli")"
[ "$CONFIG_STATUS" = "$DIR_STATUS" ]
"$BIN" stop --config "$MANIFEST" --grace 2 | grep -q "stopped:"
grep -q "event=suite_stopped" "$EXECUTION_MANAGER_LOG"
"$BIN" status --config "$MANIFEST" | grep -q "suite: stopped"
"$BIN" stop --config "$MANIFEST" --grace 0 | grep -q "stopped:"

# An invalid manifest still logs through the working-directory fallback.
mkdir -p "$ROOT/fallback_cwd"
printf '{bad json' > "$ROOT/bad.json"
if (cd "$ROOT/fallback_cwd" && "$BIN" start --config "$ROOT/bad.json"); then
    echo "invalid manifest unexpectedly succeeded" >&2
    exit 1
fi
grep -q "event=command" "$ROOT/fallback_cwd/.nexigen-exec/execution_manager.jsonl"

# Failed exec is reported visibly (change fix-p0-critical-defects): the CLI
# exits nonzero printing `start failed` on stderr, and no resource.json /
# exit.json is published for the failed start (spec execution_manager/process-supervision
# "A failed exec fails the start visibly").
FAIL_DIR="$ROOT/failedstart"
if "$BIN" start --run-id run-fail --app fail_app --instance 1 \
        --run-dir "$FAIL_DIR" -- /nonexistent/nexigen/definitely-missing \
        >"$ROOT/fail.out" 2>"$ROOT/fail.err"; then
    echo "start of a missing binary unexpectedly succeeded" >&2
    exit 1
fi
grep -q "start failed" "$ROOT/fail.err"
[ ! -e "$FAIL_DIR/fail_app/1/resource.json" ]
[ ! -e "$FAIL_DIR/fail_app/1/exit.json" ]

echo "cli checks passed"
