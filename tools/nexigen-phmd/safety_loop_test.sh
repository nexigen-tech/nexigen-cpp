#!/usr/bin/env bash
# nexigen-phmd safety-loop end-to-end
# (change complete-phm-safety-loop; spec phm/recovery).
#
# Proves the whole escalation ladder against the real daemon:
#   1. a supervised process stops reporting, so its alive window expires;
#   2. the daemon asks the Execution Manager to restart it and is admitted;
#   3. the Execution Manager spends the last attempt and publishes Failed;
#   4. the daemon marks the process Exhausted and escalates to the State Manager;
#   5. an Accepted acknowledgement keeps the platform alive;
#   6. a Rejected or absent acknowledgement stops the feed and fires the
#      watchdog reaction.
#
# The helper plays the application, the Execution Manager, and the State
# Manager at once, so no other daemon is required.
set -euo pipefail

PHMD="${1:?usage: safety_loop_test.sh <nexigen-phmd> <safety-loop-helper>}"
HELPER="${2:?usage: safety_loop_test.sh <nexigen-phmd> <safety-loop-helper>}"

DIR="$(mktemp -d)"
PHMD_PID=""
cleanup() {
    if [ -n "$PHMD_PID" ] && kill -0 "$PHMD_PID" 2>/dev/null; then
        kill -TERM "$PHMD_PID" 2>/dev/null || true
        wait "$PHMD_PID" 2>/dev/null || true
    fi
    rm -rf "$DIR"
}
trap cleanup EXIT

MANIFEST="$DIR/manifest.json"
cat >"$MANIFEST" <<'JSON'
{
  "processes": [
    {
      "name": "probe",
      "service_name": "Nexigen/Test/PhmdSafetyLoop",
      "alive_reference_cycle_ms": 200,
      "alive_min_margin_ms": 0,
      "alive_max_margin_ms": 0,
      "failed_supervision_cycles_tolerance": 0,
      "checkpoints": [1],
      "transitions": [{ "from_id": 1, "to_id": 1 }],
      "recovery_rules": [{ "action": "RestartProcess", "function_group": "" }]
    }
  ],
  "recovery_timeout_ms": 500,
  "watchdog": { "enabled": false }
}
JSON

fail() {
    echo "[phmd_safety_loop] $*" >&2
    echo "--- daemon log ---" >&2
    cat "$DIR/daemon.log" >&2 2>/dev/null || true
    exit 1
}

# Start the daemon and wait until it supervises the probe.
start_daemon() {
    : >"$DIR/daemon.log"
    "$PHMD" --manifest "$MANIFEST" --fault-log "$DIR/faults.log" \
        >"$DIR/daemon.log" 2>&1 &
    PHMD_PID=$!
    local started=0
    for _ in $(seq 1 100); do
        if grep -q "supervising 1 process(es)" "$DIR/daemon.log" 2>/dev/null; then
            started=1
            break
        fi
        if ! kill -0 "$PHMD_PID" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done
    if [ "$started" -ne 1 ]; then
        fail "the daemon did not start supervision"
    fi
}

stop_daemon() {
    if [ -n "$PHMD_PID" ] && kill -0 "$PHMD_PID" 2>/dev/null; then
        kill -TERM "$PHMD_PID" 2>/dev/null || true
        for _ in $(seq 1 50); do
            kill -0 "$PHMD_PID" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$PHMD_PID" 2>/dev/null; then
            kill -KILL "$PHMD_PID" 2>/dev/null || true
        fi
        wait "$PHMD_PID" 2>/dev/null || true
    fi
    PHMD_PID=""
}

# Run one mode and assert the daemon's reaction.
#   $1 mode: acked | rejected | noack
#   $2 watchdog: yes | no
run_case() {
    local mode="$1"
    local expect_watchdog="$2"
    local summary="$DIR/$mode.summary"

    start_daemon
    set +e
    "$HELPER" drive "Nexigen/Test/PhmdSafetyLoop" "probe" "motion_control" "$mode" "$summary"
    local helper_status=$?
    set -e
    if [ "$helper_status" -ne 0 ]; then
        fail "the helper failed in mode '$mode' (status $helper_status)"
    fi

    # Give the daemon room to finish its budget and log the verdict.
    sleep 0.6
    stop_daemon

    if [ ! -f "$summary" ]; then
        fail "the helper wrote no summary in mode '$mode'"
    fi
    grep -q "em_requests=[1-9]" "$summary" || fail "mode '$mode': no EM recovery request"
    grep -q "sm_notifications=1" "$summary" || fail "mode '$mode': expected one SM notification"
    if [ "$mode" != "noack" ]; then
        grep -q "ack_sent=1" "$summary" || fail "mode '$mode': no acknowledgement sent"
    fi

    grep -q "expired -> recovery dispatched" "$DIR/daemon.log" ||
        fail "mode '$mode': the daemon did not dispatch local recovery"
    if grep -q "expired with unknown function group" "$DIR/daemon.log"; then
        fail "mode '$mode': the process ran without a Function Group"
    fi
    grep -q "exhausted -> escalation" "$DIR/daemon.log" ||
        fail "mode '$mode': the daemon did not escalate exhaustion"

    if [ "$expect_watchdog" = "yes" ]; then
        grep -q -- "-> watchdog" "$DIR/daemon.log" ||
            fail "mode '$mode': expected a watchdog reaction"
    else
        if grep -q -- "-> watchdog" "$DIR/daemon.log"; then
            fail "mode '$mode': unexpected watchdog reaction after an accepted ack"
        fi
    fi
}

# 5. Accepted acknowledgement keeps the platform alive.
run_case acked no
# 6a. Refused acknowledgement fires the watchdog reaction.
run_case rejected yes
# 6b. An absent acknowledgement expires into the watchdog.
run_case noack yes

echo "[phmd_safety_loop] ok"
