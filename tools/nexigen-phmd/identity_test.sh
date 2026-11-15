#!/usr/bin/env bash
# nexigen-phmd identity end-to-end (change checkpoint-identity).
#
# Proves the daemon-side identity path that unit tests cannot reach:
#   1. unverified checkpoints are dropped, so the process expires;
#   2. a matching claim lets checkpoints through (no fault);
#   3. a foreign claim is published as a ClaimRejected security event;
#   4. a lifecycle stop resets the proof, so the next incarnation must claim.
#
# The helper claims its own pid and real uid, which is exactly what the daemon
# verifies through /proc. requires a non-root user because expected_uid rejects
# 0; the CI COM job runs this script as an unprivileged user.
set -euo pipefail

PHMD="${1:?usage: identity_test.sh <nexigen-phmd> <identity-helper>}"
HELPER="${2:?usage: identity_test.sh <nexigen-phmd> <identity-helper>}"

if [ "$(id -u)" -eq 0 ]; then
    echo "[phmd_identity] skipped: expected_uid rejects 0; rerun as a non-root user"
    exit 0
fi

DIR="$(mktemp -d)"
PHMD_PID=""
SECURITY_PID=""

cleanup() {
    if [ -n "$SECURITY_PID" ] && kill -0 "$SECURITY_PID" 2>/dev/null; then
        kill -TERM "$SECURITY_PID" 2>/dev/null || true
        wait "$SECURITY_PID" 2>/dev/null || true
    fi
    if [ -n "$PHMD_PID" ] && kill -0 "$PHMD_PID" 2>/dev/null; then
        kill -TERM "$PHMD_PID" 2>/dev/null || true
        wait "$PHMD_PID" 2>/dev/null || true
    fi
    rm -rf "$DIR"
}
trap 'cleanup' EXIT

UID_NOW="$(id -u)"
CLAIM_SAMPLE="Nexigen/PHM/Claim"
CLAIM_EVENT="Nexigen/PHM/ClaimEvent"
SECURITY_SAMPLE="Nexigen/PHM/Security"
SECURITY_EVENT="Nexigen/PHM/SecurityEvent"

write_manifest() { # service-name
    cat >"$DIR/manifest.json" <<JSON
{
  "processes": [
    {
      "name": "gated_app",
      "service_name": "$1",
      "alive_reference_cycle_ms": 200,
      "alive_min_margin_ms": 1,
      "alive_max_margin_ms": 150,
      "failed_supervision_cycles_tolerance": 2,
      "checkpoints": [1],
      "transitions": [{"from_id": 1, "to_id": 1}],
      "expected_uid": $UID_NOW
    }
  ]
}
JSON
}

start_phmd() { # tag service-name fault-log
    write_manifest "$2"
    "$PHMD" --manifest "$DIR/manifest.json" --fault-log "$3" \
        >"$DIR/phmd-$1.out" 2>&1 &
    PHMD_PID=$!
    for _ in $(seq 1 200); do
        if grep -q "supervising 1 process" "$DIR/phmd-$1.out" 2>/dev/null; then
            return 0
        fi
        if ! kill -0 "$PHMD_PID" 2>/dev/null; then
            break
        fi
        sleep 0.05
    done
    echo "nexigen-phmd did not start:" >&2
    cat "$DIR/phmd-$1.out" >&2
    exit 1
}

stop_phmd() {
    kill -TERM "$PHMD_PID" 2>/dev/null || true
    wait "$PHMD_PID" 2>/dev/null || true
    PHMD_PID=""
}

start_helper() { # checkpoint-service claim-sample claim-event uid-mode duration
    "$HELPER" run "$1" "$2" "$3" gated_app "$4" "$5" &
    HELPER_PID=$!
}

stop_helper() {
    kill -TERM "$HELPER_PID" 2>/dev/null || true
    wait "$HELPER_PID" 2>/dev/null || true
    HELPER_PID=""
}

expect_fault() { # fault-log scenario
    if ! grep -q "AliveFailure" "$1"; then
        echo "expected an AliveFailure for scenario '$2'; fault log:" >&2
        cat "$1" >&2
        exit 1
    fi
}

expect_no_fault() { # fault-log scenario
    if grep -q "AliveFailure" "$1"; then
        echo "expected no AliveFailure for scenario '$2'; fault log:" >&2
        cat "$1" >&2
        exit 1
    fi
}

# 1. No claim: every checkpoint is dropped, so the process expires.
start_phmd gating "Nexigen/Test/PhmdIdentity/Gating" "$DIR/faults-gating.log"
start_helper "Nexigen/Test/PhmdIdentity/Gating" - - none 5000
sleep 1.0
stop_phmd
stop_helper
expect_fault "$DIR/faults-gating.log" "unverified checkpoints are dropped"

# 2. A matching claim: checkpoints reach the supervisor and no fault appears.
start_phmd verified "Nexigen/Test/PhmdIdentity/Verified" "$DIR/faults-verified.log"
start_helper "Nexigen/Test/PhmdIdentity/Verified" "$CLAIM_SAMPLE" "$CLAIM_EVENT" self 5000
sleep 1.5
stop_phmd
stop_helper
expect_no_fault "$DIR/faults-verified.log" "verified checkpoints are processed"

# 3. A foreign claim: the daemon publishes a ClaimRejected security event and
#    keeps the gate closed.
"$HELPER" watch-security "$SECURITY_SAMPLE" "$SECURITY_EVENT" 5000 \
    "$DIR/security.txt" &
SECURITY_PID=$!
start_phmd foreign "Nexigen/Test/PhmdIdentity/Foreign" "$DIR/faults-foreign.log"
start_helper "Nexigen/Test/PhmdIdentity/Foreign" "$CLAIM_SAMPLE" "$CLAIM_EVENT" foreign 3000
for _ in $(seq 1 100); do
    [ -s "$DIR/security.txt" ] && break
    sleep 0.05
done
wait "$SECURITY_PID" || true
stop_phmd
stop_helper
if ! grep -q "ClaimRejected" "$DIR/security.txt" 2>/dev/null; then
    echo "expected a ClaimRejected security event; got:" >&2
    cat "$DIR/security.txt" 2>/dev/null >&2 || true
    exit 1
fi

# 4. A lifecycle stop drops the proof: the next incarnation claims nothing and
#    therefore expires even though its checkpoints flow.
start_phmd reset "Nexigen/Test/PhmdIdentity/Reset" "$DIR/faults-reset.log"
"$HELPER" run "Nexigen/Test/PhmdIdentity/Reset" "$CLAIM_SAMPLE" "$CLAIM_EVENT" \
    gated_app self 5000 --reset &
HELPER_PID=$!
sleep 2.5
stop_phmd
stop_helper
expect_fault "$DIR/faults-reset.log" "a restarted process claims again"

echo "[phmd_identity] all scenarios passed"
