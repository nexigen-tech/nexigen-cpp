#!/usr/bin/env bash
# nexigen-run CLI smoke test: suite start, status (text + JSON), stop.
set -euo pipefail

BIN="${1:?usage: cli_test.sh <nexigen-run>}"
ROOT="$(mktemp -d /tmp/opencode/nexigen_cli_XXXXXX)"
ART="$(mktemp -d /tmp/opencode/nexigen_cli_art_XXXXXX)"
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
"$BIN" --help | grep -q -- "--config"
"$BIN" status --config "$MANIFEST" | grep -q "suite: running"
"$BIN" status --config "$MANIFEST" --json | grep -q '"state": "running"'
CONFIG_STATUS="$("$BIN" status --config "$MANIFEST")"
DIR_STATUS="$("$BIN" status --suite-dir "$ROOT/.suite/cli")"
[ "$CONFIG_STATUS" = "$DIR_STATUS" ]
"$BIN" stop --config "$MANIFEST" --grace 2 | grep -q "stopped:"
"$BIN" status --config "$MANIFEST" | grep -q "suite: stopped"
"$BIN" stop --config "$MANIFEST" --grace 0 | grep -q "stopped:"

echo "cli checks passed"
